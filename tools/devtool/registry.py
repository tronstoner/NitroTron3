"""Live-constant registry from the constants files (docs/DEV_INTERFACE.md section 4).

Python port of tools/hostsim/const_docs.h, plus the `@` tags of
src/core/util/tunable.h. The constants files are the single source of truth;
nothing here is hand-kept.

Per `TUNABLE(type, NAME, value);` line:
  section     nearest preceding `// --- Title ---` header (dashes stripped)
  doc         the comment block directly above the declaration (contiguous `//`
              lines up to a blank line, a code line or a section header).
              Consecutive declarations (TUNABLE or plain constexpr) with no
              comment between them share the block above the first of them;
              doc_of names that first constant.
  note        trailing inline comment after the `@` tags, plus continuation
              lines below whose comment text itself starts with `//`.
  tags        @unit <u>  @range <min> <max>  @taper lin|log  @level live|reactivate
              @pair <ID> <end>   (optional; see PAIRS below)
              @target <id>       (optional; what the constant acts on, one of TARGETS: the page's colour;
                                  default = the section's `// @target` directive, else none)
              @states <off> <on> (optional, bool: the words for false / true, e.g. `@states LFO env`)
              @below <NAME>      (optional ordering: this value must stay below NAME's current value - the code
                                  divides by their difference / log ratio; not a range: devtool refuses a set
                                  that breaks it, with the reason)
              @why <reason>      (with @range: why the range ends where it does - the engine limit; free text
                                  up to the next ` @` tag or a double space; the page shows "Range: a..b - <why>")
              (written first in the inline comment, before the note text)
  label       readable label from the note's first clause (parentheses and a
              leading unit like `Hz;` removed), else the doc's first sentence,
              else the name.

PAIRS: constants that are the ends of one law share `@pair <ID> <end>` and are
shown as ONE row (one multi-handle slider in their common unit / taper).
  ID   the row label, `_` = space (e.g. `Pluck_decay_time`)
  end  where the constant acts: knob position `ccw`, `noon`, `cw` (a knob-end
       pair: the values the knob law reaches there), or the ends of a value range
       that is not a knob law: `floor` / `top`, `fast` / `slow` (a "range" row).
  A pair needs >= 2 members, distinct ends, one kind (knob or range), the same
  type / unit / taper; members are ordered by END_ORDER.

PLACES (where a constant acts on the pedal; the page's tree): LAYOUT below is
the ONE list of display names (pedal -> benches -> modes). In the constants
files:
  // @section <Title>     comment directive: the display section (what it does +
                          which knob, e.g. `Touch env · K4`) of the following
                          declarations, until the next `// --- ---` header
  // @target <id>        comment directive: default target (TARGETS) of the following declarations
  // @view faders         comment directive: the section's single number constants are shown side by side as
                          vertical faders (a mixer row, for comparable values) instead of knobs
  // @at 2/fm,2/wt        comment directive: default places of the following
                          declarations (same scope); `<bench id>/<mode id>`
  @at 2/fm,3/2op          per-declaration tag (overrides the directive)
  c["places"] = [{key: "<bench>/<mode>/<section>", bench, mode, section}], in
  LAYOUT order. A constant without places (no @at) shows under its module /
  section as before (pedals without a LAYOUT entry use that tree only).

FAMILIES (per-preset tunables declared by a macro, explorer_tables.h):
  // @family <PREFIX> <ROW_MACRO> <LIST_MACRO>
  // @field <SUFFIX> <float|int|bool> <tags>  <note>     (one per field, in the
                                                         ROW_MACRO argument order)
  The LIST_MACRO's `#define` body lists `X(<index>, <NAME>)`; the table rows
  `{ "<NAME>", <KIND>, ..., ROW_MACRO(v0, v1, ...) }` give the defaults. Each
  preset NAME x field gives one constant <PREFIX><NAME>_<SUFFIX> (file / line of
  the preset's table row: where its default is written), with c["preset"] =
  NAME, c["preset_index"] = index; a field's @pair id becomes <NAME>_<id>.
  `@inert <KIND> <words_with_underscores>` on a @field: on presets whose row
  has that <KIND> token the field does nothing; c["inert"] = the words (else
  None) and the page shows that text instead of a control.

Python 3 stdlib only.
"""
import os
import re

