// Pedal devtool page (steps 1 + 2): connection + build info, flash, live log,
// live constants (tree, rows, pair rows, changes panel). Three panes: tree |
// values | log (always visible), draggable splitters; the pedal's current K1
// slot in the top bar.
// No build step, no external assets. Talks to tools/devtool/devtool.py.
"use strict";

// ---------------------------------------------------------------------------
// Time formatting: the ONE place timestamps become text. Local time only.
// Change a pattern here and every view follows.
//   tokens: YYYY MM DD HH mm ss SSS
// ---------------------------------------------------------------------------
const TIME_FORMATS = {
  clock: "HH:mm:ss.SSS",        // log lines, separators, flash output, status times
  build: "YYYY-MM-DD HH:mm",    // firmware build time (minute resolution on the pedal)
};

function fmtTime(value, pattern = TIME_FORMATS.clock) {
  if (value === null || value === undefined || value === "") return "–";
  const d = value instanceof Date ? value : new Date(value);
  if (isNaN(d.getTime())) return "–";
  const p = (n, w = 2) => String(n).padStart(w, "0");
  const tok = {
    YYYY: String(d.getFullYear()), MM: p(d.getMonth() + 1), DD: p(d.getDate()),
    HH: p(d.getHours()), mm: p(d.getMinutes()), ss: p(d.getSeconds()), SSS: p(d.getMilliseconds(), 3),
  };
  return pattern.replace(/YYYY|SSS|MM|DD|HH|mm|ss/g, (k) => tok[k]);
}

// ---------------------------------------------------------------------------
// Number formatting: the ONE place constant values become text (rows, number
// fields, defaults, changes panel, slider snapping). Unit-aware precision; the
// locale is a parameter (en-US default: "." decimals, which the number fields
// parse back). `sig` = significant digits, `dec` = fixed decimals. A value that
// the unit's precision would misstate (a default like 5.834 at 3 digits) gets
// the extra digits it needs (up to 7 significant), so nothing shown is rounded
// away; slider values are snapped to the unit's precision (snapNum).
// ---------------------------------------------------------------------------
const NUM_LOCALE = "en-US";
const UNIT_FMT = {
  Hz: { sig: 4 }, s: { sig: 3 }, ms: { sig: 3 }, dB: { dec: 1 }, bits: { dec: 1 },
  oct: { dec: 2 }, rad: { dec: 2 }, x: { sig: 3 }, "0..1": { dec: 3 }, b: { dec: 2 },
  knob: { dec: 2 },             // knob positions parsed from the pedal's state line (top bar)
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
function fmtNum(value, unit = "", type = "float", plain = false) {
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
// "1,234.5 Hz" style: value + unit
function fmtWithUnit(value, unit, type) {
  const t = fmtNum(value, unit, type);
  return type === "bool" || !unit || unit === "0..1" || unit === "bool" ? t : t + " " + unit;
}
// The value a control actually sends = what the formatter shows (so pedal == display).
function snapNum(v, unit, type) {
  const t = numFormatter(unit, type, false, 0).format(v);
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

// The pedal sends its build time as local wall-clock "YYYY-MM-DDTHH:MM" (no zone).
function parseBuildTime(s) {
  const m = /^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d)/.exec(s || "");
  return m ? new Date(+m[1], +m[2] - 1, +m[3], +m[4], +m[5]) : null;
}

// ---------------------------------------------------------------------------
const MAX_LINES = 20000;
const $ = (id) => document.getElementById(id);
const el = {
  dot: $("conn-dot"), connText: $("conn-text"), conn: $("conn"),
  pedal: $("b-pedal"), hash: $("b-hash"), time: $("b-time"), note: $("b-note"),
  flashPedal: $("flash-pedal"), flashBtn: $("flash-btn"),
  flashPanel: $("flash-panel"), flashBadge: $("flash-badge"), flashMsg: $("flash-msg"),
  flashTime: $("flash-time"), flashOut: $("flash-out"), flashLast: $("flash-last"),
  filter: $("filter"), filterMode: $("filter-mode"), count: $("count"),
  pauseBtn: $("pause-btn"), clearBtn: $("clear-btn"), log: $("log"), jump: $("jump"),
  changesBtn: $("changes-btn"), changesCount: $("changes-count"), changesPanel: $("changes-panel"),
  changesList: $("changes-list"), changesSub: $("changes-sub"), resetAll: $("reset-all-btn"),
  note: $("note"), sidebar: $("sidebar"), search: $("search"), tree: $("tree"),
  navToggle: $("nav-toggle"), viewConst: $("view-const"), viewLog: $("view-log"),
  tunState: $("tun-state"), guard: $("guard"), guardBtn: $("guard-btn"),
  secHead: $("sec-head"), rows: $("rows"), slotV: $("slot-v"), slotX: $("slot-x"),
  panes: $("panes"), splitTree: $("split-tree"), splitLog: $("split-log"),
};

const state = {
  online: false,          // SSE connected to devtool
  status: null,           // last status event
  flash: { active: false, state: "idle" },
  pedals: ["nitrolab", "chronotron3", "nitrotron3m"],
  defaultPedal: "nitrolab",
  userPicked: false,      // the user chose a pedal in the selector: keep it
  lines: [],              // {kind: "line"|"sep", t, text|what, el}
  paused: false,
  pausedAt: 0,            // lines.length when paused
  clearedAt: 0,           // "Clear view": hide lines up to this server time
  stick: true,            // auto-scroll while the view is at the bottom
  filter: { text: "", rx: null, isRegex: true },
  active: null,           // the pedal's current place from its state lines: {bench, sw1, preset}
};

function store(key, val) {
  try {
    if (val === undefined) return localStorage.getItem(key);
    localStorage.setItem(key, val);
  } catch (e) { /* storage blocked: fine */ }
  return null;
}

// ---------------------------------------------------------------------------
// Status / build
// ---------------------------------------------------------------------------
function renderStatus() {
  const s = state.status;
  let dot = "", text;
  if (!state.online) { dot = "bad"; text = "devtool offline - reconnecting…"; }
  else if (!s || !s.connected) { dot = ""; text = "waiting for pedal"; }
  else if (s.build_state === "ok") { dot = "ok"; text = "connected"; }
  else if (s.build_state === "asking") { dot = "warn"; text = "connected - asking for build…"; }
  else { dot = "warn"; text = "connected - no build answer (non-DIAG firmware?)"; }
  el.dot.className = "dot " + dot;
  el.connText.textContent = text;
  el.conn.title = s && s.dev ? s.dev + (s.log_file ? "  ->  " + s.log_file : "") : "";

  const b = s && s.connected ? s.build : null;
  el.pedal.textContent = b ? b.pedal : "–";
  el.hash.textContent = b ? b.hash : "–";
  if (b && b.dirty) {
    const d = document.createElement("span");
    d.className = "dirty";
    d.textContent = "dirty";
    el.hash.appendChild(d);
  }
  const bt = b ? parseBuildTime(b.time) : null;
  el.time.textContent = b ? (bt ? fmtTime(bt, TIME_FORMATS.build) : b.time) : "–";
  const note = s && s.connected && s.note ? s.note : "";
  el.note.textContent = note;          // (a fixed slot in the top bar: empty, never removed)
  el.note.title = note;

  if (!state.userPicked && !state.flash.active) {
    const want = b && state.pedals.includes(b.pedal) ? b.pedal : state.defaultPedal;
    el.flashPedal.value = want;
  }
  renderFlashControls();
}

function fillPedals() {
  const cur = el.flashPedal.value;
  el.flashPedal.textContent = "";
  for (const p of state.pedals) {
    const o = document.createElement("option");
    o.value = p; o.textContent = p;
    el.flashPedal.appendChild(o);
  }
  el.flashPedal.value = state.pedals.includes(cur) ? cur : state.defaultPedal;
}

async function loadMeta() {
  try {
    const r = await fetch("/api/status", { cache: "no-store" });
    const js = await r.json();
    if (Array.isArray(js.pedals) && js.pedals.length) state.pedals = js.pedals;
    if (js.default_pedal) state.defaultPedal = js.default_pedal;
  } catch (e) { /* offline: keep the defaults */ }
  fillPedals();
  renderStatus();
}

// ---------------------------------------------------------------------------
// Flash
// ---------------------------------------------------------------------------
const FLASH_LABEL = {
  idle: "idle", building: "building", "entering-dfu": "entering DFU",
  gesture: "footswitches", programming: "programming", done: "done", failed: "failed",
};

function renderFlashControls() {
  const busy = !!state.flash.active;
  el.flashBtn.disabled = busy || !state.online;
  el.flashPedal.disabled = busy;
  el.flashBtn.textContent = busy ? "Flashing…" : "Flash";
}

function renderFlash() {
  const f = state.flash;
  const st = f.state || "idle";
  if (st !== "idle") el.flashPanel.hidden = false;
  el.flashBadge.textContent = FLASH_LABEL[st] || st;
  el.flashBadge.className = "flash-badge " + (
    st === "done" ? "ok" : st === "failed" ? "bad" : st === "gesture" ? "attn" : f.active ? "run" : "");
  el.flashMsg.textContent = f.message ? (f.pedal ? f.pedal + ": " : "") + f.message : "";
  el.flashMsg.className = "flash-msg" + (st === "gesture" ? " attn" : "");
  el.flashMsg.title = el.flashMsg.textContent;
  el.flashTime.textContent = f.t ? fmtTime(f.t) : "";
  renderFlashControls();
}

function onFlash(f) {
  const prev = state.flash.state;
  state.flash = f && f.state ? f : { active: false, state: "idle" };
  if (state.flash.state === "building" && prev !== "building") {
    el.flashOut.textContent = "";        // never auto-expands: the summary shows the latest line
    el.flashLast.textContent = "";
  }
  renderFlash();
}

function onFlashOut(d) {
  const atBottom = el.flashOut.scrollTop + el.flashOut.clientHeight >= el.flashOut.scrollHeight - 4;
  // '\r' progress lines (dfu-util): replace the last line, as a terminal does
  if (d.replace && el.flashOut.lastChild) el.flashOut.removeChild(el.flashOut.lastChild);
  const line = document.createElement("div");
  if (d.stream === "info") line.className = "info";
  const ts = document.createElement("span");
  ts.className = "ts";
  ts.textContent = fmtTime(d.t);
  line.appendChild(ts);
  line.appendChild(document.createTextNode(d.text));
  el.flashOut.appendChild(line);
  while (el.flashOut.childNodes.length > 4000) el.flashOut.removeChild(el.flashOut.firstChild);
  if (atBottom) el.flashOut.scrollTop = el.flashOut.scrollHeight;
  el.flashLast.textContent = d.text;     // latest output line, visible while collapsed
  el.flashLast.title = d.text;
}

async function startFlash() {
  const pedal = el.flashPedal.value;
  el.flashBtn.disabled = true;
  try {
    const r = await fetch("/api/flash", {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ pedal }),
    });
    const js = await r.json().catch(() => ({}));
    if (!r.ok) {
      el.flashPanel.hidden = false;
      el.flashMsg.textContent = "not started: " + (js.message || r.status);
      el.flashMsg.className = "flash-msg attn";
    }
  } catch (e) {
    el.flashPanel.hidden = false;
    el.flashMsg.textContent = "not started: devtool unreachable";
    el.flashMsg.className = "flash-msg attn";
  }
  renderFlashControls();
}

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------
function setFilter(text) {
  const f = { text, rx: null, isRegex: true };
  if (text) {
    try { f.rx = new RegExp(text, "gi"); } catch (e) { f.isRegex = false; }
  }
  state.filter = f;
  el.filterMode.textContent = !text ? "" : f.isRegex ? "regex" : "substring";
  el.filterMode.className = "filter-mode" + (text && !f.isRegex ? " sub" : "");
}

