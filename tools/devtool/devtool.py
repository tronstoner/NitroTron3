#!/usr/bin/env python3
"""Local dev interface for the pedals (`task dev`) - steps 1 + 2 of docs/DEV_INTERFACE.md.

  * Owns the USB serial port (the connection, `?`/BUILD query, reconnect and
    log-file code is tools/diag/log.py's Session, imported, not copied): log
    files are written exactly as `task log` writes them
    (log/<pedal>-<YYYYMMDD-HHMM>.log, stamped lines, one section per connection).
  * Serves the dev page on 127.0.0.1 (port: the last one used if it is free,
    else a free one) and prints `devtool: http://localhost:<port>`.
      GET  /             the page (tools/devtool/web/)
      GET  /events       Server-Sent Events: hello, status, line, sep, flash, flashout
      GET  /api/status   connection / build / flash state as JSON
      POST /api/flash    {"pedal": "nitrolab"|"chronotron3"|"nitrotron3m"}
  * Flash: make (DIAG=1) -> `dfu` over serial (or the footswitch gesture) ->
    wait for the DFU device -> make program-dfu. Only on a click, one at a time.
    Tool output is split like a terminal does it: a '\r' without '\n' (dfu-util's
    progress bar) replaces the current line (`flashout` event with replace: true).
  * Live constants (step 2): the registry is parsed from the connected pedal's
    constants files (tools/devtool/registry.py: TUNABLE(...) lines + @ tags);
    after BUILD devtool sends `list` and merges the pedal's values (the pedal is
    the truth; re-synced after every reconnect / flash).
      GET  /api/registry   registry + values + guard state
      GET  /api/tables     the SW1 MID audition bank's wavetables as the pedal builds them
                           (tools/hostsim/tbdump, built on demand; cached per HEAD + explorer*.h state)
      POST /api/set        {"name": N, "value": V}   (throttled, latest wins, <= ~30 sends/s)
      POST /api/reset      {"name": N} | {"name": "all"}
      POST /api/note       {"text": "what I heard"}
      POST /api/override   {"on": true|false}   (allow `set` despite the mismatch guard, this session)
    SSE: tun (snapshot), tval (a value echo from the pedal), terr (pedal error).
    tune/changes.json is rewritten on every change (changed constants vs the
    compiled defaults + the note), for the agent to apply to the constants files;
    a previous non-empty file that a re-sync would drop is kept in tune/history/.

  tools/devtool/devtool.py              run it (Ctrl-C quits)
  tools/devtool/devtool.py --selftest   offline self-test (pty pair, no pedal)

Python 3 stdlib only. Only one program can hold the port: while devtool runs it
is the logger (do not run `task log` at the same time).
"""
import codecs
import collections
import glob
import hashlib
import http.client
import json
import math
import os
import queue
import re
import select
import shutil
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WEB_DIR = os.path.join(HERE, "web")
sys.path.insert(0, os.path.join(REPO, "tools", "diag"))
sys.dont_write_bytecode = True      # no __pycache__ in tools/diag
import log as diaglog  # noqa: E402  (tools/diag/log.py: Session, find_device, git_head)
sys.path.insert(0, HERE)
import registry as reglib  # noqa: E402  (tools/devtool/registry.py)

PORT_FILE = os.path.join(diaglog.LOG_DIR, ".devtoolport")
BACKLOG = 5000            # log lines replayed to a (re)connecting page
FLASH_BACKLOG = 4000      # flash output lines kept for the current/last flash
KEEPALIVE_S = 15.0

# make arguments per pedal, as the Taskfile *-debug tasks pass them. They go on
# EVERY make call (build and program-dfu), or the profile stamp wipes build/.
PEDALS = collections.OrderedDict([
    ("nitrolab", ["PEDAL=nitrolab", "DIAG=1"]),
    ("chronotron3", ["PEDAL=chronotron3", "DIAG=1"]),
    ("nitrotron3m", ["PEDAL=nitrotron3m", "NT3M_SLOT_B=sprawl", "INSTRUMENT=bass", "DIAG=1"]),
])
DEFAULT_PEDAL = "nitrolab"
DFU_ID = "0483:df11"      # Daisy bootloader (libDaisy core/Makefile DAISY_PID)
DFU_HINT_S = 15.0         # no DFU device after this -> ask for the footswitch gesture
DFU_MAX_S = 120.0         # give up waiting for the DFU device
DFU_POLL_S = 0.5
STATIC = {"/": ("index.html", "text/html; charset=utf-8"),
          "/index.html": ("index.html", "text/html; charset=utf-8"),
          "/app.css": ("app.css", "text/css; charset=utf-8"),
          "/app.js": ("app.js", "text/javascript; charset=utf-8"),
          "/core.js": ("core.js", "text/javascript; charset=utf-8"),
          "/icons/icons.svg": ("icons/icons.svg", "image/svg+xml; charset=utf-8")}
LOCAL_HOSTS = ("localhost", "127.0.0.1", "[::1]")

# Live constants: where each pedal's TUNABLE(...) constants files live (dirs are
# scanned for .h files with TUNABLE( lines). nitrotron3m's explorer slot uses
# NitroLab's explorer module.
CONST_SOURCES = {
    "nitrolab": ["pedals/nitrolab"],
    "chronotron3": ["pedals/chronotron3"],
    "nitrotron3m": ["pedals/nitrotron3m", "pedals/nitrolab/modules/explorer_constants.h",
                    "pedals/nitrolab/modules/explorer_tables.h"],
}
TUNE_DIR = os.path.join(REPO, "tune")
SEND_MIN_S = 1.0 / 30.0   # throttle: at most ~30 command writes per second
LIST_TIMEOUT_S = 2.0      # no `T END` after this -> ask once more, then give up
CHANGES_WRITE_S = 0.25    # tune/changes.json at most this often (last change always lands)
T_VAL_RE = re.compile(r"^T (\S+) ([fib]) (\S+)$")
T_END_RE = re.compile(r"^T END (\d+)$")
T_ERR_RE = re.compile(r"^T ERR (.*)$")


def now_ms(t=None):
    return int(round((time.time() if t is None else t) * 1000))


# ---------------------------------------------------------------------------
# Event hub: fan-out to every open page + backlog for (re)connecting pages
# ---------------------------------------------------------------------------
class Hub:
    def __init__(self):
        self.lock = threading.Lock()
        self.clients = set()
        self.backlog = collections.deque(maxlen=BACKLOG)       # ("line"|"sep", data)
        self.flash_out = collections.deque(maxlen=FLASH_BACKLOG)
        self.status = {}
        self.flash = {}
        self.instance = "%x" % now_ms()
        self.tun_provider = None    # () -> current live-constants snapshot (Tuner.snapshot)

    def publish(self, kind, data):
        with self.lock:
            if kind in ("line", "sep"):
                self.backlog.append((kind, data))
            elif kind == "flashout":
                if data.get("replace") and self.flash_out:
                    # '\r' line: the backlog keeps only the latest state of the line
                    self.flash_out[-1] = (kind, dict(data, replace=False))
                else:
                    self.flash_out.append((kind, dict(data, replace=False)))
            elif kind == "status":
                self.status = data
            elif kind == "flash":
                if data.get("state") == "building" and self.flash.get("state") != "building":
                    self.flash_out.clear()
                self.flash = data
            dead = []
            for q in self.clients:
                try:
                    q.put_nowait((kind, data))
                except queue.Full:
                    dead.append(q)          # a stuck page: drop it, it reconnects
            for q in dead:
                self.clients.discard(q)
                try:
                    q.put_nowait(None)
                except queue.Full:
                    pass

    def subscribe(self):
        q = queue.Queue(maxsize=20000)
        tun = self.tun_provider() if self.tun_provider else None
        with self.lock:
            snap = [("hello", {"instance": self.instance})]
            snap.append(("status", self.status))
            snap.append(("flash", self.flash))
            if tun is not None:
                snap.append(("tun", tun))
            snap.extend(self.backlog)
            snap.extend(self.flash_out)
            self.clients.add(q)
        return q, snap

    def unsubscribe(self, q):
        with self.lock:
            self.clients.discard(q)