SECTION_RE = re.compile(r"^-{3,}")
TUNABLE_RE = re.compile(r"^\s*TUNABLE\(\s*(float|int|bool)\s*,\s*(\w+)\s*,\s*(.*?)\s*\)\s*;(.*)$")
CONSTEXPR_RE = re.compile(r"^\s*(?:static\s+)?constexpr\s+[\w:]+\s+(\w+)\b(.*)$")
TAG_RE = re.compile(r"@(\w+)((?:\s+(?!@)\S+)*)")
LABEL_UNITS = {"hz", "s", "ms", "db", "dbfs", "bits", "oct", "rad", "x", "t4 units"}
LEVELS = ("live", "reactivate")
END_ORDER = ("ccw", "noon", "cw", "floor", "top", "fast", "slow")
END_LABELS = {"ccw": "CCW", "noon": "noon", "cw": "CW", "floor": "floor", "top": "top",
              "fast": "fastest env", "slow": "slowest env"}
KNOB_ENDS = ("ccw", "noon", "cw")
TAPERS = ("lin", "log")
LABEL_MAX = 64
FAMILY_RE = re.compile(r"^@family\s+(\w+)\s+(\w+)\s+(\w+)\s*$")
FIELD_RE = re.compile(r"^@field\s+(\w+)\s+(float|int|bool)\s+(.*)$")
DIRECTIVE_RE = re.compile(r"^@(section|at|target|view)\s+(.*)$")
VIEWS = ("knobs", "faders")
ROW_RE = re.compile(r'^\s*\{\s*"(\w+)".*\b(\w+)\(([^()]*)\)\s*\},?\s*(//.*)?$')
ROW_KIND_RE = re.compile(r'^\s*\{\s*"\w+"\s*,\s*(\w+)')
LIST_ITEM_RE = re.compile(r"^\s*X\(\s*(\d+)\s*,\s*(\w+)\s*\)\s*\\?\s*$")

# TARGETS: what a constant / knob acts on. The page colours everything by it (knob-strip cards, section
# headers, preset-grid columns, knob arcs, faders); the colours themselves are CSS variables --t-<id>
# (web/app.css, light + dark). This list is the ONE place target ids and their legend names are defined.
TARGETS = [
    {"id": "timbre", "name": "Timbre"},
    {"id": "filter", "name": "Filter"},
    {"id": "pitch", "name": "Pitch \u00b7 LFO"},
    {"id": "env", "name": "Touch env"},
    {"id": "lofi", "name": "Lofi \u00b7 level"},
    {"id": "select", "name": "Selection"},
]
TARGET_IDS = [t["id"] for t in TARGETS]


def _k(k, job, kind, target, show):
    """One knob card of a mode's knob strip. kind: uni | bipolar | steps (the manual icons);
    target: TARGETS id ("" = unused / inert); show: the live value template (web/core.js knobShow):
    {k} knob position, {sel} the selection (preset / ratio / bank / zone / model), {env} touch env (pluck /
    swell time), {lfo} LFO shape + rate, {num:<tok>} the number after <tok>, {hz:<tok>} a frequency,
    {word:<tok>} / {word2:<tok>} the first / second word after <tok>; text outside {} is kept. A
    placeholder the state line lacks -> the knob position."""
    return {"k": k, "job": job, "kind": kind, "target": target, "show": show}


ENV_K4 = _k(4, "Touch env", "bipolar", "env", "{env}")
LFO_K5 = _k(5, "LFO", "bipolar", "pitch", "{lfo}")

