---
name: update-controls
description: Regenerate the README controls and LEDs tables from the current source code.
disable-model-invocation: false
allowed-tools: Read Edit Grep
---

# Update Controls Documentation

Parse `src/NitroTron3.cpp` and regenerate the controls and LEDs tables in `README.md`.

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
