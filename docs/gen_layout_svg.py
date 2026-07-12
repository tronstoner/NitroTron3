#!/usr/bin/env python3
"""Generate per-mode pedal-layout SVG diagrams for the user manual.

Run from repo root:
    python3 docs/gen_layout_svg.py

Writes docs/pedal-mode-{a,b,c}.svg.

Each knob carries: a label (matching the README / USER_MANUAL control tables),
a short range/centre detail line, and a `bi` flag for bipolar knobs. Bipolar
knobs are drawn centred at noon with a red centre-detent tick; unipolar knobs
get a grey tick at their CCW start. A 270° track arc shows the sweep.
Keep the labels here in sync with the control tables when controls change
(see the `update-controls` skill).
"""
import math
from pathlib import Path

DOCS = Path(__file__).resolve().parent

# ---------------------------------------------------------------------------
# Per-mode data. Each knob: {"l": label, "d": range/centre detail, "bi": bipolar}
# ---------------------------------------------------------------------------

MODES = {
    "a": {
        "title": "Mode A — Bordun",
        "sw3_pos": "UP",
        "knobs": [
            {"l": "Semitone", "d": "±12 semi", "bi": True},
            {"l": "Octave", "d": "7 pos · ±3 oct"},
            {"l": "Fine tune", "d": "±50 cents", "bi": True},
            {"l": "Tone / Fold", "d": "80 Hz–8 kHz · TRI fold"},
            {"l": "Osc 2 detune", "d": "±1–12 semi · off@ctr", "bi": True},
            {"l": "Mix", "d": "dry → wet"},
        ],
        "sw1": ("SW1 · Waveform", ["Saw", "Triangle", "Square"]),
        "sw2": ("SW2 · Drone mode", ["Fixed pitch", "Octave-lock", "Direct track"]),
        "sw3": ("SW3 · Mode select", ["Bordun ◀", "Sprawl", "Schism"]),
        "fs1": "FS1 · Preset",
        "fs2": "FS2 · Bypass",
    },
    "b": {
        "title": "Mode B — Sprawl",
        "sw3_pos": "MID",
        "knobs": [
            {"l": "Harmony / shift", "d": "±12 / ±36 semi (SW2)", "bi": True},
            {"l": "Buffer range", "d": "100 ms–8 s · dir=sign", "bi": True},
            {"l": "Character / Glitch", "d": "CCW smear · CW glitch", "bi": True},
            {"l": "Texture amt", "d": "noon = clean (SW1)", "bi": True},
            {"l": "Reverb / Feedback", "d": "CCW rev · CW feedback", "bi": True},
            {"l": "Mix", "d": "dry → wet"},
        ],
        "sw1": ("SW1 · Texture mode", ["Crush / Fold", "Digital glitch", "Ringmod"]),
        "sw2": ("SW2 · Harmony", ["Fixed interval", "Resonance", "Freq shift"]),
        "sw3": ("SW3 · Mode select", ["Bordun", "Sprawl ◀", "Schism"]),
        "fs1": "FS1 · Preset",
        "fs2": "FS2 · Bypass",
    },
    "c": {
        "title": "Mode C — Schism",
        "sw3_pos": "DOWN",
        "knobs": [
            {"l": "Filter · where", "d": "cutoff / vowel / notch"},
            {"l": "Filter · amount", "d": "reso / size / feedbk"},
            {"l": "Env / LFO", "d": "off @ center (SW2)", "bi": True},
            {"l": "Drive character", "d": "noon = clean (SW1)", "bi": True},
            {"l": "Filter drive", "d": "−12 dB · 1× · ×8", "bi": True},
            {"l": "Mix", "d": "dry → wet"},
        ],
        "sw1": ("SW1 · Drive", ["Fold / Cheby", "Crush / Drive", "Synth osc"]),
        "sw2": ("SW2 · Filter", ["Moog ladder", "Grendel formant", "Phaser"]),
        "sw3": ("SW3 · Mode select", ["Bordun", "Sprawl", "Schism ◀"]),
        "fs1": "FS1 · Preset",
        "fs2": "FS2 · Bypass",
    },
}

# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------
# viewBox is sized to roughly match the Hothouse 125B aspect (~0.6 W/H).
# Internal font sizes are tuned so labels stay readable when the SVG is
# rendered at ~95 mm wide in the PDF (see manual.css `.pedal-layout`).

W, H = 600, 1000
COLS_X = [130, 300, 470]  # column centers for K1/K2/K3 etc.
KNOB_R = 52
ARC_R = KNOB_R + 6         # sweep-track arc radius
ARC_DEG = 135              # track spans -ARC_DEG..+ARC_DEG (270°, gap at bottom)

KNOB_ROW1_Y = 180
KNOB_ROW2_Y = 370
SWITCH_Y = 540          # toggle icon center y
SWITCH_LABEL_Y = 610    # "SW1 · Waveform" header y
SWITCH_POS_Y = 638      # first position-label y (UP)
SWITCH_POS_STEP = 27    # vertical step between position labels
LED_Y = 770
FS_Y = 870
FS_R = 52

# Text sizes (in SVG units).
FS_TITLE = 30
FS_SUBTITLE = 16
FS_LEGEND = 14
FS_KNOB_NAME = 22
FS_KNOB_FN = 18
FS_KNOB_DETAIL = 13
FS_SWITCH_LABEL = 22
FS_SWITCH_FN = 17
FS_SWITCH_POS = 15
FS_LED = 16
FS_FS_NAME = 22
FS_FS_FN = 19


def _polar(cx, cy, r, deg):
    """Point on a circle, deg measured from 12 o'clock, clockwise positive."""
    rad = math.radians(deg)
    return cx + math.sin(rad) * r, cy - math.cos(rad) * r


def knob(cx: int, cy: int, kid: str, entry: dict, indicator_deg: float = -135.0) -> str:
    """A knob: dark body + white pointer, a 270° sweep-track arc, and a
    centre-detent marker. Bipolar knobs (`entry['bi']`) are centred at noon
    with a red top tick; unipolar knobs get a grey tick at the CCW start.
    Three label lines below: K-id, function, range/centre detail."""
    bipolar = entry.get("bi", False)
    label = entry["l"]
    detail = entry.get("d", "")
    if bipolar:
        indicator_deg = 0.0  # centred pointer

    x_in, y_in = _polar(0, 0, 14, indicator_deg)
    x_out, y_out = _polar(0, 0, KNOB_R - 8, indicator_deg)

    sx, sy = _polar(cx, cy, ARC_R, -ARC_DEG)
    ex, ey = _polar(cx, cy, ARC_R, ARC_DEG)

    parts = [f"""
  <g transform="translate({cx},{cy})">
    <circle r="{KNOB_R}" fill="#1d1d1d" stroke="#000" stroke-width="1.8"/>
    <circle r="{KNOB_R - 7}" fill="none" stroke="#3a3a3a" stroke-width="1"/>
    <line x1="{x_in:.1f}" y1="{y_in:.1f}" x2="{x_out:.1f}" y2="{y_out:.1f}"
          stroke="#ffffff" stroke-width="4" stroke-linecap="round"/>
  </g>
  <path d="M {sx:.1f} {sy:.1f} A {ARC_R} {ARC_R} 0 1 1 {ex:.1f} {ey:.1f}"
        fill="none" stroke="#aeaeae" stroke-width="2.5"/>"""]

    if bipolar:
        t1x, t1y = _polar(cx, cy, KNOB_R + 2, 0)
        t2x, t2y = _polar(cx, cy, KNOB_R + 13, 0)
        parts.append(f"""
  <line x1="{t1x:.1f}" y1="{t1y:.1f}" x2="{t2x:.1f}" y2="{t2y:.1f}"
        stroke="#c83232" stroke-width="3.5" stroke-linecap="round"/>""")
    else:
        t1x, t1y = _polar(cx, cy, KNOB_R + 2, -ARC_DEG)
        t2x, t2y = _polar(cx, cy, KNOB_R + 11, -ARC_DEG)
        parts.append(f"""
  <line x1="{t1x:.1f}" y1="{t1y:.1f}" x2="{t2x:.1f}" y2="{t2y:.1f}"
        stroke="#9a9a9a" stroke-width="2" stroke-linecap="round"/>""")

    name_y = cy + KNOB_R + 24
    fn_y = name_y + 21
    detail_y = fn_y + 16
    parts.append(f"""
  <text x="{cx}" y="{name_y}" text-anchor="middle"
        font-size="{FS_KNOB_NAME}" font-weight="700" fill="#111">{kid}</text>
  <text x="{cx}" y="{fn_y}" text-anchor="middle"
        font-size="{FS_KNOB_FN}" fill="#333">{label}</text>""")
    if detail:
        parts.append(f"""
  <text x="{cx}" y="{detail_y}" text-anchor="middle"
        font-size="{FS_KNOB_DETAIL}" fill="#777">{detail}</text>""")
    return "".join(parts)