# The ONE list of display names for the page's tree (benches = SW3 positions,
# modes = SW1 positions). Ids are what the constants files' @at tags use.
# knobs: the mode's knob strip (control map: the header comment of pedals/nitrolab/modules/explorer.h).
LAYOUT = {
    "nitrolab": {
        "title": "NitroLab",
        "benches": [
            {"id": "1", "name": "Bench 1 \u00b7 Armitage", "sw3": "UP", "modes": []},
            {"id": "2", "name": "Bench 2 \u00b7 Explorer", "sw3": "MID", "modes": [
                {"id": "fm", "name": "FM", "sw1": "UP", "knobs": [
                    _k(1, "Ratio", "steps", "select", "{sel}"),
                    _k(2, "Feedback", "uni", "timbre", "b {num:b}"),
                    _k(3, "Index", "uni", "timbre", "{hz:fce}"),
                    ENV_K4, LFO_K5]},
                {"id": "wt", "name": "Wavetable", "sw1": "MID", "knobs": [
                    _k(1, "Preset", "steps", "select", "{sel}"),
                    _k(2, "Timbre", "uni", "timbre", "{k}"),
                    _k(3, "Filter", "uni", "filter", "{hz:eff}"),
                    ENV_K4, _k(5, "LFO", "bipolar", "pitch", "{lfo}")]},
                {"id": "shift", "name": "Harmonics & Shift", "sw1": "DOWN", "knobs": [
                    _k(1, "Model", "steps", "select", "{sel}"),
                    _k(2, "Harmonic / shift / interval", "uni", "pitch", "{k}"),
                    _k(3, "Blend / scale / detune", "uni", "timbre", "{k}"),
                    _k(4, "Unused", "uni", "", ""),
                    _k(5, "Unused", "uni", "", "")]}]},
            {"id": "3", "name": "Bench 3 \u00b7 FM Lab", "sw3": "DOWN", "modes": [
                {"id": "2op", "name": "2-OP FM", "sw1": "UP", "knobs": [
                    _k(1, "Ratio mode", "steps", "select", "{sel}"),
                    _k(2, "Ratio", "steps", "timbre", "r {num:r}"),
                    _k(3, "Character", "bipolar", "timbre", "{word2:r} {hz:fc}"),
                    ENV_K4, LFO_K5]},
                {"id": "spectral", "name": "Spectral", "sw1": "MID", "knobs": [
                    _k(1, "Bank", "steps", "select", "{sel}"),
                    _k(2, "Density", "uni", "timbre", "\u03bd {num:nu}"),
                    _k(3, "Lowpass", "uni", "filter", "{hz:lpf}"),
                    ENV_K4,
                    _k(5, "Sub level", "uni", "lofi", "{num:sub}")]},
                {"id": "additive", "name": "Additive", "sw1": "DOWN", "knobs": [
                    _k(1, "Zone", "steps", "select", "{sel}"),
                    _k(2, "Harmonics / index / unison", "uni", "timbre", "{k}"),
                    _k(3, "Index / harmonics", "uni", "timbre", "{k}"),
                    ENV_K4,
                    _k(5, "LFO / beat speed", "bipolar", "pitch", "{k}")]}]},
        ],
    },
}


def layout_places(layout):
    """{'2/wt': (bench_index, mode_index), ...} for one pedal's layout."""
    out = {}
    for bi, b in enumerate((layout or {}).get("benches", [])):
        for mi, m in enumerate(b["modes"]):
            out["%s/%s" % (b["id"], m["id"])] = (bi, mi)
    return out


def comment_text(line):
    """'  // text' -> 'text' (one space after // removed); None if not a pure comment line."""
    t = line.strip()
    if not t.startswith("//"):
        return None
    r = t[2:]
    if r.startswith(" "):
        r = r[1:]
    return r.rstrip()


def section_title(text):
    if not SECTION_RE.match(text):
        return None
    return text.strip().strip("-").strip()