function matches(text) {
  const f = state.filter;
  if (!f.text) return true;
  if (f.rx) { f.rx.lastIndex = 0; return f.rx.test(text); }
  return text.toLowerCase().includes(f.text.toLowerCase());
}

// Text with the filter's matches wrapped in <mark>.
function highlighted(text) {
  const frag = document.createDocumentFragment();
  const f = state.filter;
  const spans = [];
  if (f.text && f.rx) {
    f.rx.lastIndex = 0;
    let m;
    while ((m = f.rx.exec(text)) !== null) {
      if (m[0].length === 0) { f.rx.lastIndex++; continue; }
      spans.push([m.index, m.index + m[0].length]);
    }
  } else if (f.text) {
    const hay = text.toLowerCase(), needle = f.text.toLowerCase();
    let i = hay.indexOf(needle);
    while (i >= 0) { spans.push([i, i + needle.length]); i = hay.indexOf(needle, i + needle.length); }
  }
  let pos = 0;
  for (const [a, b] of spans) {
    if (a > pos) frag.appendChild(document.createTextNode(text.slice(pos, a)));
    const mk = document.createElement("mark");
    mk.textContent = text.slice(a, b);
    frag.appendChild(mk);
    pos = b;
  }
  if (pos < text.length) frag.appendChild(document.createTextNode(text.slice(pos)));
  return frag;
}

function makeRow(l) {
  const row = document.createElement("div");
  const ts = document.createElement("span");
  ts.className = "ts";
  ts.textContent = fmtTime(l.t);
  const tx = document.createElement("span");
  tx.className = "tx";
  if (l.kind === "sep") {
    row.className = "row sep";
    tx.textContent = l.what === "gone" ? "pedal gone" : "pedal back";
  } else {
    row.className = "row";
    tx.appendChild(highlighted(l.text));
  }
  row.appendChild(ts);
  row.appendChild(tx);
  return row;
}

function visible(l) {
  return l.t > state.clearedAt && (l.kind === "sep" || matches(l.text));
}

function renderCount() {
  const total = state.lines.filter((l) => l.kind === "line" && l.t > state.clearedAt).length;
  let s = total + (total === 1 ? " line" : " lines");
  if (state.paused) s += " - paused, " + (state.lines.length - state.pausedAt) + " new";
  el.count.textContent = s;
}

function renderEmpty() {
  if (el.log.querySelector(".row")) return;
  if (!el.log.querySelector(".empty")) {
    const e = document.createElement("div");
    e.className = "empty";
    e.textContent = state.lines.length ? "No lines match the filter." : "No log lines yet.";
    el.log.appendChild(e);
  }
}

function fullRender() {
  el.log.textContent = "";
  const frag = document.createDocumentFragment();
  const upto = state.paused ? state.pausedAt : state.lines.length;
  for (let i = 0; i < state.lines.length; i++) {
    const l = state.lines[i];
    l.el = null;
    if (i < upto && visible(l)) { l.el = makeRow(l); frag.appendChild(l.el); }
  }
  el.log.appendChild(frag);
  renderEmpty();
  renderCount();
  if (state.stick) scrollToBottom();
}

function scrollToBottom() {
  el.log.scrollTop = el.log.scrollHeight;
}

function addLine(l) {
  if (l.kind === "line") onSlotLine(l.text, l.t);
  state.lines.push(l);
  if (state.lines.length > MAX_LINES) {
    const old = state.lines.shift();
    if (old.el) old.el.remove();
    if (state.paused) state.pausedAt = Math.max(0, state.pausedAt - 1);
  }
  if (!state.paused && visible(l)) {
    const e = el.log.querySelector(".empty");
    if (e) e.remove();
    l.el = makeRow(l);
    el.log.appendChild(l.el);
    if (state.stick) scrollToBottom();
  }
  scheduleCount();
}

let countPending = false;
function scheduleCount() {
  if (countPending) return;
  countPending = true;
  requestAnimationFrame(() => { countPending = false; renderCount(); });
}

function setPaused(p) {
  state.paused = p;
  if (p) state.pausedAt = state.lines.length;
  el.pauseBtn.textContent = p ? "Resume" : "Pause";
  el.pauseBtn.setAttribute("aria-pressed", String(p));
  fullRender();
}

