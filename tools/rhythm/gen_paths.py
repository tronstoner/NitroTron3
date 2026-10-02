#!/usr/bin/env python3
"""Write the K3 mode-1 INTENSITY PATHS (rows_ccw_path.json, rows_cw_path.json).

One rhythmic family per side; along the knob hits are only ADDED, never
swapped: row #1 (just past noon) = a minimal, slow variation, the last row =
the rhythmically dense one. Voice A = stutters, voice B = rests (ducking);
B is an explicit pattern (b_pat) in the same base as A, its shape grows too.
Overlaps of A and B = decimate accents. Feed the JSONs to gen_interlock.py.
  CCW: son-clave family on a 2-bar (32-step) cycle, B = beat 4 -> off-beats.
  CW:  A on base 15 against B on base 7 (they drift through each other).
"""
import json, os
from math import gcd

HERE = os.path.dirname(os.path.abspath(__file__))

def mk(n, pos): return "".join("x" if i in pos else "." for i in range(n))

def row(a, n_a, S, b, n_b, R):
    st, bp = mk(n_a, S), mk(n_b, R)
    L = n_a * n_b // gcd(n_a, n_b)
    rest = "".join("x" if bp[t % n_b] == "x" and st[t % n_a] != "x" else "." for t in range(L))
    dec = "".join("x" if bp[t % n_b] == "x" and st[t % n_a] == "x" else "." for t in range(L))
    line = "".join(("D" if dec[t % L] == "x" else "s") if st[t % n_a] == "x" else ("_" if rest[t % L] == "x" else "-")
                   for t in range(48))
    return dict(a=a, b=b, pr="duck", stut=st, rest=rest, dec=dec, b_pat=bp, line=line,
                a_k=st.count("x"), a_n=n_a, a_rot=0, b_k=bp.count("x"), b_n=n_b, b_rot=0)

# CCW: 2 bars of 16ths; son clave 3-2 = 0 3 6 | 10 12 per bar
tres = {0, 3, 6, 16, 19, 22}
son = tres | {10, 12, 26, 28}
cinq = son | {2, 5, 18, 21}
cinq2 = cinq | {8, 24}
dense = cinq2 | {14, 30}
full = dense | {9, 25}
b4 = {12, 28}                                   # the 4th 8th-pair of each bar
off = {2, 6, 10, 14, 18, 22, 26, 30} | b4       # off-beat 8ths + beat 4
off24 = off | {4, 20}
CCW = [("none", set(), "beat4/2bars", {28}), ("none", set(), "beat4", b4),
       ("bombo/2bars", {19}, "beat4", b4), ("bombo", {3, 19}, "beat4", b4),
       ("tresillo", tres, "beat4", b4), ("son_3-2", son, "beat4", b4),
       ("son_3-2", son, "offbeats", off), ("son+cinq", cinq, "offbeats", off),
       ("son+cinq2", cinq2, "offbeats", off), ("dense", dense, "off+2+4", off24),
       ("full", full, "off+2+4", off24), ("full+", full | {7, 23}, "off+2+4", off24)]

# CW: base 15 vs base 7, nested maximally-even insertion order
A_ORDER, B_ORDER = [0, 8, 4, 12, 2, 10, 6, 14, 1, 9], [3, 0, 5]
CW = [(1, 0), (2, 0), (2, 1), (3, 1), (4, 1), (4, 2), (5, 2), (6, 2), (7, 3), (8, 3), (9, 3), (10, 3)]

ccw = [row(a, 32, S, b, 32, R) for a, S, b, R in CCW]
cw = [row("A15:%d" % ka, 15, set(A_ORDER[:ka]), "B7:%d" % kb, 7, set(B_ORDER[:kb])) for ka, kb in CW]
for name, rows in (("rows_ccw_path.json", ccw), ("rows_cw_path.json", cw)):
    for i, r in enumerate(rows): r["n"] = i + 1
    json.dump(rows, open(os.path.join(HERE, name), "w"), indent=1)
    for r in rows:
        l = r["line"]
        print("%-18s #%-2d %-11s %-12s |%s|%s|%s|" % (name, r["n"], r["a"], r["b"], l[:16], l[16:32], l[32:]))
