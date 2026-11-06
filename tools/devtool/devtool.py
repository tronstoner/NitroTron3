#!/usr/bin/env python3
"""Local dev interface for the pedals (`task dev`) - step 1 of docs/DEV_INTERFACE.md.

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

  tools/devtool/devtool.py              run it (Ctrl-C quits)
  tools/devtool/devtool.py --selftest   offline self-test (pty pair, no pedal)

Python 3 stdlib only. Only one program can hold the port: while devtool runs it
is the logger (do not run `task log` at the same time).
"""
import collections
import http.client
import json
import os
import queue
import re
import select
import signal
import socket
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
          "/app.js": ("app.js", "text/javascript; charset=utf-8")}
LOCAL_HOSTS = ("localhost", "127.0.0.1", "[::1]")


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

    def publish(self, kind, data):
        with self.lock:
            if kind in ("line", "sep"):
                self.backlog.append((kind, data))
            elif kind == "flashout":
                self.flash_out.append((kind, data))
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
        with self.lock:
            snap = [("hello", {"instance": self.instance})]
            snap.append(("status", self.status))
            snap.append(("flash", self.flash))
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
        self.hub.publish("line", {"t": now_ms(t), "text": text.rstrip("\r")})
        if self.build != had_build:
            self.publish_status()

    def _connect(self, dev):
        super()._connect(dev)
        self.publish_status()

    def _disconnect(self):
        super()._disconnect()
        self.publish_status()

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

    def __init__(self, sess):
        self.sess = sess
        self.lock = threading.RLock()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, name="serial", daemon=True)

    def _run(self):
        while not self.stop.is_set():
            with self.lock:
                self.sess.step(timeout=0.02)
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

    def _out(self, text, stream):
        self.hub.publish("flashout", {"t": now_ms(), "text": text, "stream": stream})

    def _cmd(self, args, stream):
        """Run a command, stream its output. Returns (exit code, all output)."""
        self._out("$ " + " ".join(args), "info")
        lines = []
        try:
            p = subprocess.Popen(args, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, text=True, errors="replace", bufsize=1)
        except OSError as e:
            self._out("cannot run %s: %s" % (args[0], e), "info")
            return -1, ""
        self.proc = p
        for line in p.stdout:
            line = line.rstrip("\n")
            lines.append(line)
            self._out(line, stream)
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
        elif path == "/events":
            self._events()
        else:
            self._send(404, b"not found\n", "text/plain")

    def do_POST(self):
        if not self._host_ok():
            return
        path = self.path.split("?")[0]
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n > 0 else b""
        if path != "/api/flash":
            self._send(404, b"not found\n", "text/plain")
            return
        # application/json only: a cross-site form post cannot send it without a
        # CORS preflight, which this server never grants.
        if (self.headers.get("Content-Type") or "").split(";")[0].strip() != "application/json":
            self._json(415, {"ok": False, "message": "send application/json"})
            return
        try:
            pedal = json.loads(body.decode() or "{}").get("pedal")
        except (ValueError, AttributeError):
            self._json(400, {"ok": False, "message": "bad JSON"})
            return
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


def bind_server(port=None):
    """`port` given -> that port. Else the last port if free, else a free one."""
    tries = [port] if port is not None else []
    if port is None:
        try:
            with open(PORT_FILE) as f:
                tries.append(int(f.read().strip()))
        except (OSError, ValueError):
            pass
        tries.append(0)
    err = None
    for p in tries:
        try:
            return Server(("127.0.0.1", p), Handler)
        except OSError as e:
            err = e
    raise err


class App:
    def __init__(self, sess_kw=None, flasher_kw=None, port=None, remember_port=True):
        self.hub = Hub()
        self.sess = DevSession(self.hub, **(sess_kw or {}))
        self.serial = SerialLoop(self.sess)
        self.flasher = Flasher(self.hub, self.serial, **(flasher_kw or {}))
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
    dev = {"path": None}
    pedal = {"got": b"", "dfu": False}
    calls = []

    def fake_argv(args):
        calls.append(list(args))
        return [sys.executable, "-c", "import sys; print('fake make', *sys.argv[1:])"] + list(args)

    app = App(sess_kw={"log_dir": tmp, "finder": lambda: dev["path"], "head": lambda: "abc1234",
                       "query_timeout": 0.3},
              flasher_kw={"argv": fake_argv, "dfu_check": lambda: pedal["dfu"],
                          "hint_s": 0.4, "max_s": 5.0, "poll_s": 0.05, "jobs": 2},
              port=0, remember_port=False)
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
    st, _ = req("GET", "/api/status", headers={"Host": "evil.example:80"})
    check(st == 403, "foreign Host header refused")
    st, _ = req("POST", "/api/flash", body=b"pedal=nitrolab",
                headers={"Content-Type": "application/x-www-form-urlencoded"})
    check(st == 415, "flash: non-JSON post refused")
    st, _ = req("POST", "/api/flash", body=b'{"pedal":"nope"}',
                headers={"Content-Type": "application/json"})
    check(st == 400, "flash: unknown pedal refused")

    # 3) flash flow: build -> dfu over serial -> DFU device -> program-dfu
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

    # 4) pedal gone
    pstop.set()
    dev["path"] = None
    os.close(m1)
    os.close(s1)
    check(has("sep", lambda d: d["what"] == "gone"), "SSE: pedal gone separator")
    check(has("status", lambda d: d.get("connected") is False and d.get("build_state") == "waiting"),
          "status: disconnected")

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
