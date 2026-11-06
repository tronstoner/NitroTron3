// Pedal devtool page - pure helpers (no DOM): the ONE time formatter, the ONE number formatter, the knob /
// fader value <-> position mapping (taper), and the pedal state-line parser. Loaded before app.js in the
// page; also `require`d by tools/devtool/web_core_test.js (node), so keep it free of DOM access.
"use strict";

// ---------------------------------------------------------------------------
// Time formatting: the ONE place timestamps become text. Local time only.
// Change a pattern here and every view follows.  tokens: YYYY MM DD HH mm ss SSS
// ---------------------------------------------------------------------------
const TIME_FORMATS = {
  clock: "HH:mm:ss.SSS",        // log lines, separators, flash output, status times
  build: "YYYY-MM-DD HH:mm",    // firmware build time (minute resolution on the pedal)
};

function fmtTime(value, pattern) {
  pattern = pattern || TIME_FORMATS.clock;
  if (value === null || value === undefined || value === "") return "–";
  const d = value instanceof Date ? value : new Date(value);
  if (isNaN(d.getTime())) return "–";
  const p = (n, w) => String(n).padStart(w || 2, "0");
  const tok = {
    YYYY: String(d.getFullYear()), MM: p(d.getMonth() + 1), DD: p(d.getDate()),
    HH: p(d.getHours()), mm: p(d.getMinutes()), ss: p(d.getSeconds()), SSS: p(d.getMilliseconds(), 3),
  };
  return pattern.replace(/YYYY|SSS|MM|DD|HH|mm|ss/g, (k) => tok[k]);
}

// The pedal sends its build time as local wall-clock "YYYY-MM-DDTHH:MM" (no zone).
function parseBuildTime(s) {
  const m = /^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d)/.exec(s || "");
  return m ? new Date(+m[1], +m[2] - 1, +m[3], +m[4], +m[5]) : null;
}

// ---------------------------------------------------------------------------
// Number formatting: the ONE place values become text (knobs, faders, number fields, preset grid, defaults,
// changes, knob strip). Unit-aware precision; the locale is a parameter (en-US default: "." decimals, which
// the number fields parse back). `sig` = significant digits, `dec` = fixed decimals. A value the unit's
// precision would misstate (a default like 5.834 at 3 digits) gets the extra digits it needs (up to 4 more),
// so nothing shown is rounded away; control values are snapped to the unit's precision (snapNum).
// ---------------------------------------------------------------------------
const NUM_LOCALE = "en-US";
const UNIT_FMT = {
  Hz: { sig: 4 }, kHz: { sig: 3 }, s: { sig: 3 }, ms: { sig: 3 }, dB: { dec: 1 }, bits: { dec: 1 },
  oct: { dec: 2 }, rad: { dec: 2 }, x: { sig: 3 }, "0..1": { dec: 3 }, b: { dec: 2 }, cents: { sig: 4 }, Q: { sig: 3 },
  knob: { dec: 2 },             // knob positions parsed from the pedal's state line
  "": { sig: 4 },
};
const numFormatters = new Map();
function numFormatter(unit, type, grouping, extra) {
  const key = unit + "|" + type + "|" + grouping + "|" + extra;
  let f = numFormatters.get(key);
  if (!f) {
    const u = type === "int" ? { dec: 0 } : (UNIT_FMT[unit] || UNIT_FMT[""]);
    const o = { useGrouping: grouping };
    if (u.sig !== undefined) o.maximumSignificantDigits = u.sig + extra;
    else { o.minimumFractionDigits = u.dec; o.maximumFractionDigits = u.dec + extra; }
    f = new Intl.NumberFormat(NUM_LOCALE, o);
    numFormatters.set(key, f);
  }
  return f;
}
// display: fmtNum(v, unit, type); plain (number fields): fmtNum(v, unit, type, true)
function fmtNum(value, unit, type, plain) {
  unit = unit || ""; type = type || "float";
  if (value === null || value === undefined || value === "") return "–";
  if (type === "bool") return value ? "on" : "off";
  const v = Number(value);
  if (!isFinite(v)) return "–";
  for (let extra = 0; extra < 5; extra++) {        // extra digits only if the value needs them
    const t = numFormatter(unit, type, false, extra).format(v);
    if (type === "int" || Math.fround(Number(t)) === Math.fround(v) || extra === 4)
      return plain ? t : numFormatter(unit, type, true, extra).format(v);
  }
  return String(v);
}
// units that are not written after the number
const SILENT_UNITS = { "": 1, "0..1": 1, bool: 1, knob: 1 };
function unitText(unit) { return SILENT_UNITS[unit || ""] ? "" : unit; }
// "1,234.5 Hz" style: value + unit
function fmtWithUnit(value, unit, type) {
  const t = fmtNum(value, unit, type);
  return type === "bool" || !unitText(unit) ? t : t + " " + unit;
}
// A live reading at a glance: the unit's precision, rounded (no extra digits), with its unit.
function fmtReading(value, unit) {
  const v = Number(value);
  if (!isFinite(v)) return "–";
  const t = numFormatter(unit || "", "float", true, 0).format(v);
  return unitText(unit) ? t + " " + unit : t;
}
// A frequency reading: below 1 kHz in Hz, above in kHz ("907 Hz", "2.69 kHz").
function fmtFreq(hz) {
  const v = Number(hz);
  return Math.abs(v) >= 1000 ? fmtReading(v / 1000, "kHz") : fmtReading(v, "Hz");
}
// The value a control actually sends = what the formatter shows (so pedal == display).
function snapNum(v, unit, type) {
  const t = numFormatter(unit || "", type || "float", false, 0).format(v);
  const n = Number(t);
  return isFinite(n) ? n : v;
}
// Parse a typed number (the locale's decimal separator or ".").
function parseNum(text) {
  const dec = (1.5).toLocaleString(NUM_LOCALE).charAt(1);
  let t = String(text).trim().replace(/\s/g, "");
  if (dec !== ".") t = t.split(dec).join(".");
  if (!/^[-+]?(\d+\.?\d*|\.\d+)(e[-+]?\d+)?$/i.test(t)) return NaN;
  return Number(t);
}

