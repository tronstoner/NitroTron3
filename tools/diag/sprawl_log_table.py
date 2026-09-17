#!/usr/bin/env python3
"""Tabulate a ChronoTron3 sprawl DIAG capture (tools/diag/capture.sh).

One row per snapshot (a `SP ...` header line plus the ~18 lines that follow).
Read the raw block around anything odd -- this table is a finding aid, not a
replacement for the log. See .agents/skills/serial-diag/SKILL.md for the field
key and for what the columns mean.

usage: tools/diag/sprawl_log_table.py <log>
"""

import re
import sys

# column -> (emitter line prefix, field name as the emitter prints it)
# `k=` on the ctl line carries five values; k4 is the 4th, k5 the 5th.
# `deg ch=A/B` and `deg d=A/B` are pairs; we show A (the active value).
FIELDS = [
    ("k4",   "ctl", "k",    3),
    ("k5",   "ctl", "k",    4),
    ("fb",   "prm", "fb",   None),
    ("duck", "fbk", "duck", None),
    ("onp",  "fbk", "onp",  None),
    ("act",  "grn", "act",  None),
    ("tmr",  "grn", "tmr",  None),
    ("ch",   "deg", "ch",   None),
    ("mix",  "deg", "mix",  None),
    ("d",    "deg", "d",    None),
    ("in",   "met", "in",   None),
    ("grn",  "met", "grn",  None),
    ("tex",  "met", "tex",  None),
    ("wet",  "met", "wet",  None),
    ("pw",   "env", "pw",   None),
]

ODD = re.compile(r"(?<![\w.])-?(?:nan|inf)(?![\w])|\de6(?![\w])")
LINE = re.compile(r"^(\d{2}:\d{2}:\d{2})\s+(.*)$")


def field(lines, prefix, name, index):
    """First `name=` value on a line starting with `prefix`; `-` if absent."""
    pat = re.compile(r"\b" + re.escape(name) + r"=(\S+)")
    for ln in lines:
        if not ln.startswith(prefix + " ") and ln != prefix:
            continue
        m = pat.search(ln)
        if not m:
            continue
        if index is not None:
            # multi-value field: the match plus the following whitespace tokens
            rest = ln[m.start(1):].split()
            return rest[index] if index < len(rest) else "-"
        return m.group(1).split("/")[0]
    return "-"


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(argv[1], "r", errors="replace") as fh:
        raw = [LINE.match(l.rstrip("\n")) for l in fh]
    entries = [(m.group(1), m.group(2).strip()) for m in raw if m]

    snaps = []   # (time, header, [payload lines])
    for t, body in entries:
        if body.startswith("SP "):
            snaps.append((t, body[3:], []))
        elif snaps:
            snaps[-1][2].append(body)

    cols = ["time", "hdr"] + [f[0] for f in FIELDS]
    rows = []
    for t, hdr, lines in snaps:
        rows.append([t, hdr[:24]] + [field(lines, p, n, i) for _, p, n, i in FIELDS])

    width = [max(len(c), *(len(r[k]) for r in rows)) if rows else len(c)
             for k, c in enumerate(cols)]
    fmt = "  ".join("{:<%d}" % w for w in width)
    print(fmt.format(*cols))
    print(fmt.format(*["-" * w for w in width]))
    for r in rows:
        print(fmt.format(*r))

    faults = sum(1 for _, h, _ in snaps if "FAULT" in h)
    silent = sum(1 for _, h, _ in snaps if "WET SILENT" in h)
    odd = [(t, ln) for t, h, ls in snaps for ln in [h] + ls if ODD.search(ln)]

    print()
    print("snapshots: %d   faults: %d   WET SILENT: %d   odd values: %d"
          % (len(snaps), faults, silent, len(odd)))
    for t, ln in odd:
        print("  %s  %s" % (t, ln))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
