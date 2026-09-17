#!/usr/bin/env bash
# Capture ChronoTron3 DIAG serial output to a timestamped log.
# See .agents/skills/serial-diag/SKILL.md for the workflow and the field key.
#
# Why /dev/cu.usbmodem* and NOT /dev/tty.usbmodem*:
#   On macOS the tty.* node is the "dial-in" device — opening it blocks until the
#   port asserts carrier detect (DCD). The Daisy's CDC-ACM port never does, so a
#   `cat /dev/tty.usbmodem*` just hangs and reads nothing. The cu.* ("call-out")
#   node opens immediately and streams. Always use cu.*.
#
# The device node vanishes when the pedal is power-cycled or re-flashed — the
# script then exits; just run it again once the pedal is back.
set -euo pipefail
DEV="${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
[ -n "$DEV" ] || { echo "no /dev/cu.usbmodem* device (is the pedal connected?)" >&2; exit 1; }
OUT="sprawl-$(date +%Y%m%d-%H%M).log"
echo "capturing $DEV -> $OUT  (Ctrl-C to stop)"
stty -f "$DEV" 115200 raw -echo
cat "$DEV" | while IFS= read -r l; do printf '%s %s\n' "$(date +%T)" "$l"; done | tee "$OUT"
