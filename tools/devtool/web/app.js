// Pedal devtool page (steps 1 + 2): connection + build info, flash, live log,
// live constants (tree, rows, changes panel).
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
  flashTime: $("flash-time"), flashOut: $("flash-out"),
  filter: $("filter"), filterMode: $("filter-mode"), count: $("count"),
  pauseBtn: $("pause-btn"), clearBtn: $("clear-btn"), log: $("log"), jump: $("jump"),
  changesBtn: $("changes-btn"), changesCount: $("changes-count"), changesPanel: $("changes-panel"),
  changesList: $("changes-list"), changesSub: $("changes-sub"), resetAll: $("reset-all-btn"),
  note: $("note"), sidebar: $("sidebar"), search: $("search"), tree: $("tree"),
  navToggle: $("nav-toggle"), tabConst: $("tab-const"), tabLog: $("tab-log"), tabLogCount: $("tab-log-count"),
  viewConst: $("view-const"), viewLog: $("view-log"), tunState: $("tun-state"), guard: $("guard"),
  secHead: $("sec-head"), rows: $("rows"),
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
  el.note.textContent = note;
  el.note.hidden = !note;

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
  el.flashTime.textContent = f.t ? fmtTime(f.t) : "";
  renderFlashControls();
}

function onFlash(f) {
  const prev = state.flash.state;
  state.flash = f && f.state ? f : { active: false, state: "idle" };
  if (state.flash.state === "building" && prev !== "building") {
    el.flashOut.textContent = "";
    el.flashPanel.open = true;
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
  el.tabLogCount.textContent = total ? "(" + total + ")" : "";
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
// Live constants (step 2)
// The registry (labels, ranges, files) comes from devtool's `tun` snapshot;
// values come from the pedal (`tval` echoes). DOM is built once per registry
// structure / selection and then updated in place, so scroll position, open
// rows, focus, the search and the selected section survive value updates and
// reconnects.
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
  rowEls: new Map(),      // name -> {row, slider, num, chk, def, msg, chg}
  dragging: null,         // name of the slider under the pointer
  inflight: new Map(),    // name -> true while a POST runs
  queued: new Map(),      // name -> latest value waiting for the in-flight POST
  rowsKey: "",
};

function secKey(c) { return c.module + "/" + c.section; }
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
    const secs = snap.constants.map(secKey);
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

function renderTunState() {
  const s = tun.snap;
  el.tunState.textContent = "";
  if (!s) { el.tunState.textContent = "Waiting for devtool…"; return; }
  const txt = {
    offline: "No pedal: showing " + s.pedal + "'s constants files (defaults). Values arrive when a DIAG build connects.",
    asking: "Reading the pedal's values (list)…",
    synced: "Values from the pedal (" + s.constants.length + " live constants).",
    "no-answer": "The pedal does not answer `list` - flash a current DIAG build.",
  }[s.state] || s.state;
  el.tunState.appendChild(document.createTextNode(state.online ? txt : "devtool offline - reconnecting…"));
  if (s.last_err) {
    const e = document.createElement("div");
    e.className = "err";
    e.textContent = "Pedal error at " + fmtTime(s.last_err.t) + ": " + s.last_err.text;
    el.tunState.appendChild(e);
  }
}

function renderGuard() {
  const s = tun.snap;
  const g = s ? s.guard : null;
  el.guard.textContent = "";
  const reasons = g ? g.reasons.filter((r) => !(s.state === "offline" && /no pedal/.test(r))) : [];
  const warns = g ? g.warnings : [];
  if (!g || (!reasons.length && !warns.length)) { el.guard.hidden = true; return; }
  el.guard.hidden = false;
  el.guard.className = "guard" + (s.override && reasons.length ? " allowed" : "");
  if (reasons.length) {
    const p = document.createElement("p");
    p.textContent = s.override ? "Changes allowed for this session despite:" : "Changes are blocked:";
    el.guard.appendChild(p);
    const ul = document.createElement("ul");
    for (const r of reasons) { const li = document.createElement("li"); li.textContent = r; ul.appendChild(li); }
    el.guard.appendChild(ul);
    if (s.state === "synced") {
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = s.override ? "Block changes again" : "Allow changes anyway (this session)";
      b.addEventListener("click", () => post("/api/override", { on: !s.override }));
      el.guard.appendChild(b);
    }
  }
  if (warns.length) {
    const w = document.createElement("div");
    w.className = "warns";
    w.textContent = "Note: " + warns.join("; ");
    el.guard.appendChild(w);
  }
}

// --- tree ---------------------------------------------------------------
function treeGroups() {
  const s = tun.snap;
  const mods = new Map();       // module -> Map(section -> [constants])
  for (const c of s.constants) {
    if (!mods.has(c.module)) mods.set(c.module, new Map());
    const m = mods.get(c.module);
    if (!m.has(c.section)) m.set(c.section, []);
    m.get(c.section).push(c);
  }
  return mods;
}

function renderTree() {
  el.tree.textContent = "";
  const s = tun.snap;
  if (!s) return;
  const ped = document.createElement("div");
  ped.className = "t-pedal";
  ped.textContent = s.pedal;
  el.tree.appendChild(ped);
  if (!s.constants.length) {
    const e = document.createElement("div");
    e.className = "t-empty";
    e.textContent = "No live constants in this pedal's files.";
    el.tree.appendChild(e);
  }
  for (const [mod, secs] of treeGroups()) {
    const m = document.createElement("div");
    m.className = "t-mod";
    m.textContent = mod;
    el.tree.appendChild(m);
    for (const [sec, list] of secs) {
      const key = mod + "/" + sec;
      const b = document.createElement("button");
      b.type = "button";
      b.className = "t-sec";
      b.dataset.key = key;
      b.title = sec || "(no section)";
      const t = document.createElement("span");
      t.className = "t-title";
      t.textContent = sec || "(no section)";
      const chg = document.createElement("span");
      chg.className = "t-chg";
      const n = document.createElement("span");
      n.className = "t-n";
      b.append(t, chg, n);
      b.addEventListener("click", () => selectSection(key));
      el.tree.appendChild(b);
      b._list = list;
    }
  }
  updateTreeCounts();
}

function updateTreeCounts() {
  const q = tun.search.trim();
  for (const b of el.tree.querySelectorAll(".t-sec")) {
    const list = b._list.map((c) => tun.byName.get(c.name) || c);
    const hits = list.filter((c) => matchesSearch(c, q));
    const nchg = list.filter(isChanged).length;
    b.hidden = !!q && hits.length === 0;
    b.querySelector(".t-n").textContent = q ? hits.length + "/" + list.length : String(list.length);
    b.querySelector(".t-chg").textContent = nchg ? nchg + " changed" : "";
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
  setTab("const");
  el.sidebar.classList.remove("open");
  el.navToggle.setAttribute("aria-expanded", "false");
}

// --- rows -----------------------------------------------------------------
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
  let list;
  if (q) {
    list = s.constants.filter((c) => matchesSearch(c, q));
    h.textContent = "Search: " + list.length + (list.length === 1 ? " match" : " matches");
  } else {
    list = s.constants.filter((c) => secKey(c) === tun.sel);
    const c0 = list[0];
    h.textContent = c0 ? (c0.section || "(no section)") : "No section selected";
    if (c0) {
      const p = document.createElement("div");
      p.className = "path";
      p.textContent = c0.module + " - " + c0.file;
      el.secHead.append(h, p);
    }
  }
  if (!el.secHead.firstChild) el.secHead.appendChild(h);
  el.navToggle.textContent = q ? "Sections" : "Sections: " + (list[0] ? list[0].section : "–");
  let lastGroup = null;
  const frag = document.createDocumentFragment();
  for (const c of list) {
    if (q && secKey(c) !== lastGroup) {
      lastGroup = secKey(c);
      const g = document.createElement("div");
      g.className = "grp-title";
      g.textContent = c.module + " / " + (c.section || "(no section)");
      frag.appendChild(g);
    }
    frag.appendChild(makeTRow(c));
  }
  if (!list.length) {
    const e = document.createElement("div");
    e.className = "empty-rows";
    e.textContent = q ? "Nothing matches." : "No constants here.";
    frag.appendChild(e);
  }
  el.rows.appendChild(frag);
  for (const c of list) updateRow(c.name);
  if (!force) el.viewConst.scrollTop = scroll;
}

function makeTRow(c) {
  const row = document.createElement("div");
  row.className = "trow";
  row.dataset.name = c.name;
  const head = document.createElement("div");
  head.className = "thead";
  const tw = document.createElement("span");
  tw.className = "twisty";
  const label = document.createElement("span");
  label.className = "label";
  label.textContent = c.label;
  const name = document.createElement("span");
  name.className = "name";
  name.textContent = c.name;
  const flags = document.createElement("span");
  flags.className = "flags";
  const chg = document.createElement("span");
  chg.className = "badge chg";
  chg.textContent = "changed";
  const lvl = document.createElement("span");
  lvl.className = "badge " + c.level;
  lvl.textContent = c.level;
  lvl.title = c.when || LEVEL_TIP[c.level] || "";
  flags.append(chg, lvl);
  head.append(tw, label, name, flags);

  const ctl = document.createElement("div");
  ctl.className = "tctl";
  const r = { row, chg, tw };
  if (c.type === "bool") {
    const lab = document.createElement("label");
    lab.className = "tog";
    const chk = document.createElement("input");
    chk.type = "checkbox";
    const txt = document.createElement("span");
    lab.append(chk, txt);
    chk.addEventListener("change", () => sendSet(c.name, chk.checked));
    r.chk = chk; r.chkTxt = txt;
    ctl.appendChild(lab);
  } else {
    const sl = document.createElement("input");
    sl.type = "range";
    sl.min = "0"; sl.max = String(SLIDER_STEPS); sl.step = "1";
    sl.setAttribute("aria-label", c.label);
    const num = document.createElement("input");
    num.type = "text";
    num.inputMode = "decimal";
    num.className = "num";
    num.setAttribute("aria-label", c.label + " value");
    num.spellcheck = false;
    const unit = document.createElement("span");
    unit.className = "unit";
    unit.textContent = c.unit;
    sl.addEventListener("pointerdown", () => { tun.dragging = c.name; });
    sl.addEventListener("pointerup", () => { tun.dragging = null; });
    sl.addEventListener("change", () => { tun.dragging = null; });   // drag released anywhere
    sl.addEventListener("input", () => {
      const cc = tun.byName.get(c.name);
      const v = fromPos(cc, +sl.value);
      if (document.activeElement !== num) num.value = fmtNum(v, cc.unit, cc.type, true);
      sendSet(c.name, v);
    });
    const commit = () => {
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
    };
    num.addEventListener("keydown", (e) => {
      if (e.key === "Enter") { e.preventDefault(); commit(); num.select(); }
      if (e.key === "Escape") { updateRow(c.name, true); num.blur(); }
    });
    num.addEventListener("change", commit);
    num.addEventListener("blur", () => updateRow(c.name, true));
    r.slider = sl; r.num = num;
    sl.hidden = !c.range;   // (a constant without @range: number field only; devtool flags the tag)
    ctl.append(sl, num, unit);
  }
  const def = document.createElement("span");
  def.className = "def";
  def.textContent = "default " + fmtWithUnit(c.default, c.unit, c.type);
  def.title = "Compiled default: " + c.default_text + "  (" + c.file + ":" + c.line + ")";
  const rst = document.createElement("button");
  rst.type = "button";
  rst.className = "rst";
  rst.textContent = "Reset";
  rst.title = "Back to the compiled default";
  rst.addEventListener("click", () => sendReset(c.name));
  r.rst = rst;
  ctl.append(def, rst);

  const msg = document.createElement("div");
  msg.className = "tmsg";
  r.msg = msg;

  const exp = document.createElement("div");
  exp.className = "texp";
  const docs = (tun.snap && tun.snap.docs) || {};
  if (c.note) {
    const n = document.createElement("pre");
    n.className = "note-txt";
    n.textContent = c.note;
    exp.appendChild(n);
  }
  if (c.doc_of && docs[c.doc_of]) {
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
  r.exp = exp;
  const setOpen = (o) => {
    exp.hidden = !o;
    tw.textContent = o ? "▾" : "▸";
    head.setAttribute("aria-expanded", String(o));
  };
  setOpen(tun.open.has(c.name));
  head.tabIndex = 0;
  head.setAttribute("role", "button");
  const toggle = () => {
    const o = exp.hidden;
    if (o) tun.open.add(c.name); else tun.open.delete(c.name);
    store("devtool.openRows", JSON.stringify([...tun.open]));
    setOpen(o);
  };
  head.addEventListener("click", (e) => { if (!e.target.closest("button")) toggle(); });
  head.addEventListener("keydown", (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); toggle(); } });

  row.append(head, ctl, msg, exp);
  tun.rowEls.set(c.name, r);
  return row;
}