el.log.addEventListener("scroll", () => {
  state.stick = el.log.scrollTop + el.log.clientHeight >= el.log.scrollHeight - 4;
  el.jump.hidden = state.stick;
});
el.jump.addEventListener("click", () => { state.stick = true; el.jump.hidden = true; scrollToBottom(); });
el.pauseBtn.addEventListener("click", () => setPaused(!state.paused));
el.clearBtn.addEventListener("click", () => {
  const last = state.lines[state.lines.length - 1];
  state.clearedAt = last ? last.t : Date.now();
  for (const l of state.lines) l.el = null;
  state.lines = [];
  state.pausedAt = 0;
  fullRender();
});
let filterTimer = 0;
el.filter.addEventListener("input", () => {
  clearTimeout(filterTimer);
  filterTimer = setTimeout(() => {
    setFilter(el.filter.value);
    store("devtool.filter", el.filter.value);
    fullRender();
  }, 120);
});
el.filter.addEventListener("keydown", (e) => {
  if (e.key === "Escape") { el.filter.value = ""; el.filter.dispatchEvent(new Event("input")); }
});
el.flashPedal.addEventListener("change", () => { state.userPicked = true; });
el.flashBtn.addEventListener("click", startFlash);

// ---------------------------------------------------------------------------
// Current K1 slot (top bar), parsed from the pedal's DIAG state lines:
//   explorer  `XP <auto|#n> K1 .. K2 .. K3 .. K4 .. K5 .. SW1 <MID> oct <played> | <selection> ...`
//             selection: `tab <n>/<N> <NAME> <LFO|ANALOG|LOFI> ...` (+ `step <i>/<n>` on the LOFI slot),
//             `WT/<zone>`, `LAB SPEC bank <n>/9 <NAME>`, `LAB <zone>`, else the model word (FM, XBAND-FM, ...)
//   armitage  `<auto|#n> K1 .. SW1 <..> <MODE> h .. SW2 <..> | ...`
// Only these lines change it; it stays through reconnects. The same lines give the active place for the
// tree (state.active: bench id from the line type - armitage 1, `LAB ...` 3, else 2 -, mode from SW1 via
// the registry layout, K1 preset from `tab n/N NAME`).
// ---------------------------------------------------------------------------
const XP_RE = /(?:^|\s)XP (?:auto|#\d+) K1 ([\d.]+) K2 [\d.]+ K3 [\d.]+ K4 [\d.]+ K5 [\d.]+ SW1 (\w+) oct (\w+) \| (.*)$/;
const AR_RE = /(?:^|\s)(?:auto|#\d+) K1 ([\d.]+) K2 [\d.]+ K3 [\d.]+ K4 [\d.]+ K5 [\d.]+ SW1 (\w+) (\w+) h ?[\d.]+ SW2 (\w+) \| /;
function slotSelection(rest) {
  let m = /^tab (\d+)\/(\d+) (.+?) (LFO|ANALOG|LOFI)\b(.*)$/.exec(rest);
  if (m) {
    const st = /\bstep (\d+)\/(\d+)/.exec(m[5]);
    return "K1 " + m[1] + "/" + m[2] + "  " + m[3] + " (" + m[4] + ")" + (st ? "  K2 stage " + st[1] + "/" + st[2] : "");
  }
  if ((m = /^WT\/(\S+)/.exec(rest))) return "WT " + m[1];
  if ((m = /^LAB SPEC bank (\d+\/\d+) (\S+)/.exec(rest))) return "FM LAB SPEC bank " + m[1] + " " + m[2];
  if ((m = /^LAB (\S+)/.exec(rest))) return "FM LAB " + m[1];
  return (rest.split(/\s+/)[0] || "?");
}
function parseSlot(text) {
  let m = XP_RE.exec(text);
  if (m) {
    const tb = /^tab \d+\/\d+ (.+?) (?:LFO|ANALOG|LOFI)\b/.exec(m[4]);
    return { what: slotSelection(m[4]), k1: Number(m[1]), sw1: m[2], extra: "oct " + m[3],
             bench: /^LAB /.test(m[4]) ? "3" : "2", preset: tb ? tb[1] : null };
  }
  m = AR_RE.exec(text);
  if (m) return { what: "armitage " + m[3], k1: Number(m[1]), sw1: m[2], extra: "SW2 " + m[4], bench: "1", preset: null };
  return null;
}
function onSlotLine(text, t) {
  const p = parseSlot(text);
  if (!p) return;
  el.slotV.textContent = p.what;
  el.slotX.textContent = "K1 " + fmtNum(p.k1, "knob") + "  SW1 " + p.sw1 + "  " + p.extra + "  at " + fmtTime(t);
  el.slotV.parentNode.title = text;
  const a = state.active;
  if (!a || a.bench !== p.bench || a.sw1 !== p.sw1 || a.preset !== p.preset) {
    state.active = { bench: p.bench, sw1: p.sw1, preset: p.preset };
    renderActive();
  }
}

// ---------------------------------------------------------------------------
// Live constants (step 2)
// The registry (labels, ranges, files) comes from devtool's `tun` snapshot;
// values come from the pedal (`tval` echoes). DOM is built once per registry
// structure / selection and then updated in place, so scroll position, open
// rows, focus, the search and the selected section survive value updates and
// reconnects. Every row has fixed-height lines (messages, badges and marks
// have reserved space), so nothing reflows on a value update.
// PAIRS (registry @pair): the constants that are the ends of one law (knob
// positions CCW / noon / CW, or a value range) are ONE row: their value fields
// in end order around one multi-handle slider (one lane per end, on the union
// of their ranges in their shared taper; each handle stays in its own range).
// ---------------------------------------------------------------------------
const LEVEL_TIP = {
  live: "Live: read while audio runs - heard at once (next audio block).",
  reactivate: "Takes effect after the next mode / zone change.",
};
const SLIDER_STEPS = 1000;
const tun = {
  snap: null,             // last `tun` snapshot
  byName: new Map(),      // name -> constant (with .value)
  structKey: "",          // names in order: rebuild the tree / rows only when this changes
  sel: store("devtool.section") || "",     // "module/section"
  search: store("devtool.search") || "",
  open: new Set((() => { try { return JSON.parse(store("devtool.openRows") || "[]"); } catch (e) { return []; } })()),
  rowEls: new Map(),      // name -> single row {row, slider, num, chk, def, msg, chg} | {pair: rec, i}
  dragging: null,         // name of the slider under the pointer
  inflight: new Map(),    // name -> true while a POST runs
  queued: new Map(),      // name -> latest value waiting for the in-flight POST
  rowsKey: "",
};

// Tree keys. With a registry layout (NitroLab: benches / modes, registry.py LAYOUT) a constant sits in one
// leaf per place (`<bench>/<mode>/<section>`, c.places); without places it falls back to module / section.
function fbKey(c) { return c.module + "/" + (c.display_section || c.section); }
function keysOf(c) { return c.places && c.places.length ? c.places.map((p) => p.key) : [fbKey(c)]; }
function secKey(c) {                     // the primary key (changes panel, search groups): the active place first
  const ks = keysOf(c);
  const a = state.active && activeModeId();
  if (a) { const hit = ks.find((k) => k.startsWith(a + "/")); if (hit) return hit; }
  return ks[0];
}
function layoutOf() { return (tun.snap && tun.snap.layout) || null; }
function benchOf(id) { const L = layoutOf(); return L ? L.benches.find((b) => b.id === id) : null; }
function placeName(bench, mode) {
  const b = benchOf(bench);
  const m = b && b.modes.find((x) => x.id === mode);
  return (b ? b.name : "Bench " + bench) + " \u00b7 " + (m ? m.name : mode);
}
function activeModeId() {               // "<bench>/<mode>" of the pedal's current state, or null
  const a = state.active, b = a && benchOf(a.bench);
  if (!b) return null;
  const m = b.modes.find((x) => x.sw1 === a.sw1);
  return m ? b.id + "/" + m.id : null;
}
function sectionOrder(title) {           // within a mode: by the knob in the title (K1 .. K6), then the rest
  const m = /\bK(\d)\b/.exec(title || "");
  return m ? Number(m[1]) : 9;
}
function isChanged(c) {
  if (c.value === null || c.value === undefined || c.default === null) return false;
  if (c.type === "float") return Math.fround(c.value) !== Math.fround(c.default);
  return c.value !== c.default;
}
function writable() { return !!(tun.snap && tun.snap.writable && state.online); }

// --- taper: slider position (0..SLIDER_STEPS) <-> value ---
function toPos(c, v) {
  if (!c.range) return 0;
  const [lo, hi] = c.range;
  let x = c.taper === "log" ? Math.log(v / lo) / Math.log(hi / lo) : (v - lo) / (hi - lo);
  if (!isFinite(x)) x = 0;
  return Math.round(Math.min(1, Math.max(0, x)) * SLIDER_STEPS);
}
function fromPos(c, p) {
  if (!c.range) return c.value;
  const [lo, hi] = c.range;
  const x = p / SLIDER_STEPS;
  let v = c.taper === "log" ? lo * Math.pow(hi / lo, x) : lo + (hi - lo) * x;
  if (c.type === "int") v = Math.round(v);
  return Math.min(hi, Math.max(lo, snapNum(v, c.unit, c.type)));
}

function matchesSearch(c, q) {
  if (!q) return true;
  const hay = (c.name + "\n" + c.label + "\n" + c.note + "\n" + ((tun.snap.docs || {})[c.doc_of] || "")).toLowerCase();
  return q.toLowerCase().split(/\s+/).filter(Boolean).every((w) => hay.includes(w));
}

function onTun(snap) {
  tun.snap = snap;
  tun.byName = new Map(snap.constants.map((c) => [c.name, c]));
  const key = snap.pedal + "|" + snap.constants.map((c) => c.name).join(",");
  if (key !== tun.structKey) {
    tun.structKey = key;
    const secs = snap.constants.flatMap(keysOf);
    if (!secs.includes(tun.sel)) tun.sel = secs[0] || "";
    renderTree();
    renderRows(true);
  } else {
    updateTreeCounts();
    for (const c of snap.constants) updateRow(c.name);
  }
  renderTunState();
  renderGuard();
  renderChanges();
  if (document.activeElement !== el.note) el.note.value = snap.note || "";
}

function onTval(d) {
  const c = tun.byName.get(d.name);
  if (!c) return;
  c.value = d.value;
  updateRow(d.name);
  updateTreeCounts();
  renderChanges();
}

function onTerr(d) {
  if (tun.snap) tun.snap.last_err = d;
  renderTunState();
}

// --- status strip: one fixed-height line (state / last pedal error, guard summary, override) ---
function renderTunState() {
  const s = tun.snap;
  el.tunState.textContent = "";
  let txt;
  if (!state.online) txt = "devtool offline - reconnecting…";
  else if (!s) txt = "Waiting for devtool…";
  else txt = {
    offline: "No pedal: showing " + s.pedal + "'s constants files (defaults). Values arrive when a DIAG build connects.",
    asking: "Reading the pedal's values (list)…",
    synced: "Values from the pedal (" + s.constants.length + " live constants).",
    "no-answer": "The pedal does not answer `list` - flash a current DIAG build.",
  }[s.state] || s.state;
  el.tunState.appendChild(document.createTextNode(txt));
  let title = txt;
  if (state.online && s && s.last_err) {
    const e = document.createElement("span");
    e.className = "err";
    e.textContent = "  Pedal error at " + fmtTime(s.last_err.t) + ": " + s.last_err.text;
    el.tunState.appendChild(e);
    title += "\n" + e.textContent.trim();
  }
  el.tunState.title = title;
}

function renderGuard() {
  const s = tun.snap;
  const g = s ? s.guard : null;
  const reasons = g ? g.reasons.filter((r) => !(s.state === "offline" && /no pedal/.test(r))) : [];
  const warns = g ? g.warnings : [];
  const more = (n) => (n > 1 ? "  (+" + (n - 1) + " more)" : "");
  let txt = "";
  if (reasons.length) txt = (s.override ? "Allowed despite: " : "Changes blocked: ") + reasons[0] + more(reasons.length);
  else if (warns.length) txt = "Note: " + warns[0] + more(warns.length);
  el.guard.textContent = txt;
  el.guard.title = reasons.map((r) => "blocked: " + r).concat(warns.map((w) => "note: " + w)).join("\n");
  el.guard.className = "strip-guard" + (!reasons.length || s.override ? " allowed" : "");
  const btn = !!(reasons.length && s.state === "synced");
  el.guardBtn.style.visibility = btn ? "" : "hidden";   // (space kept: nothing in the strip moves)
  el.guardBtn.disabled = !btn;
  if (btn) el.guardBtn.textContent = s.override ? "Block changes again" : "Allow changes anyway";
}
el.guardBtn.addEventListener("click", () => {
  if (tun.snap) post("/api/override", { on: !tun.snap.override });
});

// --- tree ---------------------------------------------------------------
function treeGroups() {                  // fallback: module -> section (constants without places)
  const s = tun.snap;
  const mods = new Map();       // module -> Map(section -> [constants])
  for (const c of s.constants) {
    if (c.places && c.places.length) continue;
    if (!mods.has(c.module)) mods.set(c.module, new Map());
    const m = mods.get(c.module);
    const sec = c.display_section || c.section;
    if (!m.has(sec)) m.set(sec, []);
    m.get(sec).push(c);
  }
  return mods;
}

function treeButton(key, title, list) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "t-sec";
  b.dataset.key = key;
  b.title = title || "(no section)";
  const t = document.createElement("span");
  t.className = "t-title";
  t.textContent = title || "(no section)";
  const chg = document.createElement("span");
  chg.className = "t-chg";
  const n = document.createElement("span");
  n.className = "t-n";
  b.append(t, chg, n);
  b.addEventListener("click", () => selectSection(key));
  b._list = list;
  return b;
}

function renderTree() {
  el.tree.textContent = "";
  const s = tun.snap;
  if (!s) return;
  const L = layoutOf();
  const ped = document.createElement("div");
  ped.className = "t-pedal";
  ped.textContent = L ? L.title : s.pedal;
  el.tree.appendChild(ped);
  if (!s.constants.length) {
    const e = document.createElement("div");
    e.className = "t-empty";
    e.textContent = "No live constants in this pedal's files.";
    el.tree.appendChild(e);
  }
  if (L) {
    for (const b of L.benches) {
      const bh = document.createElement("div");
      bh.className = "t-bench";
      bh.dataset.bench = b.id;
      bh.textContent = b.name;
      bh.title = "SW3 " + b.sw3;
      el.tree.appendChild(bh);
      if (!b.modes.length) {
        const e = document.createElement("div");
        e.className = "t-empty";
        e.textContent = "No live constants yet.";
        el.tree.appendChild(e);
      }
      for (const m of b.modes) {
        const mid = b.id + "/" + m.id;
        const mh = document.createElement("div");
        mh.className = "t-mode";
        mh.dataset.mode = mid;
        mh.textContent = m.name;
        mh.title = "SW1 " + m.sw1;
        el.tree.appendChild(mh);
        const secs = new Map();   // section -> [constants], file order
        for (const c of s.constants)
          for (const p of c.places || [])
            if (p.bench === b.id && p.mode === m.id) {
              if (!secs.has(p.section)) secs.set(p.section, []);
              secs.get(p.section).push(c);
            }
        if (!secs.size) {
          const e = document.createElement("div");
          e.className = "t-empty t-empty-mode";
          e.textContent = "No live constants.";
          el.tree.appendChild(e);
        }
        const order = [...secs.keys()].map((t, i) => [t, i])
          .sort((x, y) => (sectionOrder(x[0]) - sectionOrder(y[0])) || (x[1] - y[1])).map((x) => x[0]);
        for (const sec of order) el.tree.appendChild(treeButton(mid + "/" + sec, sec, secs.get(sec)));
      }
    }
  }
  for (const [mod, secs] of treeGroups()) {
    const m = document.createElement("div");
    m.className = "t-mod";
    m.textContent = L ? mod + " (no bench tag)" : mod;
    el.tree.appendChild(m);
    for (const [sec, list] of secs) el.tree.appendChild(treeButton(mod + "/" + sec, sec, list));
  }
  updateTreeCounts();
  renderActive();
}

// The pedal's current place (state lines): bench / mode headers and their sections marked, and the K1
// preset's panel in the rows.
function renderActive() {
  const a = state.active;
  const am = activeModeId();
  for (const e of el.tree.querySelectorAll(".t-bench")) e.classList.toggle("active", !!a && e.dataset.bench === a.bench);
  for (const e of el.tree.querySelectorAll(".t-mode")) e.classList.toggle("active", e.dataset.mode === am);
  for (const e of el.tree.querySelectorAll(".t-sec")) e.classList.toggle("active", !!am && e.dataset.key.startsWith(am + "/"));
  for (const e of el.rows.querySelectorAll(".grp-title[data-preset]")) {
    const on = !!a && a.preset === e.dataset.preset && am !== null && (tun.sel || "").startsWith(am + "/");
    e.classList.toggle("active", on);
    e.querySelector(".now").textContent = on ? "now on K1" : "";
  }
}

function updateTreeCounts() {
  const q = tun.search.trim();
  for (const b of el.tree.querySelectorAll(".t-sec")) {
    const list = b._list.map((c) => tun.byName.get(c.name) || c);
    const hits = list.filter((c) => matchesSearch(c, q));
    const nchg = list.filter(isChanged).length;
    b.hidden = !!q && hits.length === 0;
    b.querySelector(".t-n").textContent = q ? hits.length + "/" + list.length : String(list.length);
    const ch = b.querySelector(".t-chg");
    ch.textContent = nchg ? String(nchg) : "";
    ch.title = nchg ? nchg + " changed" : "";
    b.setAttribute("aria-current", String(!q && b.dataset.key === tun.sel));
  }
}

function selectSection(key) {
  tun.sel = key;
  store("devtool.section", key);
  if (tun.search) { tun.search = ""; el.search.value = ""; store("devtool.search", ""); }
  updateTreeCounts();
  renderRows(true);
  el.viewConst.scrollTop = 0;
  setTreeOpen(false);
}

// --- rows -----------------------------------------------------------------
function pairOf(c) {
  const ps = tun.snap && tun.snap.pairs;
  return c.pair && ps && ps[c.pair.id] ? ps[c.pair.id] : null;
}

function renderRows(force) {
  const s = tun.snap;
  if (!s) return;
  const q = tun.search.trim();
  const key = tun.structKey + "|" + (q ? "?" + q : tun.sel);
  if (!force && key === tun.rowsKey) return;
  tun.rowsKey = key;
  const scroll = el.viewConst.scrollTop;
  el.rows.textContent = "";
  el.secHead.textContent = "";
  tun.rowEls.clear();
  const h = document.createElement("h2");
  const p = document.createElement("div");
  p.className = "path";
  let list;
  if (q) {
    list = s.constants.filter((c) => matchesSearch(c, q));
    h.textContent = "Search: " + list.length + (list.length === 1 ? " match" : " matches");
    p.textContent = q;
  } else {
    list = s.constants.filter((c) => keysOf(c).includes(tun.sel));
    const c0 = list[0];
    const pl = c0 && (c0.places || []).find((x) => x.key === tun.sel);
    h.textContent = c0 ? (pl ? pl.section : (c0.display_section || c0.section || "(no section)")) : "No section selected";
    p.textContent = c0 ? (pl ? placeName(pl.bench, pl.mode) + " - " + c0.module : c0.module + " - " + c0.file) : "";
  }
  h.title = h.textContent;
  p.title = p.textContent;
  el.secHead.append(h, p);
  el.navToggle.textContent = "Sections";
  let lastGroup = null, lastPreset = null;
  const frag = document.createDocumentFragment();
  const done = new Set();
  const shown = [];
  const inList = new Set(list.map((c) => c.name));
  for (const c of list) {
    if (done.has(c.name)) continue;
    if (q && secKey(c) !== lastGroup) {
      lastGroup = secKey(c);
      const g = document.createElement("div");
      g.className = "grp-title";
      const pl = (c.places || []).find((x) => x.key === lastGroup);
      g.textContent = pl ? placeName(pl.bench, pl.mode) + " / " + pl.section : c.module + " / " + (c.display_section || c.section || "(no section)");
      frag.appendChild(g);
      lastPreset = null;
    }
    if (c.preset && c.preset !== lastPreset) {   // per-preset panel (registry family): K1 position + name
      lastPreset = c.preset;
      const g = document.createElement("div");
      g.className = "grp-title preset-grp";
      g.dataset.preset = c.preset;
      const t = document.createElement("span");
      t.textContent = "K1 " + (c.preset_index + 1) + " \u00b7 " + c.preset;
      const now = document.createElement("span");
      now.className = "now";
      g.append(t, now);
      frag.appendChild(g);
    }
    let P = pairOf(c);
    // a pair whose members do not all act in this place: only its members here, as single rows if < 2
    if (P && !q) { const mem = P.members.filter((n) => inList.has(n)); P = mem.length >= 2 ? Object.assign({}, P, { members: mem }) : null; }
    if (P) {
      for (const n of P.members) { done.add(n); shown.push(n); }
      frag.appendChild(makePairRow(c.pair.id, P));
    } else {
      done.add(c.name);
      shown.push(c.name);
      frag.appendChild(makeTRow(c));
    }
  }
  if (!list.length) {
    const e = document.createElement("div");
    e.className = "empty-rows";
    e.textContent = q ? "Nothing matches." : "No constants here.";
    frag.appendChild(e);
  }
  el.rows.appendChild(frag);
  for (const n of shown) updateRow(n);
  renderActive();
  if (!force) el.viewConst.scrollTop = scroll;
}

// "Shared": the other places a constant acts in (Bench · mode), for the row head / explanation.
function sharedWith(c) {
  if (!c.places || c.places.length < 2) return [];
  const cur = c.places.find((x) => x.key === tun.sel);
  return c.places.filter((x) => x !== cur && !(cur && x.bench === cur.bench && x.mode === cur.mode))
    .map((x) => placeName(x.bench, x.mode));
}
function markShared(h, exp, names) {
  if (!names.length) return;
  const b = document.createElement("span");
  b.className = "badge shared";
  b.textContent = "shared";
  b.title = "Also acts on: " + names.join(", ") + " (one value, changed everywhere)";
  h.head.querySelector(".flags").prepend(b);
  const d = document.createElement("div");
  d.className = "doc-h";
  d.textContent = "Shared with " + names.join(", ") + " - one value, changed everywhere.";
  exp.prepend(d);
}

// Row head: twisty, label, (kind), name(s), message slot, changed + level badges. Click / Enter folds `exp`.
function makeHead(openKey, labelText, kindText, nameText, level, when) {
  const head = document.createElement("div");
  head.className = "thead";
  const tw = document.createElement("span");
  tw.className = "twisty";
  const label = document.createElement("span");
  label.className = "label";
  label.textContent = labelText;
  label.title = labelText;
  head.append(tw, label);
  if (kindText) {
    const k = document.createElement("span");
    k.className = "kind";
    k.textContent = kindText;
    head.appendChild(k);
  }
  const name = document.createElement("span");
  name.className = "name";
  name.textContent = nameText;
  name.title = nameText;
  const msg = document.createElement("span");
  msg.className = "tmsg";
  const flags = document.createElement("span");
  flags.className = "flags";
  const chg = document.createElement("span");
  chg.className = "badge chg no";
  chg.textContent = "changed";
  const lvl = document.createElement("span");
  lvl.className = "badge " + level;
  lvl.textContent = level;
  lvl.title = when || LEVEL_TIP[level] || "";
  flags.append(chg, lvl);
  head.append(name, msg, flags);
  return { head, tw, msg, chg, openKey };
}

function wireExpander(h, exp) {
  const setOpen = (o) => {
    exp.hidden = !o;
    h.tw.textContent = o ? "▾" : "▸";
    h.head.setAttribute("aria-expanded", String(o));
  };
  setOpen(tun.open.has(h.openKey));
  h.head.tabIndex = 0;
  h.head.setAttribute("role", "button");
  const toggle = () => {
    const o = exp.hidden;
    if (o) tun.open.add(h.openKey); else tun.open.delete(h.openKey);
    store("devtool.openRows", JSON.stringify([...tun.open]));
    setOpen(o);
  };
  h.head.addEventListener("click", (e) => { if (!e.target.closest("button")) toggle(); });
  h.head.addEventListener("keydown", (e) => {
    if (e.target === h.head && (e.key === "Enter" || e.key === " ")) { e.preventDefault(); toggle(); }
  });
}

// The explanation of one constant (note, the shared comment block above it, source + range).
function explainInto(exp, c, prefix) {
  const docs = (tun.snap && tun.snap.docs) || {};
  if (prefix) {
    const t = document.createElement("div");
    t.className = "doc-h";
    t.textContent = prefix;
    exp.appendChild(t);
  }
  if (c.note) {
    const n = document.createElement("pre");
    n.className = "note-txt";
    n.textContent = c.note;
    exp.appendChild(n);
  }
  if (c.doc_of && docs[c.doc_of] && !exp._docs.has(c.doc_of)) {
    exp._docs.add(c.doc_of);
    const dh = document.createElement("div");
    dh.className = "doc-h";
    dh.textContent = c.doc_of === c.name ? "Notes above it:" : "Notes above " + c.doc_of + " (shared by the constants below it):";
    const d = document.createElement("pre");
    d.className = "doc-txt";
    d.textContent = docs[c.doc_of];
    exp.append(dh, d);
  }
  const src = document.createElement("div");
  src.className = "src";
  src.textContent = c.file + ":" + c.line + "   default " + c.default_text +
    (c.range ? "   range " + fmtNum(c.range[0], c.unit, c.type) + " .. " + fmtNum(c.range[1], c.unit, c.type) + " (" + c.taper + ")" : "");
  exp.appendChild(src);
}

// A typed number -> sendSet, or a message (range / type checks as devtool's).
function commitNum(c, num) {
  const cc = tun.byName.get(c.name);
  const v = parseNum(num.value);
  if (!isFinite(v)) { showMsg(c.name, "Not a number: " + num.value); updateRow(c.name, true); return; }
  if (cc.type === "int" && v !== Math.round(v)) { showMsg(c.name, "Whole numbers only."); updateRow(c.name, true); return; }
  const [lo, hi] = cc.range || [-Infinity, Infinity];
  if (v < lo || v > hi) {
    showMsg(c.name, "Outside the range " + fmtWithUnit(lo, cc.unit, cc.type) + " .. " + fmtWithUnit(hi, cc.unit, cc.type) + ".");
    updateRow(c.name, true);
    return;
  }
  sendSet(c.name, v);
}

function makeNum(c, aria) {
  const num = document.createElement("input");
  num.type = "text";
  num.inputMode = "decimal";
  num.className = "num";
  num.setAttribute("aria-label", aria);
  num.spellcheck = false;
  num.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); commitNum(c, num); num.select(); }
    if (e.key === "Escape") { updateRow(c.name, true); num.blur(); }
  });
  num.addEventListener("change", () => commitNum(c, num));
  num.addEventListener("blur", () => updateRow(c.name, true));
  return num;
}