# ---------------------------------------------------------------------------
# Serial session: log.py's Session + events for the page
# ---------------------------------------------------------------------------
class DevSession(diaglog.Session):
    """log.py's Session (connect, `?`/BUILD, reconnect, log files) publishing to a Hub."""

    def __init__(self, hub, **kw):
        self.hub = hub
        self.tuner = None            # set by App (live constants)
        super().__init__(**kw)

    def _show(self, text):
        super()._show(text)
        if text.startswith("--- pedal back ") or text.startswith("--- pedal gone "):
            self.hub.publish("sep", {"t": now_ms(self.clock()),
                                     "what": "back" if "back" in text[:16] else "gone"})

    def _line(self, text):
        t = self.clock()
        had_build = self.build
        super()._line(text)
        text = text.rstrip("\r")
        quiet = self.tuner.on_line(text) if self.tuner else False
        if not quiet:   # `T <name> ..` value lines go to the file, not the page log (the rows show them)
            self.hub.publish("line", {"t": now_ms(t), "text": text})
        if self.build != had_build:
            self.publish_status()
            if self.build and self.tuner:
                self.tuner.on_build(self.build[0], self.build[1])

    def _connect(self, dev):
        super()._connect(dev)
        self.publish_status()

    def _disconnect(self):
        super()._disconnect()
        self.publish_status()
        if self.tuner:
            self.tuner.on_disconnect()

    def _resolve_build(self):
        super()._resolve_build()
        self.publish_status()

    def build_state(self):
        if self.fd is None:
            return "waiting"
        if self.build:
            return "ok"
        return "asking" if self.query_sent_at is not None else "none"

    def status(self):
        b = None
        if self.build:
            pedal, hsh, btime = self.build
            b = {"pedal": pedal, "hash": hsh.split("-")[0], "dirty": hsh.endswith("-dirty"),
                 "time": btime}
        return {"connected": self.fd is not None, "dev": self.dev if self.fd is not None else None,
                "build_state": self.build_state(), "build": b, "note": self.build_note,
                "log_file": os.path.relpath(self.file_path, REPO) if self.file_path else None}

    def publish_status(self):
        self.hub.publish("status", self.status())

    def send(self, data):
        if self.fd is None:
            return False
        try:
            os.write(self.fd, data)
            return True
        except OSError:
            return False


class SerialLoop:
    """Runs DevSession.step() in its own thread; other threads go through the lock."""

    def __init__(self, sess, tick=None):
        self.sess = sess
        self.tick = tick             # called after every step, under the lock (serial thread)
        self.lock = threading.RLock()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, name="serial", daemon=True)

    def _run(self):
        while not self.stop.is_set():
            with self.lock:
                self.sess.step(timeout=0.02)
                if self.tick:
                    self.tick()
            time.sleep(0.005)       # let other threads take the lock

    def start(self):
        with self.lock:
            self.sess.publish_status()
        self.thread.start()

    def call(self, fn, *a):
        with self.lock:
            return fn(*a)

    def close(self):
        self.stop.set()
        if self.thread.is_alive():
            self.thread.join(1.0)
        with self.lock:
            self.sess.close()


# ---------------------------------------------------------------------------
# Live constants (step 2): registry + pedal values + changes file
# ---------------------------------------------------------------------------
def f32(x):
    """x as the float32 the pedal stores (the firmware's TUNABLE floats)."""
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def files_dirty(paths):
    """True if any of the repo-relative paths differ from HEAD (None: git failed)."""
    if not paths:
        return False
    try:
        r = subprocess.run(["git", "-C", REPO, "diff", "--quiet", "HEAD", "--"] + list(paths),
                           capture_output=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    return r.returncode != 0


def local_iso(t=None):
    """ISO 8601 local time with UTC offset (the one time format of tune/changes.json)."""
    t = time.time() if t is None else t
    z = time.strftime("%z", time.localtime(t))
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(t)) + (z[:3] + ":" + z[3:] if z else "")


