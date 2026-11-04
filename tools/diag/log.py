#!/usr/bin/env python3
"""Live USB-serial logger for every pedal in this repo (`task log`).

Start once per session; it never exits by itself (q or Ctrl-C quits).

  * Connects to /dev/cu.usbmodem* (never tty.*: on macOS the tty node waits for
    carrier detect, which the Daisy's CDC port never asserts -> it just hangs).
  * Survives flash / DFU / power cycle: "--- pedal gone <time> ---", polls,
    "--- pedal back <time> ---".
  * On each connect sends `?` once (retried once after ~1 s) and waits for the
    DIAG firmware's answer `BUILD <pedal> <hash> DIAG <time>`
    (src/core/io/diag_link.h). Non-DIAG builds do not answer.
  * Every line, unfiltered, with an HH:MM:SS.mmm stamp, goes to
    log/<pedal>-<YYYYMMDD-HHMM>.log (one section per connection; a new file
    when the pedal name changes; `unknown` when there is no BUILD answer).
  * Keys:  /  filter (case-insensitive regex, substring if the regex is invalid;
              applied while typing, Enter keeps it, Esc in the prompt cancels)
           Esc clear filter     p pause/resume scrolling     q quit
    The last filter is kept in log/.logfilter for the next start.

  tools/diag/log.py              run the app
  tools/diag/log.py --selftest   offline self-test (pty pair, no pedal needed)

Python 3 stdlib only.
"""
import collections
import glob
import os
import re
import select
import subprocess
import sys
import termios
import time
import tty

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOG_DIR = os.path.join(REPO, "log")
FILTER_FILE = ".logfilter"
QUERY_TIMEOUT_S = 1.0
POLL_S = 0.3
SCROLLBACK = 20000
BUILD_RE = re.compile(r"^BUILD (\S+) (\S+) DIAG (.*)$")


def now_ts(t=None):
    t = time.time() if t is None else t
    return time.strftime("%H:%M:%S", time.localtime(t)) + ".%03d" % int((t % 1) * 1000)


def find_device():
    devs = sorted(glob.glob("/dev/cu.usbmodem*"))
    return devs[0] if devs else None