function makeReset(c, text, aria) {
  const rst = document.createElement("button");
  rst.type = "button";
  rst.className = "rst";
  rst.textContent = text;
  rst.title = "Back to the compiled default (" + fmtWithUnit(c.default, c.unit, c.type) + ")";
  rst.setAttribute("aria-label", aria);
  rst.addEventListener("click", () => sendReset(c.name));
  return rst;
}

function makeTRow(c) {
  const row = document.createElement("div");
  row.className = "trow";
  row.dataset.name = c.name;
  const h = makeHead(c.name, c.label, "", c.name, c.level, c.when);
  const ctl = document.createElement("div");
  ctl.className = "tctl";
  const r = { row, chg: h.chg, msg: h.msg, tw: h.tw };
  if (c.type === "bool") {
    const lab = document.createElement("label");
    lab.className = "tog";
    const chk = document.createElement("input");
    chk.type = "checkbox";
    chk.setAttribute("role", "switch");
    chk.setAttribute("aria-label", c.label);
    const txt = document.createElement("span");
    txt.className = "tog-txt";
    lab.append(chk, txt);
    chk.addEventListener("change", () => sendSet(c.name, chk.checked));
    r.chk = chk; r.chkTxt = txt;
    ctl.appendChild(lab);
  } else {
    const sl = document.createElement("input");
    sl.type = "range";
    sl.min = "0"; sl.max = String(SLIDER_STEPS); sl.step = "1";
    sl.setAttribute("aria-label", c.label);
    const num = makeNum(c, c.label + " value");
    const unit = document.createElement("span");
    unit.className = "unit";
    unit.textContent = c.unit === "bool" ? "" : c.unit;
    sl.addEventListener("pointerdown", () => { tun.dragging = c.name; });
    sl.addEventListener("pointerup", () => { tun.dragging = null; });
    sl.addEventListener("change", () => { tun.dragging = null; });   // drag released anywhere
    sl.addEventListener("input", () => {
      const cc = tun.byName.get(c.name);
      const v = fromPos(cc, +sl.value);
      if (document.activeElement !== num) num.value = fmtNum(v, cc.unit, cc.type, true);
      sendSet(c.name, v);
    });
    r.slider = sl; r.num = num;
    sl.style.visibility = c.range ? "" : "hidden";   // (no @range: number field only; devtool flags the tag)
    ctl.append(sl, num, unit);
  }
  const def = document.createElement("span");
  def.className = "def";
  def.textContent = "default " + fmtWithUnit(c.default, c.unit, c.type);
  def.title = "Compiled default: " + c.default_text + "  (" + c.file + ":" + c.line + ")";
  r.rst = makeReset(c, "Reset", "Reset " + c.label);
  ctl.append(def, r.rst);

  const exp = document.createElement("div");
  exp.className = "texp";
  exp._docs = new Set();
  explainInto(exp, c, "");
  markShared(h, exp, sharedWith(c));
  wireExpander(h, exp);
  row.append(h.head, ctl, exp);
  tun.rowEls.set(c.name, r);
  return row;
}