def parse_tags(comment):
    """'@unit Hz @range 1 20 @taper log  note' -> ({unit: 'Hz', range: [1, 20], ...}, 'note')."""
    tags = {}
    rest = comment.strip()
    while rest.startswith("@"):
        wm = re.match(r"@why\s+(.*?)(?=\s@\w|\s{2,}|$)", rest)
        if wm:                               # free text: up to the next ` @tag` or a double space
            tags["why"] = [wm.group(1).strip()]
            rest = rest[wm.end():].strip()
            continue
        m = TAG_RE.match(rest)
        if not m:
            break
        key, vals = m.group(1), m.group(2).split()
        # a tag takes a fixed number of values; anything after is note text
        n = 2 if key in ("range", "pair", "states", "inert") else 1
        tags[key] = vals[:n]
        tail = vals[n:]
        rest = (" ".join(tail) + rest[m.end():]).strip() if tail else rest[m.end():].strip()
    return tags, rest


def strip_parens(s):
    out, depth = [], 0
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    return "".join(out)


# Short display labels (the page's row titles). The constant's comments stay the explanation (info popover);
# a name missing here falls back to make_label(). Keep them short: what it is, not how it works.
LABELS = {
    "EXP_TB_LOFI_ENABLE": "Lofi master", "EXP_TB_LOFI_SUM_BITS": "Output bits", "EXP_TB_LOFI_SUM_RATE_HZ": "Output rate",
    "EXP_TB_LOFI_TABLE_BITS": "Table bits", "EXP_TB_LOFI_TABLE_INTERP": "Table steps",
    "EXP_PS_SLIDE_MS": "Knob glide", "EXP_PS_DEC_FAST_S": "Pluck, fastest", "EXP_PS_DEC_SLOW_S": "Pluck, slowest",
    "EXP_PS_RISE_FAST_S": "Swell, fastest", "EXP_PS_RISE_SLOW_S": "Swell, slowest",
    "EXP_PS_DEC_2POLE": "2-pole pluck", "EXP_PS_DEC_2P_TAUS": "2-pole steepness", "EXP_PS_DEC_LINEAR": "Linear pluck",
    "EXP_LFO_DEPTH_FRAC": "Depth fade-in", "EXP_LFO_HZ_MIN": "Slowest", "EXP_LFO_HZ_MAX": "Fastest CW",
    "EXP_LFO_HZ_MAX_CCW": "Fastest CCW", "EXP_LFO_INDEX_SWING": "FM index swing", "EXP_LFO_FM_FC_OCT": "FM cutoff, swell side",
    "EXP_LFO_FM_FC_OCT_DEC": "FM cutoff, pluck side", "EXP_LFO_FB_SWING": "FM feedback swing",
    "EXP_SPEC_ENV_DEPTH_MIN": "Travel, fast env", "EXP_SPEC_ENV_DEPTH_MAX": "Travel, slow env",
    "EXP_SPEC_MORPH_ZONE": "Noon blend zone", "EXP_SPEC_DYN_FLOOR_DB": "Level floor", "EXP_SPEC_DYN_TOP_DB": "Level top",
    "EXP_SPEC_DYN_GATE_DB": "Swell gate", "EXP_SPEC_DYN_REL_FRAC": "Mute damping", "EXP_ENV_TRAVEL_TAUS": "Swell curve",
}
FIELD_LABELS = {"TIMBRE": "Timbre ← LFO", "TIMBRE_ENV": "Timbre ← env", "STEPPED": "K2 stepped",
                "FILTER": "Filter ← LFO, pluck", "FILTER_SWELL": "Filter ← LFO, swell", "FILTER_ENV": "Filter ← env",
                "RES": "Resonance (Q)", "FILTER_TOUCH": "Touch env → filter",
                "PITCH": "Pitch ← LFO", "PITCH_ENV": "Pitch ← env",
                "RATE_CCW": "LFO max, CCW", "RATE_CW": "LFO max, CW", "LOFI": "Lofi engine"}