class Tuner:
    """Registry (from the constants files), the pedal's values (`list` / echoes),
    the throttled command queue, the mismatch guard and tune/changes.json.
    on_line / on_build / on_disconnect / tick run in the serial thread (under its
    lock); request_* come from HTTP threads and only queue. One internal lock."""

    def __init__(self, hub, sess, repo=REPO, head=diaglog.git_head, dirty=files_dirty,
                 tune_dir=TUNE_DIR, clock=time.time, list_timeout=LIST_TIMEOUT_S):
        self.hub, self.sess, self.repo = hub, sess, repo
        self.head, self.dirty, self.tune_dir, self.clock = head, dirty, tune_dir, clock
        self.list_timeout = list_timeout
        self.lock = threading.RLock()
        self.pedal = DEFAULT_PEDAL       # registry shown before a pedal answered
        self.firmware = None             # BUILD hash as sent ("abc1234[-dirty]")
        self.reg = None
        self.by_name = {}
        self.values = {}                 # name -> value as the pedal reported it
        self.fw_names = None             # names in the pedal's last full `list`
        self.state = "offline"           # offline | asking | synced | no-answer
        self.list_at = None
        self.list_tries = 0
        self.list_seen = []
        self.pending = collections.OrderedDict()   # name|"*" -> command line (latest wins)
        self.last_send = 0.0
        self.override = False
        self.note = ""
        self.guard = {"ok": False, "reasons": ["no pedal"], "warnings": []}
        self.repo_head = None
        self.last_err = None
        self.changes_due = False
        self.changes_at = 0.0
        self.resync_archive = False
        self._load_registry(self.pedal)
        self._compute_guard()

    # ---- registry / guard ------------------------------------------------
    def _load_registry(self, pedal):
        self.reg = reglib.build(self.repo, pedal, CONST_SOURCES.get(pedal, ["pedals/" + pedal]))
        self.by_name = {c["name"]: c for c in self.reg["constants"]}

    def _compute_guard(self):
        reasons, warnings = [], []
        if self.state == "offline":
            reasons.append("no pedal connected")
        elif self.state == "no-answer":
            reasons.append("the pedal does not answer `list` (firmware without live constants? flash a current DIAG build)")
        self.repo_head = head = self.head()
        if self.firmware:
            built = self.firmware.split("-")[0]
            if head and built != head:
                reasons.append("firmware %s is not the repo HEAD %s: the constants files may not match the "
                               "firmware (flash, or allow changes)" % (built, head))
            if self.firmware.endswith("-dirty"):
                warnings.append("firmware was built from a dirty tree")
        d = self.dirty(self.reg["files"])
        if d:
            reasons.append("constants file(s) changed since HEAD (uncommitted): the firmware may not match "
                           "them (commit + flash, or allow changes)")
        elif d is None:
            warnings.append("git status unknown")
        if self.fw_names is not None:
            fw, rg = set(self.fw_names), set(self.by_name)
            extra = sorted(fw - rg)
            miss = sorted(rg - fw)
            if extra:     # the firmware knows constants the files do not: files and firmware disagree
                reasons.append("the pedal has live constants the files do not declare (%s%s)" % (
                    ", ".join(extra[:4]), ", ..." if len(extra) > 4 else ""))
            if miss:      # e.g. a module not compiled into this build: those rows are shown as off
                warnings.append("%d constant(s) from the files are not live in this firmware (%s%s)" % (
                    len(miss), ", ".join(miss[:3]), ", ..." if len(miss) > 3 else ""))
        if self.reg["problems"]:
            warnings.append("registry: " + "; ".join(self.reg["problems"][:3]))
        self.guard = {"ok": not reasons, "reasons": reasons, "warnings": warnings}

    def writable(self):
        if self.state != "synced":
            return False
        return self.guard["ok"] or (self.override and self.state == "synced")

    # ---- serial side (serial thread) --------------------------------------
    def on_build(self, pedal, hsh):
        with self.lock:
            self.pedal = pedal
            self.firmware = hsh
            self._load_registry(pedal)
            self.values = {}
            self.fw_names = None
            self.pending.clear()
            self.state = "asking"
            self.list_tries = 0
            self._ask_list()
            self._compute_guard()
        self.publish()

    def on_disconnect(self):
        with self.lock:
            self.state = "offline"
            self.firmware = None
            self.pending.clear()
            self._compute_guard()
        self.publish()

    def _ask_list(self):
        self.list_seen = []
        self.list_at = self.clock()
        self.list_tries += 1
        self.sess.send(b"list\n")

    def _parse_value(self, ty, text):
        if ty == "b":
            return text in ("1", "true")
        if ty == "i":
            return int(text)
        return float(text)      # the pedal's shortest text (compared as float32, written as sent)

    def on_line(self, text):
        """A pedal line. Returns True for `T <name>` / `T END` lines (kept off the page log)."""
        if not text.startswith("T "):
            return False
        m = T_VAL_RE.match(text)
        if m and m.group(1) not in ("END", "ERR"):
            name, ty, val = m.groups()
            try:
                v = self._parse_value(ty, val)
            except ValueError:
                return False
            with self.lock:
                self.values[name] = v
                if self.state == "asking":
                    self.list_seen.append(name)
                self.changes_due = True
            self.hub.publish("tval", {"name": name, "value": v, "t": now_ms(self.clock())})
            return True
        m = T_END_RE.match(text)
        if m:
            with self.lock:
                if self.state in ("asking", "no-answer"):
                    self.fw_names = list(self.list_seen)
                    self.state = "synced"
                    self.resync_archive = True
                    self.changes_due = True
                    self._compute_guard()
            self.publish()
            return True
        m = T_ERR_RE.match(text)
        if m:
            with self.lock:
                self.last_err = {"text": m.group(1), "t": now_ms(self.clock())}
            self.hub.publish("terr", self.last_err)
            return False     # errors stay in the page log too
        return False

    def tick(self):
        """Serial thread, every loop: `list` timeout, throttled sends, changes file."""
        now = self.clock()
        with self.lock:
            if self.state == "asking" and self.list_at is not None and now - self.list_at >= self.list_timeout:
                if self.list_tries < 2:
                    self._ask_list()
                else:
                    self.state = "no-answer"
                    self._compute_guard()
                    self.hub.publish("tun", self._snapshot())
            if self.pending and now - self.last_send >= SEND_MIN_S and self.sess.fd is not None:
                data = "".join(self.pending.values()).encode()
                self.pending.clear()
                self.last_send = now
                self.sess.send(data)
            # not while a `list` is coming in: half-synced values would overwrite the record before
            # the T END archive check sees it (a long list - the per-preset constants - made this race real)
            if self.changes_due and now - self.changes_at >= CHANGES_WRITE_S and self.state != "asking":
                self.changes_due = False
                self.changes_at = now
                self._write_changes()

    # ---- requests (HTTP threads) -------------------------------------------
    def request_set(self, name, value):
        """Returns (http code, message)."""
        with self.lock:
            c = self.by_name.get(name)
            if c is None:
                return 404, "unknown constant %s" % name
            if not self.writable():
                return 409, "changes are blocked: " + "; ".join(self.guard["reasons"] or [self.state])
            if self.fw_names is not None and name not in self.fw_names:
                return 409, "%s is not live in this firmware" % name
            ty = c["type"]
            if ty == "bool":
                if value in (True, False, 0, 1, "true", "false", "0", "1"):
                    v = value in (True, 1, "true", "1")
                else:
                    return 400, "bool expected"
                text = "1" if v else "0"
            else:
                try:
                    x = float(value)
                except (TypeError, ValueError):
                    return 400, "number expected"
                if not math.isfinite(x):
                    return 400, "not finite"
                if ty == "int":
                    if x != int(x):
                        return 400, "integer expected"
                    x = int(x)
                rng = c["range"]
                if rng and not (rng[0] - 1e-9 * abs(rng[0]) <= x <= rng[1] + 1e-9 * abs(rng[1])):
                    return 400, "outside the range %g .. %g" % (rng[0], rng[1])
                why = self._order_problem(name, x)
                if why:
                    return 400, why
                text = str(x) if ty == "int" else ("%.9g" % x)
            self.pending[name] = "set %s %s\n" % (name, text)
            self.pending.move_to_end(name)
            return 202, "queued"

    def _cur(self, name):
        """The value the pedal will have once queued sets land: queued `set` text, else the pedal's, else the default."""
        q = self.pending.get(name)
        if q is None and "*" in self.pending:
            q = "reset"
        if q:
            if q.startswith("reset"):
                return self.by_name[name]["default"]
            try:
                return float(q.split()[2])
            except (IndexError, ValueError):
                pass
        v = self.values.get(name)
        if v is None:
            v = self.by_name[name]["default"]
        return v

    def _order_problem(self, name, x):
        """@below ordering (registry): the code divides by the difference / log ratio of the two, so the lower one
        must stay strictly below the upper one. Returns the refusal text or ""."""
        c = self.by_name[name]
        hi = c.get("below")
        if hi and hi in self.by_name:
            v = self._cur(hi)
            if v is not None and not x < v:
                return "must stay below %s (%g): the code divides by their difference / ratio" % (self.by_name[hi]["label"], v)
        for o in self.by_name.values():
            if o.get("below") == name:
                v = self._cur(o["name"])
                if v is not None and not v < x:
                    return "must stay above %s (%g): the code divides by their difference / ratio" % (o["label"], v)
        return ""

    def request_reset(self, name):
        with self.lock:
            if self.state != "synced":
                return 409, "pedal not synced (%s)" % self.state
            if name == "all":
                self.pending.clear()
                self.pending["*"] = "reset all\n"
                return 202, "queued"
            if name not in self.by_name:
                return 404, "unknown constant %s" % name
            # (a reset is allowed even while `set` is blocked: it only returns to the firmware's default)
            self.pending[name] = "reset %s\n" % name
            self.pending.move_to_end(name)
            return 202, "queued"

    def set_note(self, text):
        with self.lock:
            self.note = str(text)[:4000]
            self.changes_due = True
        self.publish()

    def set_override(self, on):
        with self.lock:
            self.override = bool(on)
            self._compute_guard()
        self.publish()

    # ---- state out -------------------------------------------------------------
    def _changed(self, c):
        name = c["name"]
        if name not in self.values or c["default"] is None:
            return False
        v, d = self.values[name], c["default"]
        if c["type"] == "float":
            return f32(v) != f32(d)
        return v != d

    def _changes(self):
        out = []
        for c in self.reg["constants"]:
            if self._changed(c):
                out.append({"name": c["name"], "label": c["label"], "module": c["module"],
                            "section": c["section"], "file": c["file"], "line": c["line"],
                            "default_text": c["default_text"], "default": c["default"],
                            "value": self.values[c["name"]], "unit": c["unit"]})
        return out

    def _write_changes(self):
        try:
            os.makedirs(self.tune_dir, exist_ok=True)
        except OSError:
            return
        path = os.path.join(self.tune_dir, "changes.json")
        changes = self._changes()
        if self.resync_archive:
            # after a (re)connect the pedal is the truth; keep an older record it would drop
            self.resync_archive = False
            try:
                with open(path) as f:
                    old = json.load(f)
                old_ch = [(x.get("name"), x.get("value")) for x in old.get("changes", [])]
                if old_ch and old_ch != [(x["name"], x["value"]) for x in changes]:
                    hist = os.path.join(self.tune_dir, "history")
                    os.makedirs(hist, exist_ok=True)
                    shutil.copyfile(path, os.path.join(
                        hist, "changes-%s.json" % time.strftime("%Y%m%d-%H%M%S", time.localtime(self.clock()))))
            except (OSError, ValueError, AttributeError, TypeError):
                pass
        doc = {
            "about": "Live constant changes made with tools/devtool (docs/DEV_INTERFACE.md): every constant whose "
                     "value on the pedal differs from its compiled default. Apply them to the constants files "
                     "(file / line / name); devtool never edits source files.",
            "pedal": self.pedal, "firmware": self.firmware, "repo_head": self.repo_head,
            "updated": local_iso(self.clock()), "synced": self.state == "synced",
            "guard": {"ok": self.guard["ok"], "reasons": self.guard["reasons"], "override": self.override},
            "note": self.note, "changes": changes,
        }
        tmp = path + ".tmp"
        try:
            with open(tmp, "w") as f:
                json.dump(doc, f, indent=2)
                f.write("\n")
            os.replace(tmp, path)
        except OSError:
            pass

    def _snapshot(self):
        docs = {}
        consts = []
        for c in self.reg["constants"]:
            d = dict(c)
            if c["doc_of"]:
                docs[c["doc_of"]] = c["doc"]
            d.pop("doc", None)
            d["value"] = self.values.get(c["name"])
            d["in_firmware"] = None if self.fw_names is None else c["name"] in self.fw_names
            consts.append(d)
        return {"pedal": self.pedal, "firmware": self.firmware, "head": self.repo_head,
                "files": self.reg["files"], "problems": self.reg["problems"],
                "state": self.state, "guard": self.guard, "override": self.override,
                "writable": self.writable(), "note": self.note, "constants": consts, "docs": docs,
                "pairs": self.reg.get("pairs", {}), "layout": self.reg.get("layout"),
                "targets": self.reg.get("targets", []),
                "last_err": self.last_err}

    def snapshot(self):
        with self.lock:
            return self._snapshot()

    def publish(self):
        self.hub.publish("tun", self.snapshot())


# ---------------------------------------------------------------------------
# Flash: make -> dfu -> wait for the DFU device -> make program-dfu
# ---------------------------------------------------------------------------
def make_argv(args):
    return ["make"] + list(args)