// --- pair row --------------------------------------------------------------
// The slider axis of a pair: the union of the members' ranges, their (shared) taper.
function pairAxis(members) {
  const c0 = members[0];
  let lo = Infinity, hi = -Infinity;
  for (const c of members) { lo = Math.min(lo, c.range[0]); hi = Math.max(hi, c.range[1]); }
  return { range: [lo, hi], taper: c0.taper, unit: c0.unit, type: c0.type };
}
function axisFrac(ax, v) { return toPos(ax, v) / SLIDER_STEPS; }
function memberValueAt(c, ax, frac) {
  const v = fromPos(ax, Math.round(Math.min(1, Math.max(0, frac)) * SLIDER_STEPS));
  return Math.min(c.range[1], Math.max(c.range[0], snapNum(v, c.unit, c.type)));
}

function makePairRow(id, P) {
  const members = P.members.map((n) => tun.byName.get(n));
  const c0 = members[0];
  const row = document.createElement("div");
  row.className = "trow prow";
  row.dataset.pair = id;
  const kind = P.kind === "knob" ? "knob ends" : "range";
  const h = makeHead("pair:" + id, P.label, kind, P.members.join(" · "), c0.level, c0.when);
  const ctl = document.createElement("div");
  ctl.className = "tctl pctl";
  const rec = { row, chg: h.chg, msg: h.msg, tw: h.tw, members: P.members, ends: [], ax: pairAxis(members) };

  const ms = document.createElement("div");
  ms.className = "mslider";
  ms.setAttribute("role", "group");
  ms.setAttribute("aria-label", P.label);
  rec.ms = ms;

  members.forEach((c, i) => {
    const endLab = c.pair.end_label;
    const wrap = document.createElement("span");
    wrap.className = "end";
    const lab = document.createElement("span");
    lab.className = "end-lab";
    lab.textContent = endLab;
    const num = makeNum(c, P.label + " " + endLab + " value");
    num.title = c.name + " - " + c.label;
    const unit = document.createElement("span");
    unit.className = "unit";
    unit.textContent = c.unit;
    const rst = makeReset(c, "↺", "Reset " + P.label + " " + endLab);
    wrap.append(lab, num, unit, rst);

    // its lane in the multi-handle slider
    const lane = document.createElement("div");
    lane.className = "mlane";
    lane.title = endLab + ": " + c.name;
    const track = document.createElement("div");
    track.className = "mtrack";
    const span = document.createElement("div");
    span.className = "mspan";
    const a = axisFrac(rec.ax, c.range[0]), b = axisFrac(rec.ax, c.range[1]);
    span.style.left = (a * 100) + "%";
    span.style.width = ((b - a) * 100) + "%";
    const hd = document.createElement("div");
    hd.className = "mhandle";
    hd.tabIndex = 0;
    hd.setAttribute("role", "slider");
    hd.setAttribute("aria-label", P.label + " " + endLab);
    hd.setAttribute("aria-valuemin", String(c.range[0]));
    hd.setAttribute("aria-valuemax", String(c.range[1]));
    lane.append(track, span, hd);
    ms.appendChild(lane);

    const setFrac = (frac) => {
      const cc = tun.byName.get(c.name);
      const v = memberValueAt(cc, rec.ax, frac);
      hd.style.left = (axisFrac(rec.ax, v) * 100) + "%";
      if (document.activeElement !== num) num.value = fmtNum(v, cc.unit, cc.type, true);
      sendSet(c.name, v);
    };
    const fracAt = (e) => {
      const rc = lane.getBoundingClientRect();
      return rc.width > 0 ? (e.clientX - rc.left) / rc.width : 0;
    };
    lane.addEventListener("pointerdown", (e) => {
      if (ms.classList.contains("off") || e.button !== 0) return;
      e.preventDefault();
      lane.setPointerCapture(e.pointerId);
      tun.dragging = c.name;
      hd.focus({ preventScroll: true });
      setFrac(fracAt(e));
    });
    lane.addEventListener("pointermove", (e) => {
      if (tun.dragging === c.name && lane.hasPointerCapture(e.pointerId)) setFrac(fracAt(e));
    });
    const release = (e) => {
      if (lane.hasPointerCapture(e.pointerId)) lane.releasePointerCapture(e.pointerId);
      if (tun.dragging === c.name) { tun.dragging = null; updateRow(c.name); }
    };
    lane.addEventListener("pointerup", release);
    lane.addEventListener("pointercancel", release);
    hd.addEventListener("keydown", (e) => {
      if (ms.classList.contains("off")) return;
      const cc = tun.byName.get(c.name);
      const cur = cc.value !== null && cc.value !== undefined ? cc.value : cc.default;
      const f = axisFrac(rec.ax, cur);
      const step = e.shiftKey ? 0.1 : 0.01;
      let nf = null;
      if (e.key === "ArrowRight" || e.key === "ArrowUp") nf = f + step;
      else if (e.key === "ArrowLeft" || e.key === "ArrowDown") nf = f - step;
      else if (e.key === "PageUp") nf = f + 0.1;
      else if (e.key === "PageDown") nf = f - 0.1;
      else if (e.key === "Home") nf = axisFrac(rec.ax, cc.range[0]);
      else if (e.key === "End") nf = axisFrac(rec.ax, cc.range[1]);
      if (nf === null) return;
      e.preventDefault();
      setFrac(nf);
    });
    rec.ends.push({ name: c.name, label: endLab, wrap, num, rst, lane, hd });
    tun.rowEls.set(c.name, { pair: rec, i });
  });

  ctl.appendChild(rec.ends[0].wrap);
  ctl.appendChild(ms);
  for (let i = 1; i < rec.ends.length; i++) ctl.appendChild(rec.ends[i].wrap);

  const exp = document.createElement("div");
  exp.className = "texp";
  exp._docs = new Set();
  members.forEach((c) => explainInto(exp, c, c.pair.end_label + " - " + c.name + " - " + c.label));
  markShared(h, exp, [...new Set(members.flatMap(sharedWith))]);
  wireExpander(h, exp);
  row.append(h.head, ctl, exp);
  return row;
}