def make_label(name, note, doc, unit):
    def clean(s):
        s = strip_parens(s)
        s = re.sub(r"\s+", " ", s).strip()
        s = re.sub(r"\s+([,;:])", r"\1", s)
        m = re.match(r"^([\w ]{1,10}?)\s*[;:]\s*(.*)$", s)
        if m and (m.group(1).strip().lower() in LABEL_UNITS
                  or (unit and m.group(1).strip().lower() == unit.lower())):
            s = m.group(2)
        return s.split(";")[0].strip().rstrip(",:")

    if name in LABELS:
        return LABELS[name]
    fld = next((f for f in sorted(FIELD_LABELS, key=len, reverse=True) if name.startswith("EXP_TB_") and name.endswith("_" + f)), None)
    if fld:
        return FIELD_LABELS[fld]
    cands = [clean(note.split("\n")[0]) if note else ""]
    if doc:
        cands.append(re.split(r"(?<=[a-z0-9])\.\s", clean(doc.split("\n")[0]))[0])
    for s in cands:
        if len(s) >= 3:
            if len(s) > LABEL_MAX:
                s = s[:LABEL_MAX].rsplit(" ", 1)[0].rstrip(",;:") + "\u2026"
            if s[0].islower() and not (len(s) > 1 and s[1].isupper()):   # keep "dB", "t4"
                s = s[0].upper() + s[1:]
            return s
    return name


def num(text, ty):
    t = text.strip()
    if ty == "bool":
        if t in ("true", "false"):
            return t == "true"
        raise ValueError(text)
    t = re.sub(r"(?<=[0-9.])[fF]$", "", t)
    v = float(t)
    return int(v) if ty == "int" else v


def make_entry(name, ty, value, comment, ctx, rel, lineno, problems):
    """One constant from its declaration parts. ctx: module, section, display, at, doc, doc_of."""
    tags, note = parse_tags(comment)
    e = {"name": name, "type": ty, "default_text": value, "module": ctx["module"],
         "section": ctx["section"], "doc": ctx["doc"], "doc_of": ctx["doc_of"] if ctx["doc"] else "",
         "note": note, "file": rel, "line": lineno}
    try:
        e["default"] = num(value, ty)
    except ValueError:
        e["default"] = None
        problems.append("%s:%d %s: default %r is not a plain number" % (rel, lineno, name, value))
    e["unit"] = (tags.get("unit") or [""])[0]
    e["why"] = (tags.get("why") or [""])[0]
    e["below"] = (tags.get("below") or [None])[0]
    rng = tags.get("range")
    e["range"] = None
    if ty != "bool":
        try:
            lo, hi = float(rng[0]), float(rng[1])
            if not lo < hi:
                raise ValueError
            e["range"] = [lo, hi]
        except (TypeError, ValueError, IndexError):
            problems.append("%s:%d %s: missing / bad @range" % (rel, lineno, name))
        if not e["why"]:
            problems.append("%s:%d %s: @range without @why (the engine limit it stops at)" % (rel, lineno, name))
    taper = (tags.get("taper") or ["lin"])[0]
    if taper not in TAPERS or (taper == "log" and e["range"] and e["range"][0] <= 0):
        problems.append("%s:%d %s: bad @taper %s" % (rel, lineno, name, taper))
        taper = "lin"
    e["taper"] = taper
    level = (tags.get("level") or [""])[0]
    if level not in LEVELS:
        problems.append("%s:%d %s: missing / bad @level" % (rel, lineno, name))
        level = "live"
    e["level"] = level
    e["when"] = (" ".join(tags.get("when") or [])) or None
    e["pair"] = None
    if "pair" in tags:
        pv = tags["pair"]
        if len(pv) == 2 and re.match(r"^\w+$", pv[0]) and pv[1] in END_ORDER:
            e["pair"] = {"id": pv[0], "end": pv[1], "end_label": END_LABELS[pv[1]]}
        else:
            problems.append("%s:%d %s: bad @pair (want: @pair <ID> %s)" % (rel, lineno, name, "|".join(END_ORDER)))
    if e["default"] is not None and e["range"] and ty != "bool":
        if not e["range"][0] <= e["default"] <= e["range"][1]:
            problems.append("%s:%d %s: default outside @range" % (rel, lineno, name))
    e["label"] = make_label(name, note, ctx["doc"], e["unit"])
    at = (tags.get("at") or [None])[0] or ctx.get("at") or ""
    e["at"] = [x.strip() for x in at.split(",") if x.strip()]
    e["display_section"] = ctx.get("display") or ctx["section"]
    tgt = (tags.get("target") or [None])[0] or ctx.get("target") or ""
    if tgt and tgt not in TARGET_IDS:
        problems.append("%s:%d %s: unknown @target %s (want one of %s)" % (rel, lineno, name, tgt, "|".join(TARGET_IDS)))
        tgt = ""
    e["target"] = tgt
    e["section_target"] = ctx.get("target") or ""
    st = tags.get("states")
    e["states"] = None
    if st is not None:
        if ty == "bool" and len(st) == 2:
            e["states"] = [x.replace("_", " ") for x in st]
        else:
            problems.append("%s:%d %s: @states wants a bool and two words" % (rel, lineno, name))
    e["view"] = ctx.get("view") or "knobs"
    iv = tags.get("inert")
    e["inert_on"] = None          # (kind, text) of a family @field's @inert; resolved per preset by expand_family
    if iv is not None:
        if len(iv) == 2:
            e["inert_on"] = (iv[0], iv[1].replace("_", " "))
        else:
            problems.append("%s:%d %s: @inert wants a kind token and one word (underscores = spaces)" % (rel, lineno, name))
    e["inert"] = None
    e["preset"] = None
    e["preset_index"] = None
    return e