def switch(cx: int, cy: int, sw_label: str, positions: list) -> str:
    """Toggle switch icon + per-position labels stacked below.

    ``sw_label`` may be in the form ``"SW1 · Waveform"`` — the two halves
    get split across two short lines so headers don't collide across
    columns at the narrow viewBox width."""
    if " · " in sw_label:
        sw_id, sw_fn = sw_label.split(" · ", 1)
    else:
        sw_id, sw_fn = sw_label, ""

    base = f"""
  <g transform="translate({cx},{cy})">
    <rect x="-16" y="-26" width="32" height="52" rx="4"
          fill="#9aa0a4" stroke="#333" stroke-width="1.5"/>
    <rect x="-6" y="-34" width="12" height="22" rx="2"
          fill="#cfd2d5" stroke="#222" stroke-width="1.2"/>
    <circle cx="0" cy="-36" r="5" fill="#888" stroke="#222" stroke-width="1"/>
  </g>
  <text x="{cx}" y="{SWITCH_LABEL_Y}" text-anchor="middle"
        font-size="{FS_SWITCH_LABEL}" font-weight="700" fill="#111">{sw_id}</text>
  <text x="{cx}" y="{SWITCH_LABEL_Y + FS_SWITCH_FN + 6}" text-anchor="middle"
        font-size="{FS_SWITCH_FN}" fill="#444">{sw_fn}</text>"""

    markers = ["▲", "●", "▼"]
    pos_names = ["UP", "MID", "DOWN"]
    rows = []
    pos_y0 = SWITCH_LABEL_Y + FS_SWITCH_FN + FS_SWITCH_POS + 16
    for i, (marker, pos_name, body) in enumerate(zip(markers, pos_names, positions)):
        y = pos_y0 + i * SWITCH_POS_STEP
        rows.append(
            f"""
  <text x="{cx}" y="{y}" text-anchor="middle" font-size="{FS_SWITCH_POS}" fill="#222">
    <tspan font-weight="700">{marker} {pos_name}</tspan>
    <tspan dx="6" fill="#444">{body}</tspan>
  </text>"""
        )
    return base + "".join(rows)


def footswitch(cx: int, cy: int, name: str, fn_text: str) -> str:
    return f"""
  <g transform="translate({cx},{cy})">
    <circle r="{FS_R}" fill="#3a3a3a" stroke="#111" stroke-width="1.8"/>
    <circle r="{FS_R - 12}" fill="#d8d8d8" stroke="#111" stroke-width="1.4"/>
    <circle r="7" fill="#666"/>
  </g>
  <text x="{cx}" y="{cy + FS_R + FS_FS_NAME}" text-anchor="middle"
        font-size="{FS_FS_NAME}" font-weight="700" fill="#111">{name}</text>
  <text x="{cx}" y="{cy + FS_R + FS_FS_NAME + FS_FS_FN + 4}" text-anchor="middle"
        font-size="{FS_FS_FN}" fill="#333">{fn_text}</text>"""


