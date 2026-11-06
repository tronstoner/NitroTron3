# devtool - local dev interface (steps 1 + 2)

Steps 1 and 2 of [docs/DEV_INTERFACE.md](../../docs/DEV_INTERFACE.md):
serial log, BUILD info and a Flash button in a browser page (step 1), and live
constants on a DIAG build (step 2: tree, rows, changes panel,
`tune/changes.json`).

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
| `GET /api/registry` | live constants: registry (from the constants files) + the pedal's values + guard state |
| `POST /api/set` | `{"name": N, "value": V}`: 202 queued (sent throttled, latest wins, <= ~30 writes/s), 400 type / range, 404 unknown, 409 blocked (guard, no pedal) |
| `POST /api/reset` | `{"name": N}` or `{"name": "all"}` |
| `POST /api/note` | `{"text": "what I heard"}` (goes into `tune/changes.json`) |
| `POST /api/override` | `{"on": true}`: allow `set` despite the mismatch guard, this devtool session |

SSE also carries `tun` (registry + values snapshot), `tval` (one value echo
from the pedal) and `terr` (a `T ERR` line).

## Live constants (step 2)

- **Firmware:** a constant declared `TUNABLE(type, NAME, value);`
  (`src/core/util/tunable.h`) is `constexpr` in normal builds (byte-identical)
  and a registered RAM variable in DIAG builds (`DIAG=1` adds
  `-DLIVE_TUNABLES`). The diag link (`src/core/io/diag_link.h`) answers
  `list`, `get <name>`, `set <name> <value>`, `reset <name>|all` with
  `T <name> <f|i|b> <value>` lines (`T END <n>` after a list, `T ERR <reason>`).
- **Registry:** `registry.py` parses the connected pedal's constants files
  (`CONST_SOURCES` in `devtool.py`): section headers `// --- Title ---`, the
  comment block above, the inline note, and the tag at the start of the note:
  `// @unit Hz @range 0.001 4000 @taper log @level live @why <engine limit>  <note>`.
  `@range` = the real engine limit (where the code stops working or the value
  stops meaning anything), never an invented bracket; `@why` (free text up to
  the next ` @tag` or a double space) says which limit - the info popover shows
  "Range: a .. b - <why>". `@below <NAME>`: an ordering the code needs (it
  divides by the difference / log ratio); devtool refuses a set that breaks it.
  Levels: `live` (heard at the next audio block), `reactivate` (after the next
  mode / zone change).
- **Sync:** after `BUILD`, devtool sends `list`; the pedal is the truth
  (re-synced after every reconnect / flash; a flash restores the defaults).
  `T <name>` lines go to the log file, not the page log.
- **Guard:** `set` is blocked when the firmware hash is not the repo HEAD,
  when the constants files differ from HEAD (uncommitted), or when the pedal
  reports live constants the files do not declare. "Allow changes anyway"
  overrides it for the session. Warnings only: a dirty firmware, and file
  constants the firmware does not have (e.g. a module not in this build; those
  rows are shown disabled).
- **`tune/changes.json`** (gitignored): rewritten on every change - pedal,
  firmware, HEAD, time, note, guard, and per changed constant name, file, line,
  default text, value, unit. devtool never edits source files; the agent
  applies the file. When a re-sync would drop a non-empty record (e.g. after a
  flash), the old file is kept in `tune/history/`.

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

Tool output is shown the way a terminal shows it: a `\r` without `\n`
(dfu-util's progress bar) replaces the current line instead of adding one.

The `dfu` command exists only in DIAG firmware built from this change on
(`src/core/io/diag_link.h`); older firmware needs the gesture once.
