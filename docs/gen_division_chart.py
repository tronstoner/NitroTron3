#!/usr/bin/env python3
"""Generate docs/ChronoTron3/division-chart.svg — a rhythm chart of the mnemonic
Edge multitap delay. One row per K1 stop: an input impulse at t=0, the PRIMARY
(division) echoes drawn upward, the SECONDARY (telephone) companion echoes drawn
downward, decaying at a medium feedback, over a faint quarter-note grid (= your
tapped pulse). Ratios mirror MNEM_DIV_RATIOS / MNEM_EDGE_SECONDARY_RATIOS in
pedals/chronotron3/modules/mnemonic_constants.h — keep them in sync.

Run:  python3 docs/gen_division_chart.py
"""
import os
import math

# (primary ratio, primary label, secondary ratio, secondary label) per K1 stop,
# index-aligned with the firmware arrays.
STOPS = [
    (0.25,      "1/4 sixteenth",       0.5,  "1/2 eighth"),
    (1/3,       "1/3 eighth-triplet",  0.5,  "1/2 eighth"),
    (0.5,       "1/2 eighth",          0.75, "3/4 dotted-8"),
    (2/3,       "2/3 quarter-triplet", 1.0,  "1/1 quarter"),
    (0.75,      "3/4 dotted-8 *",      0.5,  "1/2 eighth"),
    (1.0,       "1/1 quarter (NOON)",  0.75, "3/4 dotted-8"),
    (4/3,       "4/3 half-triplet",    1.0,  "1/1 quarter"),
    (1.5,       "3/2 dotted-quarter",  1.0,  "1/1 quarter"),
    (2.0,       "2/1 half",            0.75, "3/4 dotted-8"),
    (3.0,       "3/1 dotted-half",     1.0,  "1/1 quarter"),
    (4.0,       "4/1 whole",           1.5,  "3/2 dotted-quarter"),
]

FB       = 0.62     # medium feedback (each successive echo x this)
MIN_AMP  = 0.08     # stop drawing echoes below this
T_MAX    = 8.0      # beats (quarter-notes) shown
PX_BEAT  = 38       # horizontal compression
KNOB_CX  = 40       # knob dial column (per-row K1 position)
KNOB_R   = 17
LABEL_X  = 68       # ratio labels start here (right of the knob)
X0       = 250                   # t = 0
PLOT_W   = int(T_MAX * PX_BEAT)
WIDTH    = X0 + PLOT_W + 22
TOP      = 108      # header: title + 2 wrapped subtitle lines + legend
ROW_H    = 58
HALF     = 23                    # max bar half-height (up = primary, down = secondary)
ROWS     = len(STOPS)
HEIGHT   = TOP + ROWS * ROW_H + 34
DIV_COUNT = ROWS                 # K1 maps the stops linearly across the sweep

C_BG   = "#ffffff"
C_IN   = "#333744"   # input impulse
C_PRI  = "#1f77b4"   # primary (division) echoes
C_SEC  = "#e8730c"   # secondary (telephone) echoes
C_GRID = "#e6e6ea"   # quarter grid
C_BEAT = "#c9c9d2"   # beat numbers / axis
C_MID  = "#b9b9c2"   # per-row midline
C_TXT  = "#222222"


def bar(x, y_base, h, w, color, up=True):
    y = y_base - h if up else y_base
    return (f'<rect x="{x - w/2:.1f}" y="{y:.1f}" width="{w}" height="{h:.1f}" '
            f'rx="1.2" fill="{color}"/>')


def _ang(v):
    """Knob value 0..1 -> radians from 12 o'clock, clockwise (270 deg sweep)."""
    return math.radians(-135.0 + v * 270.0)


def _pt(cx, cy, r, v):
    a = _ang(v)
    return cx + r * math.sin(a), cy - r * math.cos(a)


def knob(cx, cy, idx):
    """A small K1 dial with the active 1/COUNT slice highlighted and a pointer to
    its center (idx = stop index; stops map linearly across the sweep)."""
    v_lo, v_hi = idx / DIV_COUNT, (idx + 1) / DIV_COUNT
    vc = (v_lo + v_hi) / 2.0
    p = []
    p.append(f'<circle cx="{cx}" cy="{cy}" r="{KNOB_R}" fill="#f4f4f7" '
             f'stroke="#c9c9d2" stroke-width="1.2"/>')
    # min / noon / max reference ticks
    for v in (0.0, 0.5, 1.0):
        x1, y1 = _pt(cx, cy, KNOB_R + 1, v)
        x2, y2 = _pt(cx, cy, KNOB_R + 4, v)
        p.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
                 f'stroke="{C_BEAT}" stroke-width="1"/>')
    # active-range arc on the rim
    rr = KNOB_R + 3
    x1, y1 = _pt(cx, cy, rr, v_lo)
    x2, y2 = _pt(cx, cy, rr, v_hi)
    large = 1 if (v_hi - v_lo) * 270.0 > 180.0 else 0
    p.append(f'<path d="M {x1:.1f} {y1:.1f} A {rr} {rr} 0 {large} 1 {x2:.1f} {y2:.1f}" '
             f'fill="none" stroke="{C_PRI}" stroke-width="3" stroke-linecap="round"/>')
    # pointer to slice center
    px, py = _pt(cx, cy, KNOB_R - 3, vc)
    p.append(f'<line x1="{cx}" y1="{cy}" x2="{px:.1f}" y2="{py:.1f}" '
             f'stroke="#333744" stroke-width="2" stroke-linecap="round"/>')
    return "".join(p)