def dfu_present():
    try:
        out = subprocess.run(["dfu-util", "-l"], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return False
    return ("[%s]" % DFU_ID) in out


class TermLines:
    """Splits a tool's output into lines the way a terminal shows them: '\n' ends a
    line, '\r\n' too, a lone '\r' ends a line that the NEXT text replaces (progress
    bars). emit(text, replace): replace = True -> the text replaces the last line."""

    def __init__(self, emit):
        self.emit = emit
        self.buf = []
        self.cr = False          # the last char was '\r' (a following '\n' just completes it)
        self.overwrite = False   # the next emitted line replaces the last one

    def feed(self, text):
        for ch in text:
            if self.cr:
                self.cr = False
                if ch == "\n":          # '\r\n': the line is already out and stays
                    self.overwrite = False
                    continue
            if ch == "\r":              # shown at once; the next text replaces it
                self.emit("".join(self.buf), self.overwrite)
                self.buf = []
                self.overwrite = True
                self.cr = True
            elif ch == "\n":
                self.emit("".join(self.buf), self.overwrite)
                self.buf = []
                self.overwrite = False
            else:
                self.buf.append(ch)

    def close(self):
        if self.buf:
            self.emit("".join(self.buf), self.overwrite)
            self.buf = []
        self.cr = self.overwrite = False


class Flasher:
    def __init__(self, hub, serial, argv=make_argv, dfu_check=dfu_present,
                 hint_s=DFU_HINT_S, max_s=DFU_MAX_S, poll_s=DFU_POLL_S, jobs=None):
        self.hub, self.serial = hub, serial
        self.argv, self.dfu_check = argv, dfu_check
        self.hint_s, self.max_s, self.poll_s = hint_s, max_s, poll_s
        self.jobs = jobs if jobs is not None else (os.cpu_count() or 4)
        self.lock = threading.Lock()
        self.active = False
        self.proc = None
        self.state = {"active": False, "state": "idle"}
        self.hub.publish("flash", self.state)

    def start(self, pedal):
        """Returns (ok, message). Refuses a second flash while one runs."""
        if pedal not in PEDALS:
            return False, "unknown pedal %r" % pedal
        with self.lock:
            if self.active:
                return False, "a flash is already running"
            self.active = True
        threading.Thread(target=self._run, args=(pedal,), name="flash", daemon=True).start()
        return True, "flash started"

    def _set(self, pedal, state, message, ok=None):
        st = {"active": state not in ("done", "failed"), "state": state, "pedal": pedal,
              "t": now_ms(), "message": message}
        if ok is not None:
            st["ok"] = ok
        self.state = st
        self.hub.publish("flash", st)

    def _out(self, text, stream, replace=False):
        self.hub.publish("flashout", {"t": now_ms(), "text": text, "stream": stream,
                                      "replace": replace})

    def _cmd(self, args, stream):
        """Run a command, stream its output. Returns (exit code, all output)."""
        self._out("$ " + " ".join(args), "info")
        lines = []

        def emit(text, replace):
            if replace and lines:
                lines[-1] = text
            else:
                lines.append(text)
            self._out(text, stream, replace)

        try:
            p = subprocess.Popen(args, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, bufsize=0)
        except OSError as e:
            self._out("cannot run %s: %s" % (args[0], e), "info")
            return -1, ""
        self.proc = p
        split = TermLines(emit)
        dec = codecs.getincrementaldecoder("utf-8")(errors="replace")
        fd = p.stdout.fileno()
        while True:
            chunk = os.read(fd, 4096)
            if not chunk:
                break
            split.feed(dec.decode(chunk))
        split.feed(dec.decode(b"", final=True))
        split.close()
        rc = p.wait()
        self.proc = None
        return rc, "\n".join(lines)

    def _run(self, pedal):
        args = PEDALS[pedal]
        try:
            self._set(pedal, "building", "building %s (DIAG)" % pedal)
            rc, _ = self._cmd(self.argv(args + ["-j%d" % self.jobs]), "make")
            if rc != 0:
                self._set(pedal, "failed", "build failed (make exit %d)" % rc, ok=False)
                return
            if self.dfu_check():
                self._out("DFU device already present", "info")
            else:
                st = self.serial.call(lambda: (self.serial.sess.fd is not None,
                                               self.serial.sess.build))
                connected, build = st
                sent = False
                if connected:
                    sent = self.serial.call(self.serial.sess.send, b"dfu\n")
                if sent:
                    who = build[0] if build else "the connected pedal (no BUILD answer yet)"
                    self._out("sent `dfu` to %s" % who, "info")
                    self._set(pedal, "entering-dfu", "asked the pedal to enter the bootloader")
                    hint_at = time.time() + self.hint_s
                else:
                    self._out("no pedal on the serial port: footswitch gesture needed", "info")
                    hint_at = time.time()
                end = time.time() + self.max_s
                hinted = False
                while not self.dfu_check():
                    now = time.time()
                    if now >= end:
                        self._set(pedal, "failed", "no DFU device after %d s" % self.max_s, ok=False)
                        return
                    if not hinted and now >= hint_at:
                        hinted = True
                        self._set(pedal, "gesture",
                                  "hold both footswitches to enter the bootloader (waiting for DFU)")
                    time.sleep(self.poll_s)
            self._out("DFU device found", "info")
            self._set(pedal, "programming", "programming %s" % pedal)
            rc, out = self._cmd(self.argv(args + ["program-dfu"]), "dfu")
            if rc != 0 and "File downloaded successfully" in out:
                self._out("dfu-util exited %d after a complete download (usual with :leave)" % rc,
                          "info")
                rc = 0
            if rc != 0:
                self._set(pedal, "failed", "programming failed (exit %d)" % rc, ok=False)
                return
            self._set(pedal, "done", "flashed %s - the pedal reconnects by itself" % pedal, ok=True)
        except Exception as e:  # report, never kill the server
            self._set(pedal, "failed", "devtool error: %s" % e, ok=False)
        finally:
            with self.lock:
                self.active = False

    def stop(self):
        p = self.proc
        if p is not None and p.poll() is None:
            try:
                p.terminate()
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Wavetables (preset editor): tools/hostsim/tbdump = the exact tables the pedal builds (explorer.h Init ->
# BuildBank), as JSON. Built with `make -C tools/hostsim tbdump` when missing or older than explorer*.h; the
# JSON is cached per (repo HEAD, explorer*.h dirty state + contents). Failure = {"ok": false, "reason": ...}.
# ---------------------------------------------------------------------------
HOSTSIM_DIR = os.path.join(REPO, "tools", "hostsim")
TBDUMP = os.path.join(HOSTSIM_DIR, "tbdump")
EXPLORER_GLOB = os.path.join(REPO, "pedals", "nitrolab", "modules", "explorer*.h")


def _tail(text, n=6):
    lines = [l for l in (text or "").strip().splitlines() if l.strip()]
    return " | ".join(lines[-n:])


class Tables:
    def __init__(self, head=diaglog.git_head, dirty=files_dirty):
        self.head = head
        self.dirty = dirty
        self.lock = threading.Lock()
        self.key = None
        self.result = None

    def _sources(self):
        return sorted(glob.glob(EXPLORER_GLOB))

    def _key(self, srcs):
        rel = [os.path.relpath(p, REPO) for p in srcs]
        d = self.dirty(rel)
        key = [self.head() or "?", "dirty" if d else ("clean" if d is False else "unknown")]
        if d is not False:                          # dirty (or git failed): the contents decide
            hs = hashlib.sha1()
            for p in srcs:
                try:
                    with open(p, "rb") as f:
                        hs.update(f.read())
                except OSError:
                    hs.update(b"?")
            key.append(hs.hexdigest()[:12])
        return ":".join(key)

    def _build(self, srcs):
        try:
            newest = max(os.path.getmtime(p) for p in srcs) if srcs else 0.0
            stale = not os.path.exists(TBDUMP) or os.path.getmtime(TBDUMP) < newest
        except OSError as e:
            return "explorer sources: %s" % e
        if not stale:
            return None
        try:
            r = subprocess.run(["make", "-C", HOSTSIM_DIR, "tbdump"], capture_output=True, text=True, timeout=600)
        except (OSError, subprocess.SubprocessError) as e:
            return "make -C tools/hostsim tbdump: %s" % e
        if r.returncode != 0:
            return "make -C tools/hostsim tbdump failed (exit %d): %s" % (r.returncode, _tail(r.stdout + r.stderr))
        return None

    def _run(self, srcs):
        if not srcs:
            return {"ok": False, "reason": "no pedals/nitrolab/modules/explorer*.h found"}
        err = self._build(srcs)
        if err:
            return {"ok": False, "reason": err}
        try:
            r = subprocess.run([TBDUMP], capture_output=True, text=True, timeout=120)
        except (OSError, subprocess.SubprocessError) as e:
            return {"ok": False, "reason": "tbdump: %s" % e}
        if r.returncode != 0:
            return {"ok": False, "reason": "tbdump failed (exit %d): %s" % (r.returncode, _tail(r.stderr))}
        try:
            data = json.loads(r.stdout)
        except ValueError as e:
            return {"ok": False, "reason": "tbdump output is not JSON: %s" % e}
        if not isinstance(data, dict) or not isinstance(data.get("presets"), list):
            return {"ok": False, "reason": "tbdump output has no presets list"}
        return {"ok": True, "data": data}

    def get(self):
        """The cached result for the current key; rebuilt when the key changed (blocking, one at a time)."""
        with self.lock:
            try:
                srcs = self._sources()
                key = self._key(srcs)
                if key != self.key or self.result is None:
                    t0 = time.time()
                    res = self._run(srcs)
                    res["key"] = key
                    res["ms"] = int((time.time() - t0) * 1000)
                    self.key, self.result = key, res
                return self.result
            except Exception as e:                  # never crash the server over the tables
                return {"ok": False, "reason": "tables: %s" % e, "key": None}

    def prefetch(self):
        threading.Thread(target=self.get, name="tables", daemon=True).start()


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    server_version = "devtool/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass  # quiet; the page is the UI

    def _host_ok(self):
        host = (self.headers.get("Host") or "").strip()
        if host.startswith("["):
            name = host.split("]")[0] + "]"
        else:
            name = host.split(":")[0]
        if name.lower() in LOCAL_HOSTS:
            return True
        self._send(403, b"forbidden host\n", "text/plain")
        return False

    def _send(self, code, body, ctype, extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code, obj):
        self._send(code, (json.dumps(obj) + "\n").encode(), "application/json")

    def do_GET(self):
        if not self._host_ok():
            return
        path = self.path.split("?")[0]
        app = self.server.app
        if path in STATIC:
            name, ctype = STATIC[path]
            try:
                with open(os.path.join(WEB_DIR, name), "rb") as f:
                    body = f.read()
            except OSError:
                self._send(404, b"missing\n", "text/plain")
                return
            self._send(200, body, ctype)
        elif path == "/api/status":
            st = app.serial.call(app.sess.status)
            st.update({"flash": app.flasher.state, "pedals": list(PEDALS),
                       "default_pedal": DEFAULT_PEDAL, "head": diaglog.git_head()})
            self._json(200, st)
        elif path == "/api/registry":
            self._json(200, app.tuner.snapshot())
        elif path == "/api/tables":
            self._json(200, app.tables.get())
        elif path == "/events":
            self._events()
        else:
            self._send(404, b"not found\n", "text/plain")

    def do_POST(self):
        if not self._host_ok():
            return
        path = self.path.split("?")[0]
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if 0 < n <= 65536 else b""
        if path not in ("/api/flash", "/api/set", "/api/reset", "/api/note", "/api/override"):
            self._send(404, b"not found\n", "text/plain")
            return
        # application/json only: a cross-site form post cannot send it without a
        # CORS preflight, which this server never grants.
        if (self.headers.get("Content-Type") or "").split(";")[0].strip() != "application/json":
            self._json(415, {"ok": False, "message": "send application/json"})
            return
        try:
            req = json.loads(body.decode() or "{}")
            if not isinstance(req, dict):
                raise ValueError
        except (ValueError, UnicodeDecodeError):
            self._json(400, {"ok": False, "message": "bad JSON"})
            return
        tuner = self.server.app.tuner
        if path == "/api/set":
            code, msg = tuner.request_set(req.get("name"), req.get("value"))
            self._json(code, {"ok": code < 300, "message": msg})
            return
        if path == "/api/reset":
            code, msg = tuner.request_reset(req.get("name"))
            self._json(code, {"ok": code < 300, "message": msg})
            return
        if path == "/api/note":
            tuner.set_note(req.get("text") or "")
            self._json(200, {"ok": True, "message": "saved"})
            return
        if path == "/api/override":
            tuner.set_override(bool(req.get("on")))
            self._json(200, {"ok": True, "message": "override %s" % ("on" if req.get("on") else "off")})
            return
        pedal = req.get("pedal")
        if pedal not in PEDALS:
            self._json(400, {"ok": False, "message": "unknown pedal %r" % (pedal,)})
            return
        ok, msg = self.server.app.flasher.start(pedal)
        self._json(202 if ok else 409, {"ok": ok, "message": msg})

    def _events(self):
        app = self.server.app
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        q, snap = app.hub.subscribe()

        def emit(kind, data):
            self.wfile.write(("event: %s\ndata: %s\n\n" % (kind, json.dumps(data))).encode())

        try:
            self.wfile.write(b"retry: 1000\n\n")
            for kind, data in snap:
                emit(kind, data)
            self.wfile.flush()
            while not app.stopping.is_set():
                try:
                    item = q.get(timeout=KEEPALIVE_S)
                except queue.Empty:
                    self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
                    continue
                if item is None:
                    break
                emit(*item)
                # batch whatever else is queued before flushing
                while True:
                    try:
                        item = q.get_nowait()
                    except queue.Empty:
                        break
                    if item is None:
                        return
                    emit(*item)
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            app.hub.unsubscribe(q)


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


DEFAULT_PORT = 8765         # http://localhost:8765; the next free one (8766, ...) only when it is taken


def bind_server(port=None):
    """`port` given -> exactly that port (0 = any, tests). Else DEFAULT_PORT, counting up while taken."""
    tries = [port] if port is not None else list(range(DEFAULT_PORT, DEFAULT_PORT + 20))
    err = None
    for p in tries:
        try:
            return Server(("127.0.0.1", p), Handler)
        except OSError as e:
            err = e
    raise err


class App:
    def __init__(self, sess_kw=None, flasher_kw=None, tuner_kw=None, port=None, remember_port=True,
                 tables=None):
        self.hub = Hub()
        self.sess = DevSession(self.hub, **(sess_kw or {}))
        self.tuner = Tuner(self.hub, self.sess, **(tuner_kw or {}))
        self.sess.tuner = self.tuner
        self.hub.tun_provider = self.tuner.snapshot
        self.serial = SerialLoop(self.sess, tick=self.tuner.tick)
        self.flasher = Flasher(self.hub, self.serial, **(flasher_kw or {}))
        self.prefetch_tables = tables is None       # the real tool builds them at startup (background)
        self.tables = tables if tables is not None else Tables()
        self.stopping = threading.Event()
        self.httpd = bind_server(port)
        self.httpd.app = self
        self.port = self.httpd.server_address[1]
        if remember_port:
            try:
                os.makedirs(os.path.dirname(PORT_FILE), exist_ok=True)
                with open(PORT_FILE, "w") as f:
                    f.write("%d\n" % self.port)
            except OSError:
                pass
        self.http_thread = threading.Thread(target=self.httpd.serve_forever, name="http",
                                            kwargs={"poll_interval": 0.2}, daemon=True)

    def start(self):
        self.serial.start()
        self.http_thread.start()
        if self.prefetch_tables:
            self.tables.prefetch()

    def close(self):
        self.stopping.set()
        self.flasher.stop()
        with self.hub.lock:
            for q in list(self.hub.clients):
                try:
                    q.put_nowait(None)
                except queue.Full:
                    pass
        self.httpd.shutdown()
        self.httpd.server_close()
        self.serial.close()


# ---------------------------------------------------------------------------
# Self-test (offline: pty pair stands in for the pedal; fake make / dfu-util)
# ---------------------------------------------------------------------------
def selftest():
    import shutil
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("PASS " if cond else "FAIL ") + what)
        ok = ok and bool(cond)

    tmp = tempfile.mkdtemp()
    tune_dir = os.path.join(tmp, "tune")
    dev = {"path": None}
    pedal = {"got": b"", "dfu": False, "rx": b"", "sets": []}
    calls = []
    head = {"h": "abc1234"}

    # The fake pedal's live constants: the real nitrolab registry, compiled defaults.
    reg = reglib.build(REPO, "nitrolab", CONST_SOURCES["nitrolab"])
    tys = {"float": "f", "int": "i", "bool": "b"}

    def ptext(c, v):
        if c["type"] == "bool":
            return "1" if v else "0"
        if c["type"] == "int":
            return str(int(v))
        x = f32(v)
        for prec in range(1, 10):
            t = "%.*g" % (prec, x)
            if f32(float(t)) == x:
                return t
        return repr(x)

    def fresh_values():
        return {c["name"]: c["default"] for c in reg["constants"]}

    pedal["vals"] = fresh_values()
    byname = {c["name"]: c for c in reg["constants"]}

    def tline(name):
        c = byname[name]
        return ("T %s %s %s\r\n" % (name, tys[c["type"]], ptext(c, pedal["vals"][name]))).encode()

    def pedal_cmd(m, line):
        parts = line.split()
        if not parts:
            return
        if parts[0] == "list":
            os.write(m, b"".join(tline(c["name"]) for c in reg["constants"])
                     + b"T END %d\r\n" % len(reg["constants"]))
        elif parts[0] == "set" and len(parts) == 3 and parts[1] in byname:
            c = byname[parts[1]]
            pedal["sets"].append((parts[1], parts[2]))
            v = parts[2]
            pedal["vals"][parts[1]] = (v == "1") if c["type"] == "bool" else (
                int(v) if c["type"] == "int" else float(v))
            os.write(m, tline(parts[1]))
        elif parts[0] == "reset" and len(parts) == 2:
            names = list(byname) if parts[1] == "all" else [parts[1]]
            for n in names:
                pedal["vals"][n] = byname[n]["default"]
            os.write(m, b"".join(tline(n) for n in names))
        elif parts[0] in ("set", "reset", "get"):
            os.write(m, b"T ERR unknown name %s\r\n" % parts[1].encode())

    def fake_argv(args):
        calls.append(list(args))
        code = ("import sys; print('fake make', *sys.argv[1:]);"
                "sys.stdout.write('Download [=  ] 10%\\rDownload [== ] 50%\\rDownload [===] 100%\\r\\n"
                "File downloaded successfully\\n') if 'program-dfu' in sys.argv else None")
        return [sys.executable, "-c", code] + list(args)

    # 0) wavetables: the real tbdump (built if missing / stale), JSON, 15 presets, frame counts = EXP_TB_PRESETS
    tables = Tables()
    tres = tables.get()
    check(tres.get("ok") is True, "tables: tbdump builds + runs (%s)" % (tres.get("reason") or "%d ms" % tres.get("ms", 0)))
    tdata = tres.get("data") or {}
    tpres = tdata.get("presets") or []
    check(len(tpres) == 15, "tables: 15 presets")
    with open(os.path.join(REPO, "pedals", "nitrolab", "modules", "explorer_tables.h")) as fh:
        rows = re.findall(r'^\s*\{\s*"(\w+)",\s*kTb\w+,\s*(\d+),', fh.read(), re.M)
    check(len(rows) == 15 and [(p["name"], p["frames"]) for p in tpres] == [(n, int(f)) for n, f in rows]
          and all(len(p["waves"]) == p["frames"] and all(len(w) == tdata.get("size") for w in p["waves"])
                  for p in tpres), "tables: names / frame counts match EXP_TB_PRESETS, waves complete")
    check(tables.get() is tres, "tables: cached for the same HEAD / explorer*.h state")

    class NoSrcTables(Tables):
        def _sources(self):
            return []
    bad = NoSrcTables().get()
    check(bad.get("ok") is False and bad.get("reason"), "tables: failure -> ok false + reason")

    app = App(sess_kw={"log_dir": tmp, "finder": lambda: dev["path"], "head": lambda: head["h"],
                       "query_timeout": 0.3},
              flasher_kw={"argv": fake_argv, "dfu_check": lambda: pedal["dfu"],
                          "hint_s": 0.4, "max_s": 5.0, "poll_s": 0.05, "jobs": 2},
              tuner_kw={"head": lambda: head["h"], "dirty": lambda files: False, "tune_dir": tune_dir,
                        "list_timeout": 0.5},
              port=0, remember_port=False, tables=tables)
    app.start()
    port = app.port

    events = []
    ev_lock = threading.Lock()

    def sse_reader(stop):
        c = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        c.request("GET", "/events")
        r = c.getresponse()
        events.append(("http", r.status))
        kind = None
        while not stop.is_set():
            try:
                line = r.fp.readline()
            except (socket.timeout, OSError):
                continue
            if not line:
                break
            line = line.decode().rstrip("\n")
            if line.startswith("event: "):
                kind = line[7:]
            elif line.startswith("data: ") and kind:
                with ev_lock:
                    events.append((kind, json.loads(line[6:])))
        c.close()

    def has(kind, pred=lambda d: True, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            with ev_lock:
                for k, d in events:
                    if k == kind and pred(d):
                        return True
            time.sleep(0.02)
        return False

    def pedal_pump(m, stop):
        while not stop.is_set():
            try:
                r, _, _ = select.select([m], [], [], 0.02)
            except (OSError, ValueError):
                return
            if not r:
                continue
            try:
                got = os.read(m, 100)
            except OSError:
                return
            pedal["got"] += got
            if b"?" in got:
                os.write(m, b"BUILD nitrolab abc1234-dirty DIAG 2026-11-06T10:00\r\n")
            pedal["rx"] += got
            while b"\n" in pedal["rx"]:
                line, pedal["rx"] = pedal["rx"].split(b"\n", 1)
                pedal_cmd(m, line.decode())
            if b"dfu\n" in pedal["got"] and not pedal["dfu"]:
                os.write(m, b"DFU entering bootloader\r\n")
                time.sleep(0.05)
                pedal["dfu"] = True

    def req(method, path, body=None, headers=None):
        c = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        c.request(method, path, body=body, headers=headers or {})
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, data

    sse_stop = threading.Event()
    threading.Thread(target=sse_reader, args=(sse_stop,), daemon=True).start()
    check(has("hello"), "SSE: hello on connect")
    check(has("status", lambda d: d.get("connected") is False), "SSE: status waiting for pedal")

    # 1) pedal connects, answers BUILD
    m1, s1 = os.openpty()
    dev["path"] = os.ttyname(s1)
    pstop = threading.Event()
    threading.Thread(target=pedal_pump, args=(m1, pstop), daemon=True).start()
    check(has("status", lambda d: d.get("build_state") == "ok"), "connect + `?` -> BUILD parsed")
    st, body = req("GET", "/api/tables")
    check(st == 200 and json.loads(body).get("key") == tres.get("key"), "/api/tables: served (cached)")
    st, body = req("GET", "/api/status")
    js = json.loads(body)
    check(st == 200 and js["build"] == {"pedal": "nitrolab", "hash": "abc1234", "dirty": True,
                                        "time": "2026-11-06T10:00"}, "/api/status: build info")
    check(b"?\n" in pedal["got"], "devtool sent `?`")
    os.write(m1, b"NL HB t=100 cpu=420 mode=1\r\n")
    check(has("line", lambda d: d["text"] == "NL HB t=100 cpu=420 mode=1" and d["t"] > 0),
          "SSE: log line delivered (CR stripped, epoch-ms stamp)")

    # 2) HTTP basics
    st, body = req("GET", "/")
    check(st == 200 and b"<html" in body.lower(), "GET / serves the page")
    got = {pth: req("GET", pth) for pth in ("/app.css", "/core.js", "/app.js", "/icons/icons.svg")}
    check(all(v[0] == 200 and len(v[1]) > 100 for v in got.values()) and b'id="ic-ccw"' in got["/icons/icons.svg"][1],
          "GET page assets (app.css, core.js, app.js, icons sprite)")
    st, _ = req("GET", "/api/status", headers={"Host": "evil.example:80"})
    check(st == 403, "foreign Host header refused")
    st, _ = req("POST", "/api/flash", body=b"pedal=nitrolab",
                headers={"Content-Type": "application/x-www-form-urlencoded"})
    check(st == 415, "flash: non-JSON post refused")
    st, _ = req("POST", "/api/flash", body=b'{"pedal":"nope"}',
                headers={"Content-Type": "application/json"})
    check(st == 400, "flash: unknown pedal refused")

    # 2b) live constants: list -> synced, set / reset / note / guard, changes file
    J = {"Content-Type": "application/json"}

    def post(path, obj):
        st, body = req("POST", path, body=json.dumps(obj).encode(), headers=J)
        return st, json.loads(body or b"{}")

    def changes_file():
        try:
            with open(os.path.join(tune_dir, "changes.json")) as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def wait_for(fn, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            if fn():
                return True
            time.sleep(0.03)
        return False

    check(reg["constants"] and not reg["problems"], "registry: TUNABLE constants parsed, no tag problems (%d)"
          % len(reg["constants"]))
    want_pairs = {"Pluck_decay_time": ["EXP_PS_DEC_FAST_S", "EXP_PS_DEC_SLOW_S"],
                  "Swell_rise_time": ["EXP_PS_RISE_FAST_S", "EXP_PS_RISE_SLOW_S"],
                  "LFO_rate": ["EXP_LFO_HZ_MAX_CCW", "EXP_LFO_HZ_MIN", "EXP_LFO_HZ_MAX"],
                  "Live_level_range": ["EXP_SPEC_DYN_FLOOR_DB", "EXP_SPEC_DYN_TOP_DB"],
                  "Density_travel_by_env_time": ["EXP_SPEC_ENV_DEPTH_MIN", "EXP_SPEC_ENV_DEPTH_MAX"]}
    fam = [c for c in reg["constants"] if c.get("preset")]
    presets = sorted({(c["preset_index"], c["preset"]) for c in fam})
    for _i, pn in presets:      # per-preset LFO rate pairs (explorer_tables.h @family)
        want_pairs["%s_LFO_rate" % pn] = ["EXP_TB_%s_RATE_CCW" % pn, "EXP_TB_%s_RATE_CW" % pn]
    byn = {c["name"]: c for c in reg["constants"]}
    rows = {}                   # preset -> line of its EXP_TB_PRESETS row (the family constants' file line)
    with open(os.path.join(REPO, "pedals/nitrolab/modules/explorer_tables.h"), encoding="utf-8") as f:
        for n, ln in enumerate(f, 1):
            mrow = re.match(r'^\s*\{\s*"(\w+)"\s*,\s*kTb', ln)
            if mrow:
                rows[mrow.group(1)] = n
    check(len(presets) == 15 and len(fam) == 15 * 13
          and byn["EXP_TB_SAW_FILTER"]["default"] == 2.6 and byn["EXP_TB_PWM_TIMBRE"]["default"] == 0.15
          and all(byn["EXP_TB_%s_%s_ENV" % (pn, t)]["default"] is ((pn, t) == ("BRASS", "TIMBRE")) and byn["EXP_TB_%s_%s_ENV" % (pn, t)]["target"] == t.lower()
                  for _i, pn in presets for t in ("TIMBRE", "FILTER", "PITCH"))
          and "EXP_TB_SAW_ENV" not in byn and byn["EXP_TB_AIR_PITCH"]["default"] == 15.0
          and all(byn["EXP_TB_%s_LOFI" % pn]["default"] is (pn in ("SAW", "PULSE", "VOCAL", "AIR")) for _i, pn in presets)
          and all(byn["EXP_TB_%s_STEPPED" % pn]["default"] is (pn == "LOFI") for _i, pn in presets)
          and byn["EXP_TB_SAW_STEPPED"]["states"] == ["smooth", "stepped"] and byn["EXP_TB_SAW_LOFI"]["target"] == "lofi",
          "registry: @family per-preset tunables (15 presets x 13 fields, defaults from the EXP_TB_MOD rows; Lofi on SAW / PULSE / VOCAL / AIR, Stepped on LOFI only; per-target sources LFO except BRASS timbre = env)")
    check(byn["EXP_TB_VOCAL_RES"]["default"] == 0.7 and byn["EXP_TB_SAW_RES"]["default"] == 1.5
          and all(byn["EXP_TB_%s_RES" % pn]["unit"] == "Q"
              and byn["EXP_TB_%s_RES" % pn]["taper"] == "log" and byn["EXP_TB_%s_RES" % pn]["target"] == "filter"
              and (byn["EXP_TB_%s_RES" % pn]["inert"] == "K2 sets Q on ANALOG") == (pn in ("SAW", "PULSE"))
              for _i, pn in presets)
          and all(c["inert"] is None for c in fam if not c["name"].endswith("_RES")),
          "registry: RES per preset (from the rows, Q, log, filter), @inert on the ANALOG presets only")
    check(len(rows) == 15 and all(c["line"] == rows[c["preset"]] for c in fam),
          "registry: family constants' line = the preset's EXP_TB_PRESETS row")
    check(all(c["why"] for c in reg["constants"] if c["range"])
          and byn["EXP_PS_DEC_FAST_S"]["below"] == "EXP_PS_DEC_SLOW_S" and byn["EXP_SPEC_DYN_FLOOR_DB"]["below"] == "EXP_SPEC_DYN_TOP_DB"
          and "Nyquist" in byn["EXP_LFO_HZ_MAX"]["why"] and byn["EXP_LFO_HZ_MAX"]["note"].startswith("Hz;"),
          "registry: every @range has its @why (free text, note kept), @below ordering parsed")
    check(reg["layout"] and [p["key"] for p in byn["EXP_PS_SLIDE_MS"]["places"]] ==
          ["2/fm/Touch env \u00b7 K4", "2/wt/Touch env \u00b7 K4", "3/2op/Touch env \u00b7 K4",
           "3/spectral/Touch env \u00b7 K4", "3/additive/Touch env \u00b7 K4"]
          and [p["key"] for p in byn["EXP_TB_PWM_TIMBRE"]["places"]] == ["2/wt/Presets"]
          and all(c["places"] for c in reg["constants"]),
          "registry: places from @at / @section (bench / mode / section), every nitrolab constant placed")
    check({k: v["members"] for k, v in reg["pairs"].items()} == want_pairs
          and reg["pairs"]["LFO_rate"]["kind"] == "knob" and reg["pairs"]["Live_level_range"]["kind"] == "range",
          "registry: @pair rows (members in end order, knob / range kind)")
    bad_dir = os.path.join(tmp, "badpair")
    os.makedirs(bad_dir)
    with open(os.path.join(bad_dir, "x_constants.h"), "w") as fh:
        fh.write("// --- S ---\n"
                 "TUNABLE(float, A1, 1.f);  // @unit s @range 0 2 @taper lin @level live @pair P ccw  a\n"
                 "TUNABLE(float, A2, 1.f);  // @unit Hz @range 0 2 @taper lin @level live @pair P cw  b\n"
                 "TUNABLE(float, A3, 1.f);  // @unit s @range 0 2 @taper lin @level live @pair Q sideways  c\n")
    br = reglib.build(tmp, "x", ["badpair"])
    check(br["pairs"] == {} and any("@pair P" in x for x in br["problems"])
          and any("A3: bad @pair" in x for x in br["problems"]) and all(c["pair"] is None for c in br["constants"]),
          "registry: a bad @pair is reported, its members fall back to single rows")
    check(has("tun", lambda d: d.get("state") == "synced" and d.get("writable") is True),
          "connect: `list` sent, synced, writable (guard ok)")
    check(b"list\n" in pedal["got"], "devtool sent `list` after BUILD")
    st, body = req("GET", "/api/registry")
    js = json.loads(body)
    c0 = js["constants"][0]
    check(js.get("pairs", {}).get("LFO_rate", {}).get("members") == ["EXP_LFO_HZ_MAX_CCW", "EXP_LFO_HZ_MIN", "EXP_LFO_HZ_MAX"],
          "/api/registry: pairs included")
    check((js.get("layout") or {}).get("title") == "NitroLab" and len(js["layout"]["benches"]) == 3,
          "/api/registry: layout (benches / modes) included")
    tids = [t["id"] for t in js.get("targets", [])]
    check(tids and all(k["target"] in tids + [""] for b in js["layout"]["benches"] for m in b["modes"]
                       for k in m.get("knobs", []))
          and all(c["target"] in tids + [""] for c in js["constants"])
          and all(next(c for c in js["constants"] if c["name"] == "EXP_TB_SAW_%s_ENV" % t)["states"] == ["LFO", "env"]
                  for t in ("TIMBRE", "FILTER", "PITCH")),
          "/api/registry: targets, knob strips, per-constant @target / @states")
    check(st == 200 and all(c["value"] == c["default"] for c in js["constants"])
          and {"label", "unit", "range", "taper", "level", "file", "line", "section", "module"} <= set(c0),
          "/api/registry: values merged (= defaults), metadata fields")
    check("firmware was built from a dirty tree" in js["guard"]["warnings"], "guard: dirty firmware is a warning")
    check(not has("line", lambda d: d["text"].startswith("T EXP_"), timeout=0.2),
          "`T` value lines kept off the page log")
    name = "EXP_LFO_HZ_MAX"
    st, r = post("/api/set", {"name": name, "value": 35})
    check(st == 202, "set: accepted")
    check(has("tval", lambda d: d["name"] == name and d["value"] == 35), "set: pedal echo -> tval event")
    check(wait_for(lambda: (changes_file() or {}).get("changes") and changes_file()["changes"][0]["name"] == name),
          "changes.json written")
    ch = changes_file()["changes"][0]
    check(ch["value"] == 35 and ch["default_text"] == "20.f" and ch["unit"] == "Hz"
          and ch["file"].endswith("explorer_constants.h") and ch["line"] > 0, "changes.json: name/file/line/default/value/unit")
    n0 = len(pedal["sets"])
    for i in range(60):
        post("/api/set", {"name": "EXP_LFO_HZ_MIN", "value": round(0.1 + i * 0.01, 2)})
    check(wait_for(lambda: pedal["vals"]["EXP_LFO_HZ_MIN"] == 0.69), "throttle: latest value wins")
    sent = len(pedal["sets"]) - n0
    check(sent < 45, "throttle: %d sends for 60 requests" % sent)
    st, _ = post("/api/set", {"name": name, "value": 5000})
    check(st == 400, "set: outside @range refused")
    st, r = post("/api/set", {"name": "EXP_PS_DEC_FAST_S", "value": 9})
    check(st == 400 and "below" in r.get("message", ""), "set: @below ordering refused (FAST >= SLOW)")
    st, r = post("/api/set", {"name": "EXP_SPEC_DYN_TOP_DB", "value": -70})
    check(st == 400 and "above" in r.get("message", ""), "set: @below ordering refused from the upper side (TOP <= FLOOR)")
    st, _ = post("/api/set", {"name": "NOPE", "value": 1})
    check(st == 404, "set: unknown constant refused")
    st, _ = post("/api/set", {"name": "EXP_TB_LOFI_ENABLE", "value": False})
    check(st == 202 and has("tval", lambda d: d["name"] == "EXP_TB_LOFI_ENABLE" and d["value"] is False),
          "set: bool")
    st, _ = post("/api/set", {"name": "EXP_TB_LOFI_TABLE_BITS", "value": 4.5})
    check(st == 202, "set: fractional bits accepted (float)")
    st, _ = post("/api/set", {"name": "EXP_TB_LOFI_SUM_RATE_HZ", "value": 0.001})
    check(st == 400, "set: below @range refused")
    post("/api/note", {"text": "faster LFO sounds better"})
    check(wait_for(lambda: (changes_file() or {}).get("note") == "faster LFO sounds better"), "note -> changes.json")
    st, _ = post("/api/reset", {"name": "EXP_TB_LOFI_ENABLE"})
    check(st == 202 and has("tval", lambda d: d["name"] == "EXP_TB_LOFI_ENABLE" and d["value"] is True),
          "reset one -> default echoed")
    # guard: firmware hash != HEAD blocks set until the override
    head["h"] = "fff0000"
    with app.tuner.lock:
        app.tuner._compute_guard()
    st, _ = post("/api/set", {"name": name, "value": 30})
    check(st == 409, "guard: firmware != HEAD blocks set")
    post("/api/override", {"on": True})
    st, _ = post("/api/set", {"name": name, "value": 30})
    check(st == 202, "guard: override for this session allows set")
    post("/api/override", {"on": False})
    head["h"] = "abc1234"
    with app.tuner.lock:
        app.tuner._compute_guard()
    check(has("tval", lambda d: d["name"] == name and d["value"] == 30), "set after override echoed")

    st, _ = req("POST", "/api/flash", body=b'{"pedal":"nitrolab"}',
                headers={"Content-Type": "application/json"})
    st2, _ = req("POST", "/api/flash", body=b'{"pedal":"nitrolab"}',
                 headers={"Content-Type": "application/json"})
    check(st == 202 and st2 == 409, "flash: started, second request refused")
    check(has("flash", lambda d: d.get("state") == "done" and d.get("ok") is True, timeout=6),
          "flash: finished ok")
    check(b"dfu\n" in pedal["got"], "flash: `dfu` sent to the pedal")
    check(has("line", lambda d: d["text"] == "DFU entering bootloader"), "pedal's DFU line logged")
    check(len(calls) == 2 and calls[0][:2] == ["PEDAL=nitrolab", "DIAG=1"]
          and calls[1] == ["PEDAL=nitrolab", "DIAG=1", "program-dfu"], "flash: make args per call")
    check(has("flashout", lambda d: d["text"].startswith("fake make PEDAL=nitrolab DIAG=1 -j2")),
          "SSE: make output streamed")
    check(not has("flash", lambda d: d.get("state") == "gesture", timeout=0.1),
          "flash: no gesture prompt when `dfu` worked")
    with ev_lock:
        fo = [d for k, d in events if k == "flashout" and d["text"].startswith("Download")]
    check([(d["text"][-4:], d["replace"]) for d in fo] == [(" 10%", False), (" 50%", True), ("100%", True)],
          "flash output: '\\r' progress replaces the line, '\\r\\n' ends it")
    check(has("flashout", lambda d: d["text"] == "File downloaded successfully" and d["replace"] is False),
          "flash output: next line appended after the progress line")
    with app.hub.lock:
        bl = [d["text"] for k, d in app.hub.flash_out if d["text"].startswith("Download")]
    check(len(bl) == 1 and bl[0].endswith("100%"), "flash backlog keeps only the last progress state")

    # 4) pedal gone
    pstop.set()
    dev["path"] = None
    os.close(m1)
    os.close(s1)
    check(has("sep", lambda d: d["what"] == "gone"), "SSE: pedal gone separator")
    check(has("status", lambda d: d.get("connected") is False and d.get("build_state") == "waiting"),
          "status: disconnected")
    check(has("tun", lambda d: d.get("state") == "offline" and d.get("writable") is False),
          "live constants: offline, not writable")
    st, _ = post("/api/set", {"name": name, "value": 31})
    check(st == 409, "set refused while offline")

    # 4b) the pedal comes back flashed (all defaults): re-sync from `list`, old record archived
    pedal["vals"] = fresh_values()
    with ev_lock:
        events.clear()
    m2, s2 = os.openpty()
    dev["path"] = os.ttyname(s2)
    pstop2 = threading.Event()
    threading.Thread(target=pedal_pump, args=(m2, pstop2), daemon=True).start()
    check(has("tun", lambda d: d.get("state") == "synced"
              and all(c["value"] == c["default"] for c in d["constants"])), "reconnect: re-synced to defaults")
    check(wait_for(lambda: (changes_file() or {}).get("changes") == []), "reconnect: changes.json now empty")
    hist = os.path.join(tune_dir, "history")
    check(os.path.isdir(hist) and len(os.listdir(hist)) == 1, "reconnect: previous changes kept in tune/history/")
    st, _ = post("/api/reset", {"name": "all"})
    check(st == 202, "reset all accepted")
    pstop2.set()
    dev["path"] = None
    os.close(m2)
    os.close(s2)
    check(has("status", lambda d: d.get("connected") is False), "fresh pedal gone again")

    # 5) flash without a pedal -> gesture prompt, then DFU appears
    pedal["dfu"] = False
    calls.clear()
    req("POST", "/api/flash", body=b'{"pedal":"nitrotron3m"}',
        headers={"Content-Type": "application/json"})
    check(has("flash", lambda d: d.get("state") == "gesture" and d.get("pedal") == "nitrotron3m"),
          "flash without pedal: gesture prompt")
    pedal["dfu"] = True
    check(has("flash", lambda d: d.get("state") == "done" and d.get("pedal") == "nitrotron3m"),
          "flash without pedal: done after DFU appears")
    check(calls and calls[0][:4] == ["PEDAL=nitrotron3m", "NT3M_SLOT_B=sprawl", "INSTRUMENT=bass",
                                     "DIAG=1"], "nitrotron3m: INSTRUMENT + NT3M_SLOT_B on make")

    # 6) a reconnecting page gets the backlog
    events2 = list(events)
    with ev_lock:
        events.clear()
    sse_stop.set()
    sse_stop2 = threading.Event()
    threading.Thread(target=sse_reader, args=(sse_stop2,), daemon=True).start()
    check(has("line", lambda d: d["text"].startswith("NL HB")), "SSE reconnect: backlog replayed")
    check(len(events2) > 0, "SSE: events collected")
    sse_stop2.set()

    log_path = app.sess.file_path
    app.close()
    with open(log_path) as fh:
        text = fh.read()
    check(os.path.basename(log_path).startswith("nitrolab-")
          and re.match(r"nitrolab-\d{8}-\d{4}\.log$", os.path.basename(log_path)),
          "log file named <pedal>-<YYYYMMDD-HHMM>.log")
    check("=== connection" in text and "BUILD nitrolab abc1234-dirty DIAG" in text
          and "pedal gone" in text, "log file: section, BUILD, events")
    check(re.search(r"^\d\d:\d\d:\d\d\.\d{3} NL HB t=100", text, re.M) is not None,
          "log file: HH:MM:SS.mmm stamps")
    check(re.search(r"^\d\d:\d\d:\d\d\.\d{3} DFU entering bootloader", text, re.M) is not None,
          "log file: DFU line")
    shutil.rmtree(tmp)
    print("selftest " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    port = None
    if "--port" in sys.argv:
        port = int(sys.argv[sys.argv.index("--port") + 1])
    def _term(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, _term)   # `kill` closes as cleanly as Ctrl-C
    app = App(port=port)
    app.start()
    print("devtool: http://localhost:%d" % app.port, flush=True)
    print("serial: /dev/cu.usbmodem* (auto-reconnect); logs in %s; Ctrl-C quits"
          % os.path.relpath(diaglog.LOG_DIR, os.getcwd()), flush=True)
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        app.close()
        print("devtool: stopped", flush=True)


if __name__ == "__main__":
    main()