def git_head():
    try:
        return subprocess.run(["git", "-C", REPO, "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True, timeout=5).stdout.strip() or None
    except Exception:
        return None


class Filter:
    """Case-insensitive regex; falls back to substring when the regex is invalid."""

    def __init__(self, text=""):
        self.set(text)

    def set(self, text):
        self.text = text
        self.is_regex = True
        self.rx = None
        if text:
            try:
                self.rx = re.compile(text, re.I)
            except re.error:
                self.is_regex = False

    def match(self, line):
        if not self.text:
            return True
        if self.rx is not None:
            return self.rx.search(line) is not None
        return self.text.lower() in line.lower()

    def label(self):
        if not self.text:
            return "none"
        return self.text if self.is_regex else self.text + " (substring)"


class Session:
    """Connection, BUILD query, file writing and scrollback. No UI."""

    def __init__(self, log_dir=LOG_DIR, finder=find_device, head=git_head,
                 clock=time.time, query_timeout=QUERY_TIMEOUT_S):
        self.log_dir = log_dir
        self.finder = finder
        self.head = head
        self.clock = clock
        self.query_timeout = query_timeout
        self.fd = None
        self.dev = None
        self.rxbuf = b""
        self.lines = collections.deque(maxlen=SCROLLBACK)  # (seq, text)
        self.seq = 0
        self.ever_connected = False
        self.state = "waiting for pedal"
        self.build = None            # (pedal, hash, time) or None
        self.build_note = ""
        self.query_sent_at = None
        self.query_tries = 0
        self.pending = []            # lines buffered until the BUILD answer (or timeout)
        self.file = None
        self.file_pedal = None
        self.file_path = None
        self.last_try = 0.0

    # ---- display / file -------------------------------------------------
    def _show(self, text):
        self.seq += 1
        self.lines.append((self.seq, text))

    def _write(self, text):
        if self.file:
            self.file.write(text + "\n")
            self.file.flush()

    def _event(self, text):
        """A logger event: shown and (if a file is open) written."""
        self._show(text)
        self._write(text)

    def _open_file(self, pedal):
        if self.file and self.file_pedal == pedal:
            return
        if self.file:
            self.file.close()
        os.makedirs(self.log_dir, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M", time.localtime(self.clock()))
        self.file_path = os.path.join(self.log_dir, "%s-%s.log" % (pedal, stamp))
        self.file = open(self.file_path, "a")
        self.file_pedal = pedal

    # ---- connection -----------------------------------------------------
    def _connect(self, dev):
        fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            tty.setraw(fd)
            attrs = termios.tcgetattr(fd)
            attrs[4] = attrs[5] = termios.B115200
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
        except termios.error:
            pass   # a pty in the self-test may not take every attribute
        self.fd, self.dev, self.rxbuf = fd, dev, b""
        self.state = "connected " + dev
        t = now_ts(self.clock())
        if self.ever_connected:
            self._show("--- pedal back %s ---" % t)
            self.back_marker = "--- pedal back %s ---" % t
        else:
            self.back_marker = None
        self.ever_connected = True
        self.build, self.build_note = None, "asking..."
        self.pending = []
        self.connect_time = t
        self._send_query()

    def _send_query(self):
        try:
            os.write(self.fd, b"?\n")
        except OSError:
            pass
        self.query_sent_at = self.clock()
        self.query_tries += 1 if self.query_sent_at else 0

    def _disconnect(self):
        try:
            os.close(self.fd)
        except OSError:
            pass
        if self.query_sent_at is not None:
            self._resolve_build()   # flush buffered lines to a file before closing the section
        self.fd = None
        self.state = "waiting for pedal"
        self._event("--- pedal gone %s ---" % now_ts(self.clock()))

    def _resolve_build(self):
        """BUILD answered or timed out: open the file, write the section header + buffered lines."""
        pedal = self.build[0] if self.build else "unknown"
        self._open_file(pedal)
        self._write("=== connection %s %s ===" % (self.connect_time, self.dev))
        if self.back_marker:
            self._write(self.back_marker)
        if self.build:
            self._write("BUILD %s %s DIAG %s" % self.build)
        else:
            self._write("no build answer (non-debug build?)")
        for l in self.pending:
            self._write(l)
        self.pending = []
        self.query_sent_at = None
        self.query_tries = 0

    def _check_build(self, pedal, hsh, btime):
        self.build = (pedal, hsh, btime)
        head = self.head()
        built = hsh.split("-")[0]
        notes = []
        if head and built != head:
            notes.append("MISMATCH: built %s, HEAD %s" % (built, head))
        if hsh.endswith("-dirty"):
            notes.append("built from a dirty tree")
        self.build_note = "; ".join(notes)

    def _line(self, text):
        text = text.rstrip("\r")
        stamped = "%s %s" % (now_ts(self.clock()), text)
        self._show(stamped)
        m = BUILD_RE.match(text)
        if m and self.build is None and self.query_sent_at is not None:
            self._check_build(*m.groups())
        if self.query_sent_at is not None:
            self.pending.append(stamped)
            if self.build:
                self._resolve_build()
        else:
            self._write(stamped)

    def step(self, timeout=POLL_S, extra_fds=()):
        """One I/O round. Returns the extra fds that are readable."""
        now = self.clock()
        if self.fd is None:
            if now - self.last_try >= POLL_S:
                self.last_try = now
                dev = self.finder()
                if dev:
                    try:
                        self._connect(dev)
                    except OSError:
                        self.fd = None
        if self.fd is not None and self.dev and not os.path.exists(self.dev):
            self._disconnect()
        fds = list(extra_fds) + ([self.fd] if self.fd is not None else [])
        try:
            r, _, _ = select.select(fds, [], [], timeout)
        except (OSError, ValueError):
            r = []
        if self.fd is not None and self.fd in r:
            try:
                data = os.read(self.fd, 4096)
            except BlockingIOError:
                data = None
            except OSError:
                data = b""
            if data == b"":
                self._disconnect()
            elif data:
                self.rxbuf += data
                while b"\n" in self.rxbuf:
                    raw, self.rxbuf = self.rxbuf.split(b"\n", 1)
                    self._line(raw.decode("utf-8", "replace"))
        if self.fd is not None and self.query_sent_at is not None and self.build is None:
            if self.clock() - self.query_sent_at >= self.query_timeout:
                if self.query_tries < 2:
                    self._send_query()
                else:
                    self.build_note = ""
                    self._resolve_build()
        return [f for f in r if f in extra_fds]

    def header(self, flt, paused):
        if self.build:
            b = "%s %s DIAG %s" % self.build
        elif self.fd is not None and self.query_sent_at is None:
            b = "no build answer (non-debug build?)"
        elif self.fd is not None:
            b = "asking..."
        else:
            b = "-"
        parts = [self.state, "filter: " + flt.label(), b]
        if self.build_note:
            parts.append("!! " + self.build_note)
        if paused:
            parts.append("PAUSED")
        return " | ".join(parts)

    def close(self):
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
        if self.file:
            self.file.close()


# ---------------------------------------------------------------------------
# curses UI
# ---------------------------------------------------------------------------
def load_filter():
    try:
        with open(os.path.join(LOG_DIR, FILTER_FILE)) as f:
            return f.read().strip("\n")
    except OSError:
        return ""


def save_filter(text):
    try:
        os.makedirs(LOG_DIR, exist_ok=True)
        with open(os.path.join(LOG_DIR, FILTER_FILE), "w") as f:
            f.write(text + "\n")
    except OSError:
        pass


def run_ui(stdscr):
    import curses
    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.keypad(True)
    sess = Session()
    flt = Filter(load_filter())
    paused_at = None          # seq of the last line shown when paused
    prompt = None             # text being typed, None = no prompt
    before_prompt = flt.text
    try:
        while True:
            sess.step(timeout=0.05, extra_fds=(sys.stdin.fileno(),))
            while True:
                try:
                    k = stdscr.get_wch()
                except curses.error:
                    break
                if prompt is not None:
                    if k in ("\n", "\r", curses.KEY_ENTER):
                        save_filter(flt.text)
                        prompt = None
                    elif k == "\x1b":
                        flt.set(before_prompt)
                        prompt = None
                    elif k in (curses.KEY_BACKSPACE, "\x7f", "\b"):
                        prompt = prompt[:-1]
                        flt.set(prompt)
                    elif isinstance(k, str) and k.isprintable():
                        prompt += k
                        flt.set(prompt)
                    continue
                if k == "q":
                    return
                if k == "/":
                    before_prompt = flt.text
                    prompt = flt.text
                elif k == "\x1b":
                    flt.set("")
                    save_filter("")
                elif k == "p":
                    paused_at = None if paused_at is not None else sess.seq
            h, w = stdscr.getmaxyx()
            body = h - 1 - (1 if prompt is not None else 0)
            shown = []
            for seq, text in reversed(sess.lines):
                if paused_at is not None and seq > paused_at:
                    continue
                if flt.match(text) or text.startswith("--- pedal"):
                    shown.append(text)
                    if len(shown) >= body:
                        break
            shown.reverse()
            stdscr.erase()
            stdscr.addnstr(0, 0, sess.header(flt, paused_at is not None).ljust(w), w - 1,
                           curses.A_REVERSE)
            for i, text in enumerate(shown):
                stdscr.addnstr(1 + i, 0, text, w - 1)
            if prompt is not None:
                stdscr.addnstr(h - 1, 0, "/" + prompt, w - 1)
            stdscr.refresh()
    finally:
        sess.close()


# ---------------------------------------------------------------------------
# Self-test (offline: fake lines + pty pairs standing in for the pedal)
# ---------------------------------------------------------------------------
def selftest():
    import shutil
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("PASS " if cond else "FAIL ") + what)
        ok = ok and cond

    f = Filter("cpu=\\d+")
    check(f.match("NL HB t=1 CPU=42") and not f.match("LAB x"), "filter: case-insensitive regex")
    f.set("mute [")
    check(not f.is_regex and f.match("LAB a mute [1]") and not f.match("LAB"), "filter: invalid regex -> substring")
    f.set("")
    check(f.match("anything"), "filter: empty matches all")

    tmp = tempfile.mkdtemp()
    dev = {"path": None}
    sess = Session(log_dir=tmp, finder=lambda: dev["path"], head=lambda: "abc1234",
                   query_timeout=0.3)

    def pump(seconds, pedal_fd=None, answer=None):
        end = time.time() + seconds
        while time.time() < end:
            sess.step(timeout=0.02)
            if pedal_fd is not None:
                r, _, _ = select.select([pedal_fd], [], [], 0)
                if r:
                    try:
                        got = os.read(pedal_fd, 100)
                    except OSError:
                        got = b""
                    if b"?" in got and answer:
                        os.write(pedal_fd, answer)

    # 1) DIAG pedal answering BUILD
    m1, s1 = os.openpty()
    dev["path"] = os.ttyname(s1)
    pump(0.5, m1, b"BUILD nitrolab abc1234-dirty DIAG 2026-11-04T10:00\r\n")
    os.write(m1, b"NL HB t=100 cpu=420 mode=1\r\nLAB FM x\r\n")
    pump(0.3, m1)
    check(sess.build == ("nitrolab", "abc1234-dirty", "2026-11-04T10:00"), "BUILD answer parsed")
    check("dirty" in sess.build_note and "MISMATCH" not in sess.build_note, "dirty noted, hash matches HEAD")
    p1 = sess.file_path
    check(p1 and os.path.basename(p1).startswith("nitrolab-"), "file named after the pedal")

    # 2) disconnect -> gone
    dev["path"] = None
    os.close(m1)
    os.close(s1)
    pump(1.0)
    check(sess.fd is None and any("pedal gone" in t for _, t in sess.lines), "disconnect -> pedal gone")

    # 3) reconnect, same pedal, other hash -> mismatch, same file
    m2, s2 = os.openpty()
    dev["path"] = os.ttyname(s2)
    pump(0.5, m2, b"BUILD nitrolab def5678 DIAG 2026-11-04T11:00\n")
    check(any("pedal back" in t for _, t in sess.lines), "reconnect -> pedal back")
    check("MISMATCH" in sess.build_note, "hash mismatch warned")
    check(sess.file_path == p1, "same pedal -> same file")
    os.close(m2)
    os.close(s2)
    dev["path"] = None
    pump(1.0)

    # 4) other pedal -> new file
    m3, s3 = os.openpty()
    dev["path"] = os.ttyname(s3)
    pump(0.5, m3, b"BUILD chronotron3 abc1234 DIAG 2026-11-04T12:00\n")
    check(sess.file_path != p1 and "chronotron3-" in sess.file_path, "pedal changed -> new file")
    os.close(m3)
    os.close(s3)
    dev["path"] = None
    pump(1.0)

    # 5) non-DIAG pedal: no answer (query retried once) -> unknown file
    m4, s4 = os.openpty()
    dev["path"] = os.ttyname(s4)
    os.write(m4, b"Daisy is online\n")
    pump(1.2, m4, None)
    check(sess.build is None and "unknown-" in (sess.file_path or ""), "no answer -> unknown file")
    check("no build answer" in sess.header(Filter(), False), "header: no build answer")
    os.close(m4)
    os.close(s4)
    dev["path"] = None
    pump(1.0)
    sess.close()

    with open(p1) as fh:
        text = fh.read()
    check(text.count("=== connection") == 2, "one section per connection")
    check("BUILD nitrolab abc1234-dirty" in text and "LAB FM x" in text and "pedal gone" in text,
          "file holds BUILD, every line, events")
    check(re.search(r"^\d\d:\d\d:\d\d\.\d{3} NL HB", text, re.M) is not None, "HH:MM:SS.mmm stamps")
    shutil.rmtree(tmp)
    print("selftest " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    os.environ.setdefault("ESCDELAY", "25")
    import curses
    try:
        curses.wrapper(run_ui)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
