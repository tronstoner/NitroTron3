#!/usr/bin/env python3
"""Generate the knob-direction icons used in the user manual.

Shared visual language: a grey sweep track (the ~300° a pot rotates, 7→5 o'clock
with the usual gap at the bottom) with a black overlay showing what the control
does.

  ccw   : black arc 12→7 o'clock (down the left) with an arrowhead at 7
  cw    : black arc 12→5 o'clock (down the right) with an arrowhead at 5
  noon  : a centre indicator bar at 12 on a continuous grey track (centre detent)
  uni   : a single black arc over the range, arrowhead at the max (5 o'c) end;
          the start (base) end is shortened so the flat base bar sits level with
          the arrowhead's body instead of hanging lower than the thin tip
          (unipolar knobs)
  steps : evenly-spaced tick marks along the grey track (discrete/stepped knobs
          that select fixed values or presets)
  bipolar: base bar at noon with black arcs + arrowheads to BOTH ends — marks a
          knob as two-directional (distinct from the noon icon, which describes
          what happens at the centre position)

Details that matter for clean rendering:
  * the grey track stops at the *base* of each arrowhead, so no grey shines
    through the triangle;
  * the black arc overruns a few degrees into the arrowhead (OVERLAP_DEG) so
    anti-aliasing can't leave a light hairline at the arc/arrowhead join;
  * sharp (butt) line ends, no round caps.

Output is viewBox-only SVG, trimmed tight so the icons sit inline (~1.25em).
Never hand-edit the .svg files — edit this and re-run.
"""
import math

CX = CY = 50.0
ARC_R = 30.0        # sweep-track / arc radius
KNOB_R = 13.0       # grey knob circle (small — emphasis is on the arrow)
STROKE = 7.5        # arc / track stroke width (bold)

HEAD_HALF = 9.0     # main arrowhead half-width (radial)
HEAD_DEG = 26.0     # main arrowhead angular length along the arc
OVERLAP_DEG = 5.0   # arc overrun into the arrowhead (kills the AA seam)
STEP_N = 5          # tick marks on the discrete/stepped icon
STEP_HALF = 6.0     # step-tick half-width (radial)

MIN_DEG = -150.0    # 7 o'clock (track start)
MAX_DEG = 150.0     # 5 o'clock (track end)

KNOB_FILL = "#b8b8b8"
INK = "#1b1b1b"

_R = ARC_R + HEAD_HALF + 2.0
VB = f"{CX - _R:.1f} {CY - _R:.1f} {2 * _R:.1f} {2 * _R:.1f}"


def pt(r, deg):
    """Point at clock angle `deg` (0 = 12 o'clock, +clockwise), radius r."""
    a = math.radians(deg)
    return (CX + r * math.sin(a), CY - r * math.cos(a))


def arc(a_deg, b_deg, color):
    """Stroked arc from a_deg to b_deg the short/natural way (increasing angle)."""
    lo, hi = (a_deg, b_deg) if a_deg <= b_deg else (b_deg, a_deg)
    large = 1 if (hi - lo) > 180.0 else 0
    s, e = pt(ARC_R, lo), pt(ARC_R, hi)
    return (f'<path d="M {s[0]:.2f} {s[1]:.2f} A {ARC_R} {ARC_R} 0 {large} 1 '
            f'{e[0]:.2f} {e[1]:.2f}" fill="none" stroke="{color}" '
            f'stroke-width="{STROKE}" stroke-linecap="butt"/>')


def head(tip_deg, base_deg, half):
    tip = pt(ARC_R, tip_deg)
    c1 = pt(ARC_R - half, base_deg)
    c2 = pt(ARC_R + half, base_deg)
    return (f'<polygon points="{tip[0]:.2f} {tip[1]:.2f} {c1[0]:.2f} {c1[1]:.2f} '
            f'{c2[0]:.2f} {c2[1]:.2f}" fill="{INK}"/>')


