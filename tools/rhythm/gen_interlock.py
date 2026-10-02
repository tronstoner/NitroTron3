#!/usr/bin/env python3
"""Generate the engine-2 INTERLOCK rhythm tables (K3 mode 1) from four rows JSONs.

Two sets, both kept (vestige_constants.h VESTIGE_TIMING_RHY_IL_SET picks one):
  set 0, timelines:       CCW VESTIGE_TIMING_RHY_TL_CCW   <- rows_ccw_trad.json
                          CW  VESTIGE_TIMING_RHY_TL_CW    <- rows_cw_acad.json
  set 1, intensity paths: CCW VESTIGE_TIMING_RHY_PATH_CCW <- rows_ccw_path.json
                          CW  VESTIGE_TIMING_RHY_PATH_CW  <- rows_cw_path.json
                          (the path JSONs are written by gen_paths.py)

Writes eight generated blocks (between BEGIN/END markers, replaced in place):
  pedals/chronotron3/modules/vestige_constants.h  the rows of the four tables
                                                   (markers RHY CCW / CW / CCW_PATH / CW_PATH ROWS)
  tools/host/test_vestige.cpp                      kRhyLinesCcw[], kRhyLinesCw[],
                                                   kRhyLinesCcwPath[], kRhyLinesCwPath[]
                                                   (the JSON "line" = expected 3-bar base)
Row counts follow the JSONs (each table's size).
Usage: gen_interlock.py [ccw.json cw.json [ccw_path.json cw_path.json]] [repo_root]
  The JSONs default to the four files above, next to this script (2 given =
  the timelines pair only, 4 = all). repo_root defaults to the NitroTron3 checkout.
Per row (JSON): stut, rest, dec (pattern strings, 'x' hit; dec = the overlaps
of voice A and voice B, same length as rest), line (expected 3-bar base:
s stutter, D stutter + decimate accent, _ rest, - plain), a / b (voice names),
pr (duck | neg), a_k a_n a_rot, b_k b_n b_rot, n (= row + 1); optional b_pat
= an explicit voice-B pattern (instead of B = E(b_k, b_n, b_rot); then b_k =
its hit count, b_n = its length, b_rot = 0, as the C meta prints them).
Lengths: stut 2..64, rest = dec = lcm(a_n, b_n) <= 240 (the C side's limits,
VESTIGE_TIMING_RHY_STUT_MAX / _MASK_MAX). Nothing is rotated here: the patterns
are taken as they are.
"""
import json, os, sys
from math import gcd

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = sys.argv[1:]
JSONS = [a for a in ARGS if a.endswith(".json")]
OTHER = [a for a in ARGS if not a.endswith(".json")]
assert len(JSONS) in (0, 2, 4) and len(OTHER) <= 1, "usage: gen_interlock.py [ccw.json cw.json [ccw_path.json cw_path.json]] [repo_root]"
DEFAULTS = ["rows_ccw_trad.json", "rows_cw_acad.json", "rows_ccw_path.json", "rows_cw_path.json"]
SRCS = JSONS + [os.path.join(HERE, d) for d in DEFAULTS[len(JSONS):]]
ROOT = OTHER[0] if OTHER else "/Users/ralf/src/tronstoner/daisyseed/NitroTron3"
CONST = os.path.join(ROOT, "pedals/chronotron3/modules/vestige_constants.h")
TEST = os.path.join(ROOT, "tools/host/test_vestige.cpp")
STUT_MAX, MASK_MAX = 64, 240

def euc(k, n, rot, t):  # the module's RhyHit: hit at t if ((t % n + rot) % n * k) % n < k
    return k > 0 and (((t % n) + rot) % n) * k % n < k

def name(s):  # voice name without spaces (printed as-is by the DIAG log)
    return s.replace(" ", "_")