function showMsg(name, text) {
  const r = tun.rowEls.get(name);
  if (!r) return;
  const tgt = r.pair ? r.pair : r;
  const t = text ? (r.pair ? r.pair.ends[r.i].label + ": " : "") + text : "";
  tgt.msg.textContent = t;
  tgt.msg.title = t;
}

// Value -> controls of one row (skips a field the user is typing in / a slider being dragged).
function updateRow(name, forceNum) {
  const r = tun.rowEls.get(name);
  const c = tun.byName.get(name);
  if (!r || !c) return;
  if (r.pair) { updatePairEnd(r.pair, r.i, forceNum); return; }
  const has = c.value !== null && c.value !== undefined;
  const v = has ? c.value : c.default;
  const off = c.in_firmware === false;
  const can = writable() && !off;
  r.row.classList.toggle("changed", isChanged(c));
  r.row.classList.toggle("off", off);
  r.row.classList.toggle("pending", tun.inflight.has(name) || tun.queued.has(name));
  r.chg.classList.toggle("no", !isChanged(c));
  if (r.chk) {
    r.chk.checked = !!v;
    r.chk.disabled = !can;
    r.chkTxt.textContent = fmtNum(v, c.unit, "bool") + (has ? "" : "  (default)");
  } else {
    if (tun.dragging !== name) r.slider.value = String(toPos(c, v));
    r.slider.disabled = !can;
    r.num.disabled = !can;
    if (forceNum || document.activeElement !== r.num) r.num.value = fmtNum(v, c.unit, c.type, true);
  }
  r.rst.disabled = !(tun.snap && tun.snap.state === "synced" && state.online) || !isChanged(c);
  r.row.title = off ? "Not live in this firmware" : "";
}

