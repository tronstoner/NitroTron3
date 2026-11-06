# devtool - local dev interface (step 1)

Step 1 ("spike") of [docs/DEV_INTERFACE.md](../../docs/DEV_INTERFACE.md):
serial log, BUILD info and a Flash button in a browser page. No live
constants yet (step 2).

## Run

```
task dev                                  # = python3 tools/devtool/devtool.py
python3 tools/devtool/devtool.py --selftest   # offline (pty pair, fake make / dfu-util)
```

It prints `devtool: http://localhost:<port>` - open that in the Claude app's
browser pane or any browser. The port is the last one used if it is still
free (kept in `log/.devtoolport`, so an open page reconnects after a restart),
otherwise a free one. `--port N` forces one. Ctrl-C quits and closes the port.

Python 3 standard library only. The page (`web/`) is plain HTML/CSS/JS, no
build step, no external assets.

## Serial and log

devtool **owns the serial port** - do not run `task log` at the same time.
Connection, `?` / `BUILD` query, reconnect and the log files are
`tools/diag/log.py`'s `Session` (imported, unchanged), so the files are the
same as `task log` writes: `log/<pedal>-<YYYYMMDD-HHMM>.log`, stamped lines,
one section per connection.

## Endpoints (127.0.0.1 only; Host must be localhost)

| | |
|-|-|
| `GET /` | the page (`web/index.html`, `app.css`, `app.js`) |
| `GET /events` | Server-Sent Events: `hello`, `status`, `line`, `sep` (pedal gone/back), `flash` (state), `flashout` (make / dfu-util output). A new connection gets the backlog first. Times are epoch ms; the page formats them. |
| `GET /api/status` | connection, build, flash state as JSON |
| `POST /api/flash` | `{"pedal": "nitrolab" \| "chronotron3" \| "nitrotron3m"}`, `Content-Type: application/json`. 202 started, 409 a flash is running, 400 bad pedal |

## Flash flow (only on a click, one at a time)

1. `make <args> -j<cpus>` with the Taskfile `*-debug` arguments
   (`PEDAL=<p> DIAG=1`; nitrotron3m adds `NT3M_SLOT_B=sprawl INSTRUMENT=bass`).
2. If a pedal is on the serial port: send `dfu`. A DIAG firmware answers
   `DFU entering bootloader` and jumps to the Daisy bootloader.
   No pedal, or no DFU device after 15 s: the page asks for the
   both-footswitch gesture and keeps waiting (up to 120 s).
3. Wait for the DFU device (`dfu-util -l`, `0483:df11`).
4. `make <same args> program-dfu`. If dfu-util exits non-zero after
   "File downloaded successfully" (usual with `:leave`), it counts as done.
5. The serial loop picks the pedal up again by itself.

The `dfu` command exists only in DIAG firmware built from this change on
(`src/core/io/diag_link.h`); older firmware needs the gesture once.