function showMsg(name, text) {
  const r = tun.rowEls.get(name);
  if (r) r.msg.textContent = text || "";
}

// Value -> controls of one row (skips a field the user is typing in / a slider being dragged).
function updateRow(name, forceNum) {
  const r = tun.rowEls.get(name);
  const c = tun.byName.get(name);
  if (!r || !c) return;
  const has = c.value !== null && c.value !== undefined;
  const v = has ? c.value : c.default;
  const off = c.in_firmware === false;
  const can = writable() && !off;
  r.row.classList.toggle("changed", isChanged(c));
  r.row.classList.toggle("off", off);
  r.row.classList.toggle("pending", tun.inflight.has(name) || tun.queued.has(name));
  r.chg.hidden = !isChanged(c);
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
  if (r) r.row.scrollIntoView({ block: "center" });
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

// --- tabs / search / narrow-pane section menu ----------------------------------
function setTab(which) {
  const isLog = which === "log";
  el.viewLog.hidden = !isLog;
  el.viewConst.hidden = isLog;
  el.tabLog.setAttribute("aria-selected", String(isLog));
  el.tabConst.setAttribute("aria-selected", String(!isLog));
  store("devtool.tab", which);
  if (isLog && state.stick) scrollToBottom();
}
el.tabConst.addEventListener("click", () => setTab("const"));
el.tabLog.addEventListener("click", () => setTab("log"));
el.navToggle.addEventListener("click", () => {
  const o = !el.sidebar.classList.contains("open");
  el.sidebar.classList.toggle("open", o);
  el.navToggle.setAttribute("aria-expanded", String(o));
});
let searchTimer = 0;
el.search.addEventListener("input", () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(() => {
    tun.search = el.search.value;
    store("devtool.search", tun.search);
    if (!tun.snap) return;
    updateTreeCounts();
    renderRows(true);
    if (tun.search.trim()) setTab("const");
  }, 120);
});
el.search.addEventListener("keydown", (e) => {
  if (e.key === "Escape") { el.search.value = ""; el.search.dispatchEvent(new Event("input")); }
});

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
setTab(store("devtool.tab") === "log" ? "log" : "const");
setChangesOpen(store("devtool.changesOpen") === "1");
fillPedals();
renderStatus();
renderFlash();
fullRender();
renderTunState();
renderChanges();
connect();