def split_args(text):
    """'1.f, 2.f, false' -> ['1.f', '2.f', 'false'] (no nesting needed)."""
    return [x.strip() for x in text.split(",")]


def parse_file(path, rel=None):
    """Returns (entries, problems). entries: list of dicts in file order."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    rel = rel or path
    module = re.sub(r"_(constants|tables)$", "", os.path.splitext(os.path.basename(path))[0])
    entries, problems = [], []
    section = ""
    display, at = "", ""      # @section / @at directives (until the next `// ---` header)
    target, view = "", ""     # @target / @view directives (same scope)
    block = []
    run_doc, run_of = "", ""
    in_run = False
    last = None       # entry that may take note continuation lines
    fam = None        # @family: prefix, row / list macro, fields, doc, section, display, at, items, rows
    in_list = False
    rows = {}         # (macro, NAME) -> (args, kind token, line) of `{ "<NAME>", <kind>, ..., <macro>(args) },` rows
    for i, raw in enumerate(lines):
        line = raw.rstrip("\r")
        ctx = {"module": module, "section": section, "display": display, "at": at, "target": target, "view": view}
        if fam is not None:
            if re.match(r"^\s*#define\s+%s\b" % fam["list"], line):
                in_list = True
                continue
            if in_list:
                lm = LIST_ITEM_RE.match(line)
                if lm:
                    fam["items"].append((int(lm.group(1)), lm.group(2), i + 1))
                    continue
                in_list = False
        rm = ROW_RE.match(line)
        if rm:
            km = ROW_KIND_RE.match(line)
            rows[(rm.group(2), rm.group(1))] = (split_args(rm.group(3)), km.group(1) if km else "", i + 1)
        m = TUNABLE_RE.match(line)
        cm = None if m else CONSTEXPR_RE.match(line)
        if m or cm:
            name = m.group(2) if m else cm.group(1)
            if block:
                run_doc, run_of = "\n".join(block), name
            elif not in_run:
                run_doc, run_of = "", ""
            block = []
            in_run = True
            last = None
            if not m:
                continue
            ty, value, tail = m.group(1), m.group(3), m.group(4)
            ci = tail.find("//")
            comment = tail[ci + 2:].strip() if ci >= 0 else ""
            ctx.update(doc=run_doc, doc_of=run_of)
            e = make_entry(name, ty, value, comment, ctx, rel, i + 1, problems)
            entries.append(e)
            last = e
            continue
        ct = comment_text(line)
        if ct is not None:
            title = section_title(ct)
            if title is not None:
                section = title
                display, at = "", ""
                target, view = "", ""
                block, in_run, last = [], False, None
                continue
            d = DIRECTIVE_RE.match(ct.strip())
            if d:
                if d.group(1) == "section":
                    display = d.group(2).strip()
                elif d.group(1) == "target":
                    target = d.group(2).strip()
                elif d.group(1) == "view":
                    view = d.group(2).strip()
                    if view not in VIEWS:
                        problems.append("%s:%d bad @view %s (want %s)" % (rel, i + 1, view, "|".join(VIEWS)))
                        view = ""
                else:
                    at = d.group(2).strip()
                continue
            fm = FAMILY_RE.match(ct.strip())
            if fm:
                fam = {"prefix": fm.group(1), "row": fm.group(2), "list": fm.group(3), "fields": [],
                       "doc": "\n".join(block), "section": section, "display": display, "at": at,
                       "target": target, "view": view, "items": [], "line": i + 1}
                continue
            fd = FIELD_RE.match(ct.strip())
            if fd and fam is not None:
                fam["fields"].append((fd.group(1), fd.group(2), fd.group(3)))
                continue
            # note continuation: comment text starting with `//`, or a comment indented far
            # right (aligned under the inline note) directly below the declaration
            indent = len(line) - len(line.lstrip())
            if last is not None and not block and (ct.strip().startswith("//") or indent >= 16):
                more = ct.strip()[2:].strip() if ct.strip().startswith("//") else ct.strip()
                last["note"] += ("\n" if last["note"] else "") + more
                continue
            block.append(ct)
            last = None
            continue
        # blank or other code line (array body, closing brace): ends blocks and runs
        block, in_run, last = [], False, None
    if fam is not None:
        frows = {name: args for (mac, name), args in rows.items() if mac == fam["row"]}
        entries.extend(expand_family(fam, frows, module, rel, problems))
    for e in entries:
        e.pop("inert_on", None)   # resolved into e["inert"] (family fields only)
    return entries, problems


def expand_family(fam, rows, module, rel, problems):
    out = []
    if not fam["items"] or not fam["fields"]:
        problems.append("%s:%d @family %s: no list items / @field lines" % (rel, fam["line"], fam["prefix"]))
        return out
    doc_of = ""
    for idx, preset, item_line in fam["items"]:
        row, kind, lineno = rows.get(preset) or (None, "", item_line)
        if row is None or len(row) != len(fam["fields"]):
            problems.append("%s:%d %s: no %s(...) row with %d values" % (rel, item_line, preset, fam["row"], len(fam["fields"])))
            continue
        for (suffix, ty, comment), value in zip(fam["fields"], row):
            name = "%s%s_%s" % (fam["prefix"], preset, suffix)
            doc_of = doc_of or name
            ctx = {"module": module, "section": fam["section"], "display": fam["display"], "at": fam["at"],
                   "target": fam["target"], "view": fam["view"], "doc": fam["doc"], "doc_of": doc_of}
            e = make_entry(name, ty, value, comment, ctx, rel, lineno, problems)
            if e["pair"]:
                e["pair"]["id"] = "%s_%s" % (preset, e["pair"]["id"])
            e["preset"] = preset
            e["preset_index"] = idx
            if e["inert_on"] and e["inert_on"][0] == kind:
                e["inert"] = e["inert_on"][1]
            out.append(e)
    return out


def constants_files(repo, sources):
    """sources: dirs / files relative to repo -> every .h with a TUNABLE( declaration."""
    out = []
    for src in sources:
        p = os.path.join(repo, src)
        cands = []
        if os.path.isdir(p):
            for root, _dirs, files in os.walk(p):
                for fn in sorted(files):
                    if fn.endswith(".h"):
                        cands.append(os.path.join(root, fn))
        elif os.path.isfile(p):
            cands.append(p)
        for c in sorted(cands):
            try:
                with open(c, encoding="utf-8", errors="replace") as f:
                    if re.search(r"^\s*TUNABLE\(", f.read(), re.M) and c not in out:
                        out.append(c)
            except OSError:
                pass
    return out


def build(repo, pedal, sources):
    """Registry for one pedal: {pedal, files, constants: [...], problems: [...]}."""
    files = constants_files(repo, sources)
    consts, problems = [], []
    for p in files:
        rel = os.path.relpath(p, repo)
        e, pr = parse_file(p, rel)
        for x in e:
            x["pedal"] = pedal
        consts.extend(e)
        problems.extend(pr)
    seen = set()
    for c in consts:
        if c["name"] in seen:
            problems.append("%s: declared twice" % c["name"])
        seen.add(c["name"])
    for c in consts:
        if c.get("below") and c["below"] not in seen:
            problems.append("%s: @below %s is not a constant" % (c["name"], c["below"]))
    layout = LAYOUT.get(pedal)
    lp = layout_places(layout)
    for b in (layout or {}).get("benches", []):
        for m in b["modes"]:
            for k in m.get("knobs", []):
                if k["target"] and k["target"] not in TARGET_IDS:
                    problems.append("LAYOUT[%s] %s/%s K%d: unknown target %s" % (pedal, b["id"], m["id"], k["k"], k["target"]))
    for c in consts:
        places = []
        for a in c.pop("at", []):
            if a not in lp:
                if layout:
                    problems.append("%s: @at %s is not a bench/mode of LAYOUT[%s]" % (c["name"], a, pedal))
                continue
            b, m = a.split("/")
            places.append({"key": "%s/%s" % (a, c["display_section"]), "bench": b, "mode": m,
                           "section": c["display_section"], "order": lp[a]})
        if layout and not places:
            problems.append("%s: no @at place (bench/mode) for LAYOUT[%s]" % (c["name"], pedal))
        places.sort(key=lambda x: x["order"])
        for x in places:
            x.pop("order")
        c["places"] = places
    return {"pedal": pedal, "files": [os.path.relpath(p, repo) for p in files],
            "constants": consts, "pairs": build_pairs(consts, problems), "problems": problems,
            "layout": layout, "targets": TARGETS}


def build_pairs(consts, problems):
    """{ID: {label, kind: knob|range, members: [names, END_ORDER]}}; a bad pair is reported and dropped
    (its members then show as single rows)."""
    groups = {}
    for c in consts:
        if c.get("pair"):
            groups.setdefault(c["pair"]["id"], []).append(c)
    pairs = {}
    for pid, mem in groups.items():
        ends = [c["pair"]["end"] for c in mem]
        knob = [e in KNOB_ENDS for e in ends]
        why = None
        if len(mem) < 2:
            why = "only one member"
        elif len(set(ends)) != len(ends):
            why = "an end is used twice"
        elif any(knob) and not all(knob):
            why = "knob ends (ccw / noon / cw) mixed with range ends"
        elif len({(c["type"], c["unit"], c["taper"]) for c in mem}) != 1:
            why = "members differ in type / unit / taper"
        elif any(c["type"] == "bool" or not c["range"] for c in mem):
            why = "members need a number type and a @range"
        if why:
            problems.append("@pair %s: %s" % (pid, why))
            for c in mem:
                c["pair"] = None
            continue
        mem.sort(key=lambda c: END_ORDER.index(c["pair"]["end"]))
        pairs[pid] = {"label": pid.replace("_", " "), "kind": "knob" if all(knob) else "range",
                      "members": [c["name"] for c in mem]}
    return pairs