def led(cx: int, cy: int, label: str) -> str:
    return f"""
  <circle cx="{cx}" cy="{cy}" r="9" fill="#c83232" stroke="#5c1414" stroke-width="1"/>
  <text x="{cx + 18}" y="{cy + 5}" font-size="{FS_LED}" fill="#333">{label}</text>"""


# ---------------------------------------------------------------------------
# Per-mode indicator-angle hints for UNIPOLAR knobs (cosmetic — gives each knob
# a slightly different angle so the diagram doesn't look mechanically uniform).
# Bipolar knobs ignore these and point to noon.
# ---------------------------------------------------------------------------

ANGLES = {
    "a": [-90, -30, 0, 60, 0, 120],
    "b": [0, 90, -60, 30, 0, 90],
    "c": [120, 60, 0, -45, 90, 100],
}


def build_svg(mode_key: str) -> str:
    m = MODES[mode_key]
    angles = ANGLES[mode_key]

    parts = [
        f'<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" '
        f'font-family="Helvetica Neue, Helvetica, Arial, sans-serif">',
        f'  <rect x="14" y="14" width="{W - 28}" height="{H - 28}" rx="40" '
        f'fill="#f6f6f6" stroke="#444" stroke-width="2.5"/>',
        f'  <text x="{W // 2}" y="64" text-anchor="middle" '
        f'font-size="{FS_TITLE}" font-weight="700" fill="#111">{m["title"]}</text>',
        f'  <text x="{W // 2}" y="92" text-anchor="middle" '
        f'font-size="{FS_SUBTITLE}" fill="#666">NitroTron3 · Hothouse pedal layout</text>',
        # legend for the knob markers
        f'  <text x="{W // 2}" y="116" text-anchor="middle" font-size="{FS_LEGEND}" '
        f'fill="#888"><tspan fill="#c83232" font-weight="700">|</tspan> red top tick = '
        f'bipolar (centre detent at noon) · grey tick = CCW start</text>',
    ]

    for i in range(3):
        parts.append(
            knob(COLS_X[i], KNOB_ROW1_Y, f"K{i + 1}", m["knobs"][i], angles[i])
        )
    for i in range(3):
        parts.append(
            knob(COLS_X[i], KNOB_ROW2_Y, f"K{i + 4}", m["knobs"][i + 3], angles[i + 3])
        )

    sw_label, sw_positions = m["sw1"]
    parts.append(switch(COLS_X[0], SWITCH_Y, sw_label, sw_positions))
    sw_label, sw_positions = m["sw2"]
    parts.append(switch(COLS_X[1], SWITCH_Y, sw_label, sw_positions))
    sw_label, sw_positions = m["sw3"]
    parts.append(switch(COLS_X[2], SWITCH_Y, sw_label, sw_positions))

    parts.append(led(195, LED_Y, "Preset"))
    parts.append(led(395, LED_Y, "State"))

    parts.append(footswitch(170, FS_Y, "FS1", m["fs1"].split(" · ", 1)[1]))
    parts.append(footswitch(430, FS_Y, "FS2", m["fs2"].split(" · ", 1)[1]))

    parts.append("</svg>\n")
    return "\n".join(parts)


def main() -> None:
    for key in MODES:
        out = DOCS / f"pedal-mode-{key}.svg"
        out.write_text(build_svg(key), encoding="utf-8")
        print(f"wrote {out.relative_to(DOCS.parent)}")


if __name__ == "__main__":
    main()
