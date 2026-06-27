---
name: update-controls
description: Regenerate the README controls/LEDs tables AND the per-mode pedal-layout SVG diagrams from the current source code.
disable-model-invocation: false
allowed-tools: Read Edit Grep Bash
---

# Update Controls Documentation

Parse `src/NitroTron3.cpp` and regenerate the controls and LEDs tables in `README.md`.

**Controls documentation lives in three places that must stay in lockstep:** the
README tables, the per-mode prose/tables in `docs/USER_MANUAL.md`, and the
per-mode **pedal-layout SVG diagrams** (embedded in the manual). When a mode's
controls or labels change, all three move together. The SVGs are the part most
easily forgotten — see "Pedal-layout SVGs" below.

## Steps

1. Read `src/NitroTron3.cpp`
2. For each control (KNOB 1–6, SWITCH 1–3, FOOTSWITCH 1–2), identify:
   - What it does in each drone mode (FIXED, TRACK, TRACK_DIRECT)
   - Knob ranges and mapping
   - Switch positions (UP/MIDDLE/DOWN)
3. Read `README.md` to find the existing controls table
4. Regenerate the table using the template from `CLAUDE.md` (all controls listed, even if unused)
5. Update the LEDs table if LED behavior has changed
6. Show the diff to the user

## Pedal-layout SVGs (part of the user manual)

`docs/pedal-mode-{a,b,c}.svg` are **generated build artifacts** of
`docs/gen_layout_svg.py` and are embedded in `docs/USER_MANUAL.md` → the manual
PDF. They are control documentation, so they must track the controls too.

7. **Never hand-edit the `.svg` files** — they are regenerated and your edit
   will be overwritten. Edit the `MODES` dict in `docs/gen_layout_svg.py`
   instead (per mode: `knobs`, `sw1`, `sw2`, `sw3` label lists) so the labels
   match the current controls.
8. Regenerate and verify the labels match the README/USER_MANUAL you just wrote:
   ```sh
   python3 docs/gen_layout_svg.py
   ```
9. Commit `gen_layout_svg.py` and the regenerated `.svg` files. If the manual is
   being shipped, rebuild it with `make manual` (the `.pdf` is gitignored; the
   `release` skill rebuilds it at release time).

## Template (from CLAUDE.md)

Every physical control must be listed. Use this format:

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | ... | ... |
| KNOB 2 | ... | ... |
| KNOB 3 | ... | ... |
| KNOB 4 | ... | ... |
| KNOB 5 | ... | ... |
| KNOB 6 | ... | ... |
| SWITCH 1 | ... | **UP** - ...<br/>**MIDDLE** - ...<br/>**DOWN** - ... |
| SWITCH 2 | ... | **UP** - ...<br/>**MIDDLE** - ...<br/>**DOWN** - ... |
| SWITCH 3 | ... | **UP** - ...<br/>**MIDDLE** - ...<br/>**DOWN** - ... |
| FOOTSWITCH 1 | ... | ... |
| FOOTSWITCH 2 | ... | ... |