function updatePairEnd(rec, i, forceNum) {
  const e = rec.ends[i];
  const c = tun.byName.get(e.name);
  const has = c.value !== null && c.value !== undefined;
  const v = has ? c.value : c.default;
  const off = c.in_firmware === false;
  const can = writable() && !off;
  const ch = isChanged(c);
  e.wrap.classList.toggle("changed", ch);
  e.lane.classList.toggle("changed", ch);
  e.wrap.classList.toggle("pending", tun.inflight.has(e.name) || tun.queued.has(e.name));
  if (tun.dragging !== e.name) e.hd.style.left = (axisFrac(rec.ax, v) * 100) + "%";
  e.hd.setAttribute("aria-valuenow", String(v));
  e.hd.setAttribute("aria-valuetext", fmtWithUnit(v, c.unit, c.type));
  e.hd.setAttribute("aria-disabled", String(!can));
  e.num.disabled = !can;
  if (forceNum || document.activeElement !== e.num) e.num.value = fmtNum(v, c.unit, c.type, true);
  e.rst.disabled = !(tun.snap && tun.snap.state === "synced" && state.online) || !ch;
  // whole row: changed if any end is, off / not writable if every end is
  const all = rec.members.map((n) => tun.byName.get(n));
  const any = all.some(isChanged);
  const allOff = all.every((x) => x.in_firmware === false);
  rec.row.classList.toggle("changed", any);
  rec.row.classList.toggle("off", allOff);
  rec.chg.classList.toggle("no", !any);
  rec.ms.classList.toggle("off", !can);
  rec.row.title = allOff ? "Not live in this firmware" : "";
}

// --- sending: one request per constant in flight, latest value wins -------------
async function post(path, body) {
  try {
    const r = await fetch(path, {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
    });
    const js = await r.json().catch(() => ({}));
    return { ok: r.ok, status: r.status, message: js.message || String(r.status) };
  } catch (e) {
    return { ok: false, status: 0, message: "devtool unreachable" };
  }
}

function sendSet(name, value) {
  if (tun.inflight.has(name)) { tun.queued.set(name, value); updateRow(name); return; }
  tun.inflight.set(name, true);
  updateRow(name);
  post("/api/set", { name, value }).then((res) => {
    tun.inflight.delete(name);
    showMsg(name, res.ok ? "" : res.message);
    if (tun.queued.has(name)) {
      const v = tun.queued.get(name);
      tun.queued.delete(name);
      sendSet(name, v);
    } else {
      updateRow(name, !res.ok);
    }
  });
}

function sendReset(name) {
  tun.queued.delete(name);
  post("/api/reset", { name }).then((res) => showMsg(name, res.ok ? "" : res.message));
}

// --- changes panel ------------------------------------------------------------
function renderChanges() {
  const s = tun.snap;
  const list = s ? s.constants.map((c) => tun.byName.get(c.name)).filter(isChanged) : [];
  el.changesCount.textContent = String(list.length);
  el.changesCount.classList.toggle("on", list.length > 0);
  el.changesSub.textContent = s ? "- " + s.pedal + (s.firmware ? " " + s.firmware : "") : "";
  el.changesList.textContent = "";
  if (!list.length) {
    const li = document.createElement("li");
    li.className = "empty";
    li.textContent = s && s.state === "synced" ? "Everything at the compiled defaults." : "No pedal values yet.";
    el.changesList.appendChild(li);
  }
  for (const c of list) {
    const li = document.createElement("li");
    const lab = document.createElement("span");
    lab.className = "c-label";
    lab.textContent = c.label;
    lab.title = "Show " + c.name;
    lab.addEventListener("click", () => { selectSection(secKey(c)); revealRow(c.name); });
    const nm = document.createElement("span");
    nm.className = "c-name";
    nm.textContent = c.name;
    const val = document.createElement("span");
    val.className = "c-val";
    val.append(document.createTextNode(fmtNum(c.default, c.unit, c.type) + " → "));
    const b = document.createElement("b");
    b.textContent = fmtNum(c.value, c.unit, c.type);
    val.append(b, document.createTextNode(c.unit && c.unit !== "bool" ? " " + c.unit : ""));
    li.append(lab, nm, val);
    el.changesList.appendChild(li);
  }
  el.resetAll.disabled = !list.length || !(s && s.state === "synced" && state.online);
}