def echoes(ratio):
    """Yield (t_beats, amp) for echoes within the window, medium feedback."""
    k, amp = 1, FB
    while True:
        t = k * ratio
        if t > T_MAX or amp < MIN_AMP:
            break
        yield t, amp
        k += 1
        amp *= FB


def main():
    s = []
    s.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" '
             f'height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}" '
             f'font-family="Helvetica,Arial,sans-serif">')
    s.append(f'<rect width="{WIDTH}" height="{HEIGHT}" fill="{C_BG}"/>')

    # Title
    s.append(f'<text x="18" y="28" font-size="17" font-weight="700" '
             f'fill="{C_TXT}">Mnemonic — Edge multitap: clock-division rhythms</text>')
    # Subtitle, wrapped to stay inside the (compact) viewBox width.
    s.append(f'<text x="18" y="47" font-size="11.5" fill="#666">'
             f'input impulse; echoes at medium feedback. primary (division) above '
             f'the line,</text>')
    s.append(f'<text x="18" y="62" font-size="11.5" fill="#666">'
             f'secondary (telephone) below. grid = tapped quarter-note pulse.</text>')

    # Legend
    lx = 18
    ly = 84
    for color, label in ((C_IN, "input"), (C_PRI, "primary echoes"),
                         (C_SEC, "secondary echoes")):
        s.append(f'<rect x="{lx}" y="{ly-9}" width="11" height="11" rx="1.5" fill="{color}"/>')
        s.append(f'<text x="{lx+16}" y="{ly}" font-size="11.5" fill="{C_TXT}">{label}</text>')
        lx += 24 + 8.0 * len(label)

    # Quarter grid + beat numbers (span all rows)
    grid_top = TOP - 6
    grid_bot = TOP + ROWS * ROW_H
    b = 0
    while b <= T_MAX + 0.001:
        x = X0 + b * PX_BEAT
        s.append(f'<line x1="{x:.1f}" y1="{grid_top}" x2="{x:.1f}" y2="{grid_bot}" '
                 f'stroke="{C_GRID}" stroke-width="1"/>')
        s.append(f'<text x="{x:.1f}" y="{grid_bot+18}" font-size="10.5" '
                 f'fill="{C_BEAT}" text-anchor="middle">{b}</text>')
        b += 1
    s.append(f'<text x="{X0 + PLOT_W/2:.1f}" y="{grid_bot+31}" font-size="10.5" '
             f'fill="#888" text-anchor="middle">beats (tapped quarter-notes)</text>')

    # Rows
    for i, (pr, pl, sr, sl) in enumerate(STOPS):
        y_top = TOP + i * ROW_H
        y_mid = y_top + ROW_H / 2

        # row separator + midline
        s.append(f'<line x1="{X0}" y1="{y_mid:.1f}" x2="{X0+PLOT_W}" y2="{y_mid:.1f}" '
                 f'stroke="{C_MID}" stroke-width="0.8"/>')

        # per-row K1 knob (active slice + pointer)
        s.append(knob(KNOB_CX, y_mid, i))

        # labels
        s.append(f'<text x="{LABEL_X}" y="{y_mid-3:.1f}" font-size="12" font-weight="600" '
                 f'fill="{C_PRI}">{pl}</text>')
        s.append(f'<text x="{LABEL_X}" y="{y_mid+13:.1f}" font-size="11" fill="{C_SEC}">'
                 f'+ {sl}</text>')

        # input impulse (spans both halves, distinct color)
        s.append(bar(X0, y_mid, HALF, 5, C_IN, up=True))
        s.append(bar(X0, y_mid, HALF, 5, C_IN, up=False))

        # primary echoes (upward)
        for t, amp in echoes(pr):
            x = X0 + t * PX_BEAT
            s.append(bar(x, y_mid, HALF * amp, 3, C_PRI, up=True))
        # secondary echoes (downward)
        for t, amp in echoes(sr):
            x = X0 + t * PX_BEAT
            s.append(bar(x, y_mid, HALF * amp, 3, C_SEC, up=False))

    s.append('</svg>')

    out = os.path.join(os.path.dirname(__file__), "ChronoTron3", "division-chart.svg")
    with open(out, "w") as f:
        f.write("\n".join(s))
    print("wrote", out)


if __name__ == "__main__":
    main()