// ---------------------------------------------------------------------------
// Taper: control position (frac 0..1) <-> value, for knobs and faders alike. `c` = a registry constant
// ({range: [lo, hi], taper: lin|log, type, unit}). fromFrac snaps to the unit's precision (what is shown
// is what is sent), rounds ints and clamps to the range.
// ---------------------------------------------------------------------------
function clamp01(x) { return Math.min(1, Math.max(0, x)); }
function toFrac(c, v) {
  if (!c.range || v === null || v === undefined) return 0;
  const lo = c.range[0], hi = c.range[1];
  let x = c.taper === "log" ? Math.log(v / lo) / Math.log(hi / lo) : (v - lo) / (hi - lo);
  if (!isFinite(x)) x = 0;
  return clamp01(x);
}
function fromFrac(c, f) {
  if (!c.range) return c.value;
  const lo = c.range[0], hi = c.range[1];
  const x = clamp01(f);
  let v = c.taper === "log" ? lo * Math.pow(hi / lo, x) : lo + (hi - lo) * x;
  if (c.type === "int") v = Math.round(v);
  else v = snapNum(v, c.unit, c.type);
  return Math.min(hi, Math.max(lo, v));
}
// Where a value arc starts: 0 for a range that crosses zero (bipolar: drawn from the zero point, noon on a
// symmetric range), else the range start.
function originFrac(c) {
  if (!c.range || !(c.range[0] < 0 && c.range[1] > 0) || c.taper === "log") return 0;
  return toFrac(c, 0);
}
// The manual icons' sweep: 7 o'clock (-150 deg) .. 5 o'clock (+150 deg), 0 = noon, clockwise positive.
const KNOB_MIN_DEG = -150, KNOB_MAX_DEG = 150;
function fracToDeg(f) { return KNOB_MIN_DEG + (KNOB_MAX_DEG - KNOB_MIN_DEG) * clamp01(f); }