function revealRow(name) {
  const r = tun.rowEls.get(name);
  if (r) (r.pair ? r.pair.row : r.row).scrollIntoView({ block: "center" });
}

let resetArmTimer = 0;
el.resetAll.addEventListener("click", () => {
  if (!el.resetAll.classList.contains("armed")) {
    el.resetAll.classList.add("armed");
    el.resetAll.textContent = "Click again: reset all";
    clearTimeout(resetArmTimer);
    resetArmTimer = setTimeout(() => { el.resetAll.classList.remove("armed"); el.resetAll.textContent = "Reset all"; }, 3000);
    return;
  }
  clearTimeout(resetArmTimer);
  el.resetAll.classList.remove("armed");
  el.resetAll.textContent = "Reset all";
  post("/api/reset", { name: "all" });
});

let noteTimer = 0;
el.note.addEventListener("input", () => {
  clearTimeout(noteTimer);
  noteTimer = setTimeout(() => post("/api/note", { text: el.note.value }), 600);
});
el.note.addEventListener("blur", () => { clearTimeout(noteTimer); post("/api/note", { text: el.note.value }); });

function setChangesOpen(o) {
  el.changesPanel.hidden = !o;
  el.changesBtn.setAttribute("aria-expanded", String(o));
  store("devtool.changesOpen", o ? "1" : "0");
}
el.changesBtn.addEventListener("click", () => setChangesOpen(el.changesPanel.hidden));

// --- narrow pane: the tree as an overlay; search -----------------------------------
function setTreeOpen(o) {
  el.sidebar.classList.toggle("open", o);
  el.navToggle.setAttribute("aria-expanded", String(o));
}
el.navToggle.addEventListener("click", () => setTreeOpen(!el.sidebar.classList.contains("open")));
let searchTimer = 0;
el.search.addEventListener("input", () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(() => {
    tun.search = el.search.value;
    store("devtool.search", tun.search);
    if (!tun.snap) return;
    updateTreeCounts();
    renderRows(true);
  }, 120);
});
el.search.addEventListener("keydown", (e) => {
  if (e.key === "Escape") { el.search.value = ""; el.search.dispatchEvent(new Event("input")); }
});

// --- panes: tree | values | log, sizes by the two splitters ---------------------------
// Pointer drag or keyboard (arrows 16 px, Shift 64 px, Home / End = min / max, double-click = default);
// the values pane keeps PANE_MIN.values; sizes remembered per browser (localStorage, try/catch in store()).
const PANE_MIN = { tree: 160, values: 360, log: 260 };
const PANE_DEF = { tree: 260, log: 520 };
const SPLIT_W = 7;
const panes = {
  tree: Number(store("devtool.treeW")) || PANE_DEF.tree,
  log: Number(store("devtool.logW")) || PANE_DEF.log,
};
function paneMax(which) {
  const total = el.panes.clientWidth;
  const other = which === "tree" ? panes.log : panes.tree;
  return Math.max(PANE_MIN[which], total - 2 * SPLIT_W - PANE_MIN.values - other);
}
function applyPanes() {
  const total = el.panes.clientWidth;
  if (total > 0) {
    // the log gives way first, then the tree, so the values pane keeps its minimum
    panes.log = Math.max(PANE_MIN.log, Math.min(panes.log, total - 2 * SPLIT_W - PANE_MIN.values - PANE_MIN.tree));
    panes.tree = Math.max(PANE_MIN.tree, Math.min(panes.tree, paneMax("tree")));
  }
  el.panes.style.setProperty("--tree-w", Math.round(panes.tree) + "px");
  el.panes.style.setProperty("--log-w", Math.round(panes.log) + "px");
  for (const [sp, which] of [[el.splitTree, "tree"], [el.splitLog, "log"]]) {
    sp.setAttribute("aria-valuemin", String(PANE_MIN[which]));
    sp.setAttribute("aria-valuemax", String(Math.round(paneMax(which))));
    sp.setAttribute("aria-valuenow", String(Math.round(panes[which])));
  }
}
function setPane(which, w) {
  panes[which] = Math.max(PANE_MIN[which], Math.min(w, paneMax(which)));
  applyPanes();
  store(which === "tree" ? "devtool.treeW" : "devtool.logW", String(Math.round(panes[which])));
}
function initSplitter(sp, which) {
  const dir = which === "tree" ? 1 : -1;      // dragging right widens the tree, narrows the log
  let x0 = 0, w0 = 0;
  sp.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    sp.setPointerCapture(e.pointerId);
    sp.classList.add("drag");
    x0 = e.clientX; w0 = panes[which];
  });
  sp.addEventListener("pointermove", (e) => {
    if (sp.hasPointerCapture(e.pointerId)) setPane(which, w0 + dir * (e.clientX - x0));
  });
  const end = (e) => {
    if (sp.hasPointerCapture(e.pointerId)) sp.releasePointerCapture(e.pointerId);
    sp.classList.remove("drag");
  };
  sp.addEventListener("pointerup", end);
  sp.addEventListener("pointercancel", end);
  sp.addEventListener("dblclick", () => setPane(which, PANE_DEF[which]));
  sp.addEventListener("keydown", (e) => {
    const st = e.shiftKey ? 64 : 16;
    let w = null;
    if (e.key === "ArrowRight") w = panes[which] + dir * st;
    else if (e.key === "ArrowLeft") w = panes[which] - dir * st;
    else if (e.key === "Home") w = PANE_MIN[which];
    else if (e.key === "End") w = paneMax(which);
    if (w === null) return;
    e.preventDefault();
    setPane(which, w);
  });
}
initSplitter(el.splitTree, "tree");
initSplitter(el.splitLog, "log");
window.addEventListener("resize", applyPanes);

// ---------------------------------------------------------------------------
// Server-Sent Events (reconnects by itself, also across a devtool restart)
// ---------------------------------------------------------------------------
let es = null;
let retryTimer = 0;

function connect() {
  clearTimeout(retryTimer);
  if (es) es.close();
  es = new EventSource("/events");
  es.addEventListener("hello", () => {
    // A (re)connect replays the server's backlog: start the view over from it.
    state.online = true;
    for (const l of state.lines) l.el = null;
    state.lines = [];
    state.pausedAt = 0;
    el.flashOut.textContent = "";
    loadMeta();
    fullRender();
  });
  es.addEventListener("status", (e) => { state.status = JSON.parse(e.data); renderStatus(); });
  es.addEventListener("line", (e) => { const d = JSON.parse(e.data); addLine({ kind: "line", t: d.t, text: d.text }); });
  es.addEventListener("sep", (e) => { const d = JSON.parse(e.data); addLine({ kind: "sep", t: d.t, what: d.what }); });
  es.addEventListener("flash", (e) => onFlash(JSON.parse(e.data)));
  es.addEventListener("flashout", (e) => onFlashOut(JSON.parse(e.data)));
  es.addEventListener("tun", (e) => onTun(JSON.parse(e.data)));
  es.addEventListener("tval", (e) => onTval(JSON.parse(e.data)));
  es.addEventListener("terr", (e) => onTerr(JSON.parse(e.data)));
  es.onerror = () => {
    state.online = false;
    renderStatus();
    renderTunState();
    es.close();
    es = null;
    retryTimer = setTimeout(connect, 1000);
  };
}

const savedFilter = store("devtool.filter");
if (savedFilter) { el.filter.value = savedFilter; }
setFilter(el.filter.value);
el.search.value = tun.search;
applyPanes();
setChangesOpen(store("devtool.changesOpen") === "1");
fillPedals();
renderStatus();
renderFlash();
fullRender();
renderTunState();
renderChanges();
connect();