def check(i, r):
    s, m, d = r["stut"], r["rest"], r["dec"]
    assert set(s) <= set("x.") and set(m) <= set("x.") and set(d) <= set("x."), i
    assert 2 <= len(s) <= STUT_MAX, (i, len(s))
    assert len(d) == len(m), (i, "dec length != rest length")
    assert 1 <= len(m) <= MASK_MAX, (i, len(m))
    assert len(r["line"]) == 48 and set(r["line"]) <= set("sD_-"), i
    for t in range(48):  # the JSON's own rendering agrees with its patterns
        S, R, D = s[t % len(s)] == "x", m[t % len(m)] == "x", d[t % len(d)] == "x"
        c = "D" if (S and D) else "s" if S else "_" if R else "-"
        assert c == r["line"][t], (i, t)
    assert s.count("x") == r["a_k"] and len(s) == r["a_n"], i
    if r["a"].startswith("E("):
        assert all((s[t] == "x") == euc(r["a_k"], r["a_n"], r["a_rot"], t) for t in range(len(s))), i
    bp = r.get("b_pat")                       # an explicit voice-B pattern (intensity paths)
    if bp:                                    # (the meta prints its hit count / length, rot 0)
        assert set(bp) <= set("x.") and bp.count("x") == r["b_k"] and len(bp) == r["b_n"] and r["b_rot"] == 0, i
    bn = len(bp) if bp else r["b_n"]
    L = r["a_n"] * bn // gcd(r["a_n"], bn)
    assert len(m) == L, (i, len(m), L)
    for t in range(L):
        A = s[t % len(s)] == "x"
        B = (bp[t % len(bp)] == "x") if bp else euc(r["b_k"], r["b_n"], r["b_rot"], t)
        # the rest mask = voice B by its principle (ducking / negative space), never on a stutter
        want = (B and not A) if r["pr"] == "duck" else (not A and not B)
        assert (m[t] == "x") == want, (i, t)
        assert not (A and m[t] == "x"), (i, t, "rest on a stutter")
        # dec = exactly the overlaps of A and B, only on a stutter
        assert (d[t] == "x") == (A and B), (i, t, "dec != overlap")
    assert r["pr"] in ("duck", "neg"), i
    assert int(r["n"]) == i + 1, i

PR = {"duck": ("ducking", "kRhyPrDuck"), "neg": ("negative space", "kRhyPrNeg")}

def render(rows):
    c_lines, t_lines = [], []
    for i, r in enumerate(rows):
        check(i, r)
        note = "a=%s %s b=%s, decimates = the %d overlaps" % (r["a"], PR[r["pr"]][0], r["b"], r["dec"].count("x"))
        meta = '{{"%s", %d, %d, %d}, %s, {"%s", %d, %d, %d}}' % (
            name(r["a"]), r["a_k"], r["a_n"], r["a_rot"], PR[r["pr"]][1], name(r["b"]), r["b_k"], r["b_n"], r["b_rot"])
        c_lines.append('  /* #%-2d %s */\n  {kRhyGrid1x, RhyStr("%s"), RhyStr("%s"), RhyStr("%s"), %s},'
                       % (i + 1, note, r["stut"], r["rest"], r["dec"], meta))
        t_lines.append('  /* #%-2d */ "%s",' % (i + 1, r["line"]))
    return c_lines, t_lines

def splice(path, begin, end, body):
    txt = open(path).read()
    a = txt.index(begin) + len(begin)
    b = txt.index(end, a)
    line_start = txt.rfind("\n", 0, b) + 1
    txt = txt[:a] + "\n" + "\n".join(body) + "\n" + txt[line_start:]
    open(path, "w").write(txt)

TAG = "gen_interlock.py — do not hand-edit"
for side, src in zip(("CCW", "CW", "CCW_PATH", "CW_PATH"), SRCS):
    rows = json.load(open(src))
    c_lines, t_lines = render(rows)
    splice(CONST, "// BEGIN GENERATED RHY %s ROWS (%s)" % (side, TAG), "// END GENERATED RHY %s ROWS" % side, c_lines)
    splice(TEST, "// BEGIN GENERATED RHY %s LINES (%s)" % (side, TAG), "// END GENERATED RHY %s LINES" % side, t_lines)
    print("%s: generated %d rows from %s" % (side, len(rows), os.path.basename(src)))