// ---------------------------------------------------------------------------
// Pedal state lines (DIAG), the only source of the pedal's current place and the knob strip's live values:
//   explorer  `XP <auto|#n> K1 .. K2 .. K3 .. K4 .. K5 .. SW1 <MID> oct <played> | <rest>`
//             rest: `tab <n>/<N> <NAME> <LFO|ANALOG|LOFI> ...`, `WT/<zone> ...`, `LAB SPEC bank <n>/9 <NAME> ...`,
//             `LAB 2OP-<MODE> ...`, `LAB ADD-<zone> ...`, else the model word (FM, XBAND, HARM, FSHIFT, INTERVAL)
//   armitage  `<auto|#n> K1 .. SW1 <..> <MODE> h .. SW2 <..> | ...`
// parseState -> {bench, sw1, preset, sel, knobs: [k1..k5], rest} or null. bench: armitage 1, `LAB ...` 3, else 2.
// ---------------------------------------------------------------------------
const XP_RE = /(?:^|\s)XP (?:auto|#\d+) K1 ([\d.]+) K2 ([\d.]+) K3 ([\d.]+) K4 ([\d.]+) K5 ([\d.]+) SW1 (\w+) oct (\w+) \| (.*)$/;
const AR_RE = /(?:^|\s)(?:auto|#\d+) K1 ([\d.]+) K2 ([\d.]+) K3 ([\d.]+) K4 ([\d.]+) K5 ([\d.]+) SW1 (\w+) (\w+) h ?[\d.]+ SW2 (\w+) \| (.*)$/;

function escRe(s) { return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"); }
function tokNum(rest, tok) {
  const m = new RegExp("(?:^|\\s)" + escRe(tok) + "\\s*(-?\\d+(?:\\.\\d+)?)(?=\\s|$|[A-Za-z])").exec(rest);
  return m ? Number(m[1]) : null;
}
function tokWord(rest, tok, n) {
  const m = new RegExp("(?:^|\\s)" + escRe(tok) + "\\s+(\\S+)(?:\\s+(\\S+))?").exec(rest);
  return m ? (n === 2 ? (m[2] === undefined ? null : m[2]) : m[1]) : null;
}

function selectionOf(rest) {
  let m;
  if ((m = /^tab (\d+)\/(\d+) (\S+) (LFO|ANALOG|LOFI)\b/.exec(rest))) return { sel: m[3], preset: m[3], of: m[1] + "/" + m[2] };
  if ((m = /^WT\/(\S+)/.exec(rest))) return { sel: m[1], preset: null };
  if ((m = /^LAB SPEC bank (\d+\/\d+) (\S+)/.exec(rest))) return { sel: m[2], preset: null, of: m[1] };
  if ((m = /^LAB 2OP-(\S+)/.exec(rest))) return { sel: m[1], preset: null };
  if ((m = /^LAB ADD-(\S+)/.exec(rest))) return { sel: m[1], preset: null };
  if ((m = /^LAB (\S+)/.exec(rest))) return { sel: m[1], preset: null };
  if ((m = /^FM r (\d+(?:\.\d+)?)/.exec(rest))) return { sel: "r " + fmtNum(Number(m[1]), "x"), preset: null };
  return { sel: rest.split(/\s+/)[0] || "?", preset: null };
}

function parseState(text) {
  let m = XP_RE.exec(text);
  if (m) {
    const rest = m[8];
    const s = selectionOf(rest);
    return { bench: /^LAB /.test(rest) ? "3" : "2", sw1: m[6], oct: m[7], preset: s.preset, sel: s.sel, of: s.of || "",
             knobs: [m[1], m[2], m[3], m[4], m[5]].map(Number), rest };
  }
  m = AR_RE.exec(text);
  if (m) return { bench: "1", sw1: m[6], oct: "", preset: null, sel: m[7], of: "", knobs: [m[1], m[2], m[3], m[4], m[5]].map(Number), rest: m[9] };
  return null;
}

// Touch env value: pluck side (K4 CCW of noon) = the decay time Td, swell side = the rise time Tr.
function envText(st) {
  const k4 = st.knobs[3];
  const td = tokNum(st.rest, "Td"), tr = tokNum(st.rest, "Tr");
  if (2 * k4 - 1 <= 0) return td === null ? null : "pluck " + fmtReading(td, "s");
  return tr === null ? null : "swell " + fmtReading(tr, "s");
}
const LFO_SHAPES = { tri: "tri", stp: "steps" };
function lfoText(st) {
  if (/(?:^|\s)src env\b/.test(st.rest)) return "env drives";
  const m = /(?:^|\s)lfo (tri|stp|off|-)(?: d ([\d.]+))?(?: > [\d.]+)?\s+([\d.]+)Hz/.exec(st.rest);
  if (!m) return /(?:^|\s)lfo (?:-|off)(?=\s|$)/.test(st.rest) ? "off" : null;
  if (m[1] === "off" || m[1] === "-" || Number(m[2]) === 0) return "off";
  return LFO_SHAPES[m[1]] + " " + fmtReading(Number(m[3]), "Hz");
}

// One knob card's live value from a registry LAYOUT `show` template (registry.py _k): placeholders are
// filled from the state line; one the line lacks -> the plain knob position.
function knobShow(template, st, k) {
  const pos = st && st.knobs ? st.knobs[k - 1] : null;
  const posText = pos === null || pos === undefined ? "–" : fmtNum(pos, "knob");
  if (!template || !st) return posText;
  let missing = false;
  const out = template.replace(/\{(\w+)(?::(\w+))?\}/g, (all, kind, tok) => {
    let v = null;
    if (kind === "k") v = posText;
    else if (kind === "sel") v = st.sel;
    else if (kind === "env") v = envText(st);
    else if (kind === "lfo") v = lfoText(st);
    else if (kind === "num") { const n = tokNum(st.rest, tok); v = n === null ? null : fmtReading(n, ""); }
    else if (kind === "hz") {
      const n = tokNum(st.rest, tok);
      v = n !== null ? fmtFreq(n) : (tokWord(st.rest, tok) === "off" ? "off" : null);
    }
    else if (kind === "word") v = tokWord(st.rest, tok, 1);
    else if (kind === "word2") v = tokWord(st.rest, tok, 2);
    if (v === null || v === undefined) missing = true;
    return v === null || v === undefined ? "" : v;
  });
  return missing ? posText : out.replace(/ {2,}/g, " ").trim();
}

// The frame a table preset plays, from its `tab` state line (explorer.h DIAG, pedals/nitrolab/main.cpp). The preset's
// Stepped flag (`stp 1|0`, every preset) decides; a line without `stp` (older firmware): stepped = a `step` token.
//   stepped `... step <n>/<N>` (K2 discrete)            -> key stage n: pos = keyStages[n - 1], stage = n
//   LFO     `... k2 <K2> pos <frame position>`         -> pos (0 .. frames - 1, fractional = blend of two frames)
//   LOFI    `... morph` (continuous)                   -> pos = K2 x (frames - 1)
//   ANALOG  one frame                                  -> pos 0
// -> {pos, stage (0 = not stepped)} or null when the line lacks the value.
function tablePlay(st, kind, frames, keyStages) {
  if (!st || !(frames >= 1)) return null;
  const top = frames - 1;
  const clampPos = (x) => Math.min(top, Math.max(0, x));
  if (kind === "ANALOG" || frames === 1) return { pos: 0, stage: 0 };
  const stp = tokNum(st.rest, "stp");
  const m = /(?:^|\s)step (\d+)\/(\d+)(?=\s|$)/.exec(st.rest);
  if (stp === 1 || (stp === null && m)) {
    if (!m) return null;
    const n = Number(m[1]);
    if (!keyStages || n < 1 || n > keyStages.length) return null;
    return { pos: clampPos(keyStages[n - 1]), stage: n };
  }
  if (kind === "LOFI") {
    const k2 = tokNum(st.rest, "k2");
    return k2 === null ? null : { pos: clampPos(k2 * top), stage: 0 };
  }
  const pos = tokNum(st.rest, "pos");
  return pos === null ? null : { pos: clampPos(pos), stage: 0 };
}

if (typeof module !== "undefined" && module.exports) {
  module.exports = { TIME_FORMATS, fmtTime, parseBuildTime, fmtNum, fmtWithUnit, fmtReading, fmtFreq, unitText, snapNum,
    parseNum, toFrac, fromFrac, originFrac, fracToDeg, KNOB_MIN_DEG, KNOB_MAX_DEG, parseState, knobShow,
    tokNum, tokWord, tablePlay };
}
