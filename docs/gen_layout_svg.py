#!/usr/bin/env python3
"""Generate per-mode pedal-layout SVG diagrams for the user manual.

Run from repo root:
    python3 docs/gen_layout_svg.py

Writes docs/pedal-mode-{a,b,c}.svg.
"""
from pathlib import Path

DOCS = Path(__file__).resolve().parent

# ---------------------------------------------------------------------------
# Per-mode data
# ---------------------------------------------------------------------------

MODES = {
    "a": {
        "title": "Mode A — Bordun",
        "sw3_pos": "UP",
        "knobs": [
            "Semitone / Interval",
            "Octave",
            "Fine tune",
            "Tone / Wavefold",
            "Osc 2 detune",
            "Mix",
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
            "Interval (±24 st)",
            "Buffer range",
            "Character / Glitch",
            "Texture amount",
            "Reverb ◀ ▶ Feedback",
            "Mix",
        ],
        "sw1": ("SW1 · Texture mode", ["Crush / Fold", "Digital glitch", "Ringmod"]),
        "sw2": ("SW2 · Harmony", ["Fixed interval", "Resonance", "Resonance"]),
        "sw3": ("SW3 · Mode select", ["Bordun", "Sprawl ◀", "Schism"]),
        "fs1": "FS1 · Preset",
        "fs2": "FS2 · Bypass",
    },
    "c": {
        "title": "Mode C — Schism",
        "sw3_pos": "DOWN",
        "knobs": [
            "Filter “where”",
            "Filter “how much”",
            "Env → filter",
            "Drive character",
            "Wet level",
            "Mix",
        ],
        "sw1": ("SW1 · Drive", ["Sine wavefold", "TBD (passthru)", "Passthrough"]),
        "sw2": ("SW2 · Filter", ["Moog ladder", "Grendel formant", "Plague"]),
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
KNOB_R = 58

KNOB_ROW1_Y = 170
KNOB_ROW2_Y = 360
SWITCH_Y = 540          # toggle icon center y
SWITCH_LABEL_Y = 610    # "SW1 · Waveform" header y
SWITCH_POS_Y = 638      # first position-label y (UP)
SWITCH_POS_STEP = 27    # vertical step between position labels
LED_Y = 770
FS_Y = 870
FS_R = 52

# Text sizes (in SVG units). At 95mm rendered width with viewBox W=600,
# 1 SVG unit ≈ 0.158 mm, so 20 units ≈ 3.2 mm ≈ 9 pt.
FS_TITLE = 30
FS_SUBTITLE = 16
FS_KNOB_NAME = 22
FS_KNOB_FN = 19
FS_SWITCH_LABEL = 22  # "SW1" line (above function name)
FS_SWITCH_FN = 17     # function name below the SW id ("Waveform" etc.)
FS_SWITCH_POS = 15    # ▲ UP / ● MID / ▼ DOWN rows
FS_LED = 16
FS_FS_NAME = 22
FS_FS_FN = 19


def knob(cx: int, cy: int, name: str, fn_text: str, indicator_deg: float = -135.0) -> str:
    """A knob: dark body with a white tick. `indicator_deg` rotates the tick
    (0° = pointing up, positive = clockwise)."""
    import math

    rad = math.radians(indicator_deg)
    x_inner = math.sin(rad) * 14
    y_inner = -math.cos(rad) * 14
    x_outer = math.sin(rad) * (KNOB_R - 8)
    y_outer = -math.cos(rad) * (KNOB_R - 8)
    return f"""
  <g transform="translate({cx},{cy})">
    <circle r="{KNOB_R}" fill="#1d1d1d" stroke="#000" stroke-width="1.8"/>
    <circle r="{KNOB_R - 7}" fill="none" stroke="#3a3a3a" stroke-width="1"/>
    <line x1="{x_inner:.1f}" y1="{y_inner:.1f}" x2="{x_outer:.1f}" y2="{y_outer:.1f}"
          stroke="#ffffff" stroke-width="4" stroke-linecap="round"/>
  </g>
  <text x="{cx}" y="{cy + KNOB_R + FS_KNOB_NAME + 4}" text-anchor="middle"
        font-size="{FS_KNOB_NAME}" font-weight="700" fill="#111">{name}</text>
  <text x="{cx}" y="{cy + KNOB_R + FS_KNOB_NAME + FS_KNOB_FN + 8}" text-anchor="middle"
        font-size="{FS_KNOB_FN}" fill="#333">{fn_text}</text>"""


def switch(cx: int, cy: int, sw_label: str, positions: list[str]) -> str:
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
    # Push position rows below the two-line header.
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
# Per-mode indicator-angle hints (purely cosmetic — gives each knob a slightly
# different angle so the diagram doesn't look mechanically uniform).
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
        # pedal body
        f'  <rect x="14" y="14" width="{W - 28}" height="{H - 28}" rx="40" '
        f'fill="#f6f6f6" stroke="#444" stroke-width="2.5"/>',
        # title bar
        f'  <text x="{W // 2}" y="64" text-anchor="middle" '
        f'font-size="{FS_TITLE}" font-weight="700" fill="#111">{m["title"]}</text>',
        f'  <text x="{W // 2}" y="92" text-anchor="middle" '
        f'font-size="{FS_SUBTITLE}" fill="#666">NitroTron3 · Hothouse pedal layout</text>',
    ]

    # Knobs row 1 (K1-K3)
    for i in range(3):
        parts.append(
            knob(COLS_X[i], KNOB_ROW1_Y, f"K{i + 1}", m["knobs"][i], angles[i])
        )

    # Knobs row 2 (K4-K6)
    for i in range(3):
        parts.append(
            knob(
                COLS_X[i], KNOB_ROW2_Y, f"K{i + 4}", m["knobs"][i + 3], angles[i + 3]
            )
        )

    # Switches (SW1, SW2, SW3)
    sw_label, sw_positions = m["sw1"]
    parts.append(switch(COLS_X[0], SWITCH_Y, sw_label, sw_positions))
    sw_label, sw_positions = m["sw2"]
    parts.append(switch(COLS_X[1], SWITCH_Y, sw_label, sw_positions))
    sw_label, sw_positions = m["sw3"]
    parts.append(switch(COLS_X[2], SWITCH_Y, sw_label, sw_positions))

    # LEDs (positioned above the footswitches)
    parts.append(led(195, LED_Y, "Preset"))
    parts.append(led(395, LED_Y, "State"))

    # Footswitches
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
