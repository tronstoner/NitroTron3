# Dev Interface — concept plan

**Status: concept, decisions settled (2026-11-06). Nothing built.**

## 1. Why

Judging a sound currently takes a flash, or asking the agent to move a value
and then flashing. The panel has 6 knobs; a module has hundreds of constants.
That loop is the biggest blocker in the project. The goal is one local
development interface that runs in the Claude app's browser pane (or any
browser). It lets the builder:

- tune constants live on the pedal (no flash, no agent),
- see the DIAG log in the same place,
- flash the pedal from a button,
- leave a written record of what changed, so the agent can apply it to the
  constants files.

**Not in scope:** the later user-facing online editor. It is a separate
project with its own look and code. The only thing it may reuse is the
registry idea (section 4).

## 2. Architecture

```
   Claude app browser pane / Chrome
   ┌──────────────────────────────┐
   │  Dev page (static HTML/JS)   │
   └──────────────┬───────────────┘
                  │ http://localhost:<port>   (HTTP + Server-Sent Events)
   ┌──────────────┴───────────────┐
   │  devtool  (Python, stdlib)   │──── writes ──▶ tune/changes.json, log/*.log
   │  owns the serial port        │──── runs ───▶ make / dfu-util (flash)
   └──────────────┬───────────────┘
                  │ USB serial (/dev/cu.usbmodem*)
   ┌──────────────┴───────────────┐
   │  Pedal, DIAG/debug build     │
   │  diag_link: log out,         │
   │  commands in (?, list, set…) │
   └──────────────────────────────┘
```

**Why a local helper and not Web Serial:** this app's browser has the Web
Serial API but no device picker (tested 2026-11-06 with the updater). A local
program owns the port and the files. The page then works in any browser,
including this pane, so the agent can read it as well.

- **devtool:** Python 3, standard library only, like `tools/diag/log.py`,
  whose serial and reconnect code it takes over. Started with `task dev`.
  `log.py` stays as the terminal fallback.
- **Only one program can hold the port.** While devtool runs, it is the
  logger.

## 3. Pedal side (debug builds only)

- **Normal builds stay byte-identical.** Everything below exists only
  behind `CT3_DIAG_BUILD`, like today's diag_link.
- **Live constants:**
  - A constant marked *live* becomes a RAM variable in the debug build; in
    the normal build it stays `constexpr`.
  - This is the mechanism armitage already uses for the hostsim
    (`ARM_TUNABLE`, `tools/hostsim/host_tunables.h`). It gets generalised to
    every module (`TUNABLE(type, NAME, value)`).
- **What can be live:**
  - Values read while audio runs can be changed at once.
  - Values used only at `Init` (table contents, array sizes) are marked
    *rebuild* (table data, re-run on a command; can take seconds) or *flash
    only*.
  - Array sizes and `static_assert` inputs stay fixed.
- **Commands over the existing USB link** (line based, parsed in the main
  loop, never in the audio interrupt):

  | command | answer |
  |-|-|
  | `?` | `BUILD <pedal> <hash> DIAG <time>` (as today) |
  | `list` | one line per live constant: id, name, type, current value |
  | `get <id>` | current value |
  | `set <id> <value>` | applied; echoes the new value |
  | `reset <id>` / `reset all` | back to the compiled default |
  | `rebuild` | re-runs `Init`-time work (e.g. explorer tables) |
  | `dfu` | jumps to the bootloader, so no footswitch gesture is needed for flashing |

  `set` writes a float or int that the audio code reads. Writing a 32-bit
  value is atomic on the Cortex-M7. Values that must change together (rare)
  are applied at the next block boundary.
- **Cost to check in step 1:** RAM variables instead of constants means more
  CPU (no constant folding) and more code. Measure on the explorer before
  rolling it out widely.

## 4. Registry (single source of truth)

- **Where it comes from:** the constants files themselves; no second list to
  keep in sync. The comment parser that already exists
  (`tools/hostsim/const_docs.h`) reads, per constant:
  - **group:** pedal → module → section (`// --- Title ---` headers)
  - **label and explanation:** the comment block above it and its inline
    comment
- **Additions per constant:** written in a small structured comment tag on
  the same line, never in a separate file:
  - unit (Hz, s, dB, bits, oct, ratio, 0..1)
  - range (min, max) and taper (lin / log)
  - level: *live*, *rebuild* or *flash*
  - audience: *debug* (default) or *user* (for the later editor)
- **Build-time output:** devtool generates a JSON registry and ships it with
  the page. The pedal sends only ids and values. Labels never travel over USB.
- **Mismatch guard:** the registry carries the git hash. If the pedal's
  `BUILD` hash differs, the page says so and blocks `set`.

## 5. The page

Layout (desktop-first, also usable in the narrow app pane):

- **Top bar:**
  - connection state, pedal, firmware hash and dirty state
  - **Flash** button: build + `dfu` + program, with progress
- **Left: tree** of groups (module → mode → section) with a search box.
- **Centre: constants of the selected group.** One row per constant:
  - label, slider (in the unit's taper), number field, unit
  - default, with a "changed" mark and a reset
  - the explanation, folded under the row
- **Right: live panel.**
  - current knob and switch positions plus the module's state line (parsed
    DIAG lines)
  - **A/B snapshots:** store A, store B, toggle while playing
- **Bottom: log.**
  - DIAG stream with filter and pause, the same features as `log.py`
  - written to `log/` as today
- **Changes panel:**
  - every value that differs from the compiled default (name, old → new,
    group) plus a free-text "what I heard" note
  - kept up to date by devtool in `tune/changes.json`
- **Formatting:** all numbers go through one formatting helper with the unit
  and a fixed precision per unit. Times are shown in local time only.

## 6. Applying changes

- `tune/changes.json` is written by devtool on every change (and visible on
  the page).
- On "apply", the agent reads it and patches the constants files. The builder
  reviews the diff, and it is committed as usual.
- **The page and devtool never edit source files.**
- After a flash with the applied values, the changes list is empty again (the
  defaults now match).

## 7. Steps

1. **Spike:** devtool skeleton.
   - serial reconnect, log stream to the page, `?`/BUILD, Flash button
     (build + gesture-free `dfu` + `dfu-util`)
   - no tuning yet
   - Proves the pane, serial and flash path end to end.
2. **Live constants for the NitroLab explorer.**
   - Start with the lofi, touch env and FM LFO sections.
   - Pedal: `list`/`get`/`set`/`reset`.
   - Page: tree, rows, changes panel, `tune/changes.json`.
   - Measure CPU and code size.
3. A/B snapshots, the live panel (knobs, state line), `rebuild` for
   explorer tables.
4. Roll out to the other NitroLab modules, then ChronoTron3 and nitrotron3m.
5. Later, separate project: the user editor (reads the registry's *user*
   values).

Each step is its own go.

## 8. Decisions (2026-11-06)

- **Port:** automatic free port, printed on each start.
- **`dfu` command:** yes, in debug builds only. Release builds keep the
  footswitch gesture as the only way in.
- **Live scope for step 2:** the sections being tuned now (lofi, touch env,
  FM LFO). Measure CPU and size, then widen.
- **hostsim:** later, served by the same devtool (same registry and page;
  the target is the pedal or the host).
- **A/B snapshots:** yes, in step 3.