def tick(deg, half=HEAD_HALF):
    """Arrow-base bar, a radial segment on the track at `deg`."""
    a, b = pt(ARC_R - half, deg), pt(ARC_R + half, deg)
    return (f'<line x1="{a[0]:.2f}" y1="{a[1]:.2f}" x2="{b[0]:.2f}" y2="{b[1]:.2f}" '
            f'stroke="{INK}" stroke-width="{STROKE}" stroke-linecap="butt"/>')


def knob():
    return f'<circle cx="{CX}" cy="{CY}" r="{KNOB_R}" fill="{KNOB_FILL}"/>'


def steps_icon():
    """Discrete / stepped knob: evenly-spaced step ticks along the grey track."""
    span = (MAX_DEG - MIN_DEG) / (STEP_N - 1)
    ticks = "".join(tick(MIN_DEG + i * span, half=STEP_HALF) for i in range(STEP_N))
    return svg(arc(MIN_DEG, MAX_DEG, KNOB_FILL) + knob() + ticks)


def bipolar_icon():
    """Bipolar knob — turns both ways from centre: base bar at noon, black arcs
    to both ends with an arrowhead at each. (noon-icon = behaviour AT centre;
    this = the knob is two-directional.)"""
    ccw_base = MIN_DEG + HEAD_DEG
    cw_base = MAX_DEG - HEAD_DEG
    parts = [
        knob(),
        arc(ccw_base - OVERLAP_DEG, 0.0, INK),      # noon → 7 o'c
        arc(0.0, cw_base + OVERLAP_DEG, INK),       # noon → 5 o'c
        tick(0.0),                                  # centre base bar
        head(MIN_DEG, ccw_base, HEAD_HALF),
        head(MAX_DEG, cw_base, HEAD_HALF),
    ]
    return svg("".join(parts))


def arc_icon(going_cw):
    go = 1.0 if going_cw else -1.0
    tip_deg = MAX_DEG if going_cw else MIN_DEG      # 5 o'c / 7 o'c
    base_deg = tip_deg - go * HEAD_DEG              # arrowhead base (toward noon)
    arc_end = base_deg + go * OVERLAP_DEG           # overrun into the arrowhead
    grey_far = MIN_DEG if going_cw else MAX_DEG     # unswept extreme
    parts = [
        arc(base_deg, grey_far, KNOB_FILL),         # grey: base of head → far end
        knob(),
        arc(0.0, arc_end, INK),                     # black active arc from noon
        tick(0.0),                                  # arrow-base bar at noon
        head(tip_deg, base_deg, HEAD_HALF),
    ]
    return svg("".join(parts))


def noon_icon():
    parts = [
        arc(MIN_DEG, MAX_DEG, KNOB_FILL),           # grey track, continuous
        knob(),
        tick(0.0),                                  # centre indicator bar at noon
    ]
    return svg("".join(parts))


def uni_icon():
    # Shorten the start (opposite the arrowhead) so the base bar sits level with
    # the arrowhead's body — mirror the arrowhead base angle across the vertical.
    head_base = MAX_DEG - HEAD_DEG                   # arrowhead base near 5 o'c
    start_deg = -head_base                           # base bar, mirrored
    arc_end = head_base + OVERLAP_DEG
    parts = [
        knob(),
        arc(start_deg, arc_end, INK),               # sweep, shortened at the base end
        tick(start_deg),                            # arrow-base bar
        head(MAX_DEG, head_base, HEAD_HALF),
    ]
    return svg("".join(parts))


def svg(body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{VB}" '
            f'role="img">{body}</svg>\n')


ICONS = {
    "icon-ccw.svg": arc_icon(going_cw=False),
    "icon-cw.svg": arc_icon(going_cw=True),
    "icon-noon.svg": noon_icon(),
    "icon-uni.svg": uni_icon(),
    "icon-steps.svg": steps_icon(),
    "icon-bipolar.svg": bipolar_icon(),
}

if __name__ == "__main__":
    import os
    assets = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
    os.makedirs(assets, exist_ok=True)
    for name, data in ICONS.items():
        with open(os.path.join(assets, name), "w") as fh:
            fh.write(data)
        print("wrote assets/" + name)
