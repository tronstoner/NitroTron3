// Pedal devtool page: top bar (connection, the pedal's place, changes, flash), left nav (benches -> modes),
// main view per mode (knob strip, section cards with rotary knobs / fader banks / toggles, the preset grid),
// bottom drawer (log + flash output). Formatting, taper and state-line parsing live in core.js.
// No build step, no external assets. Talks to tools/devtool/devtool.py (HTTP + SSE).
"use strict";

const MAX_LINES = 20000;
const $ = (id) => document.getElementById(id);
const el = {
  dot: $("conn-dot"), connText: $("conn-text"), pedal: $("b-pedal"), hash: $("b-hash"), conn: $("conn"),
  where: $("where"), whereBench: $("where-bench"), whereMode: $("where-mode"), whereK1: $("where-k1"),
  flashPedal: $("flash-pedal"), flashBtn: $("flash-btn"), flashBadge: $("flash-badge"), flashMsg: $("flash-msg"),
  flashOut: $("flash-out"), filter: $("filter"), filterMode: $("filter-mode"), count: $("count"),
  pauseBtn: $("pause-btn"), clearBtn: $("clear-btn"), log: $("log"), jump: $("jump"), logTools: $("log-tools"),
  changesBtn: $("changes-btn"), changesCount: $("changes-count"), changesWord: $("changes-word"),
  changesPop: $("changes-pop"), changesList: $("changes-list"), changesSub: $("changes-sub"),
  resetAll: $("reset-all-btn"), note: $("note"),
  banner: $("banner"), bannerText: $("banner-text"), guardBtn: $("guard-btn"),
  nav: $("nav"), navTree: $("nav-tree"), navToggle: $("nav-toggle"), search: $("search"),
  view: $("view"), viewTitle: $("view-title"), legend: $("legend"), strip: $("strip"), sections: $("sections"),
  drawer: $("drawer"), drawerGrip: $("drawer-grip"), drawerToggle: $("drawer-toggle"), drawerLast: $("drawer-last"),
  tabLog: $("tab-log"), tabFlash: $("tab-flash"), pop: $("pop"), sprite: $("icon-sprite"),
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
  pausedAt: 0,
  clearedAt: 0,
  stick: true,
  filter: { text: "", rx: null, isRegex: true },
  st: null,               // the latest parsed state line (core.js parseState) + its time
  active: null,           // {bench, sw1, preset} from the state lines
};

function store(key, val) {
  try {
    if (val === undefined) return localStorage.getItem(key);
    localStorage.setItem(key, val);
  } catch (e) { /* storage blocked: fine */ }
  return null;
}
function h(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined && text !== null) e.textContent = text;
  return e;
}

// ---------------------------------------------------------------------------
// Icons: the manual's knob icons (docs/gen_icons.py --tool -> icons/icons.svg), inlined once as <symbol>s.
// Two small UI glyphs (reset, info) are drawn here.
// ---------------------------------------------------------------------------
const SVGNS = "http://www.w3.org/2000/svg";
function icon(name, cls) {
  const s = document.createElementNS(SVGNS, "svg");
  s.setAttribute("class", "ic" + (cls ? " " + cls : ""));
  s.setAttribute("aria-hidden", "true");
  const u = document.createElementNS(SVGNS, "use");
  u.setAttribute("href", "#ic-" + name);
  s.appendChild(u);
  return s;
}
function glyph(kind) {
  const s = document.createElementNS(SVGNS, "svg");
  s.setAttribute("viewBox", "0 0 16 16");
  s.setAttribute("aria-hidden", "true");
  s.innerHTML = kind === "reset"
    ? '<path d="M3.2 8a4.8 4.8 0 1 0 1.5-3.5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/><path d="M3 1.8v3.6h3.6" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>'
    : '<circle cx="8" cy="8" r="6.6" fill="none" stroke="currentColor" stroke-width="1.4"/><rect x="7.2" y="7" width="1.6" height="4.6" rx=".8" fill="currentColor"/><circle cx="8" cy="4.8" r="1" fill="currentColor"/>';
  return s;
}
fetch("icons/icons.svg", { cache: "no-store" }).then((r) => (r.ok ? r.text() : "")).then((t) => {
  el.sprite.innerHTML = t;
}).catch(() => {});

// ---------------------------------------------------------------------------
// Status / build (top bar)
// ---------------------------------------------------------------------------
function renderStatus() {
  const s = state.status;
  let dot = "", text;
  if (!state.online) { dot = "bad"; text = "devtool offline - reconnecting…"; }
  else if (!s || !s.connected) { dot = ""; text = "waiting for pedal"; }
  else if (s.build_state === "ok") { dot = "ok"; text = ""; }
  else if (s.build_state === "asking") { dot = "warn"; text = "asking for build…"; }
  else { dot = "warn"; text = "no build answer (non-DIAG firmware?)"; }
  el.dot.className = "dot " + dot;
  el.connText.textContent = text;
  const b = s && s.connected ? s.build : null;
  el.pedal.textContent = b ? b.pedal : "no pedal";
  el.hash.textContent = b ? b.hash : "";
  if (b && b.dirty) el.hash.appendChild(h("span", "dirty", "dirty"));
  const bt = b ? parseBuildTime(b.time) : null;
  el.conn.title = (b ? "Firmware " + b.hash + (b.dirty ? " (dirty)" : "") + ", built " +
    (bt ? fmtTime(bt, TIME_FORMATS.build) : b.time) + "\n" : "") +
    (s && s.dev ? s.dev + (s.log_file ? "  ->  " + s.log_file : "") : "") +
    (s && s.connected && s.note ? "\n" + s.note : "");
  if (!state.userPicked && !state.flash.active) {
    el.flashPedal.value = b && state.pedals.includes(b.pedal) ? b.pedal : state.defaultPedal;
  }
  renderFlashControls();
  renderBanner();
}

function fillPedals() {
  const cur = el.flashPedal.value;
  el.flashPedal.textContent = "";
  for (const p of state.pedals) {
    const o = h("option", "", p);
    o.value = p;
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
// Flash: status in the drawer line (never opens anything by itself), output in the drawer's Flash tab.
// ---------------------------------------------------------------------------
const FLASH_LABEL = {
  idle: "idle", building: "building", "entering-dfu": "entering DFU",
  gesture: "footswitches", programming: "programming", done: "done", failed: "failed",
};
let flashLast = "";

function renderFlashControls() {
  const busy = !!state.flash.active;
  el.flashBtn.disabled = busy || !state.online;
  el.flashPedal.disabled = busy;
  el.flashBtn.textContent = busy ? "Flashing…" : "Flash";
}

function renderFlash() {
  const f = state.flash;
  const st = f.state || "idle";
  el.flashBadge.textContent = "flash: " + (FLASH_LABEL[st] || st);
  el.flashBadge.className = "flash-badge " + (
    st === "done" ? "ok" : st === "failed" ? "bad" : st === "gesture" ? "attn" : f.active ? "run" : "");
  const msg = f.message ? (f.pedal ? f.pedal + ": " : "") + f.message : "";
  // while running, the latest output line (progress) follows the message
  el.flashMsg.textContent = f.active && flashLast ? (msg ? msg + "  ·  " : "") + flashLast : msg;
  el.flashMsg.className = "flash-msg" + (st === "gesture" ? " attn" : "");
  el.flashMsg.title = el.flashMsg.textContent + (f.t ? "\n" + fmtTime(f.t) : "");
  renderFlashControls();
}

function onFlash(f) {
  const prev = state.flash.state;
  state.flash = f && f.state ? f : { active: false, state: "idle" };
  if (state.flash.state === "building" && prev !== "building") { el.flashOut.textContent = ""; flashLast = ""; }
  renderFlash();
}

function onFlashOut(d) {
  const out = el.flashOut;
  const atBottom = out.scrollTop + out.clientHeight >= out.scrollHeight - 4;
  // '\r' progress lines (dfu-util): replace the last line, as a terminal does
  if (d.replace && out.lastChild) out.removeChild(out.lastChild);
  const line = h("div", d.stream === "info" ? "info" : "");
  line.appendChild(h("span", "ts", fmtTime(d.t)));
  line.appendChild(document.createTextNode(d.text));
  out.appendChild(line);
  while (out.childNodes.length > 4000) out.removeChild(out.firstChild);
  if (atBottom) out.scrollTop = out.scrollHeight;
  flashLast = d.text;
  renderFlash();
}

async function startFlash() {
  const pedal = el.flashPedal.value;
  el.flashBtn.disabled = true;
  const res = await post("/api/flash", { pedal });
  if (!res.ok) {
    el.flashMsg.textContent = "not started: " + res.message;
    el.flashMsg.className = "flash-msg attn";
  }
  renderFlashControls();
}
el.flashPedal.addEventListener("change", () => { state.userPicked = true; });
el.flashBtn.addEventListener("click", startFlash);

// ---------------------------------------------------------------------------
// Log (drawer)
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
    frag.appendChild(h("mark", "", text.slice(a, b)));
    pos = b;
  }
  if (pos < text.length) frag.appendChild(document.createTextNode(text.slice(pos)));
  return frag;
}
function makeRow(l) {
  const row = h("div", l.kind === "sep" ? "row sep" : "row");
  row.appendChild(h("span", "ts", fmtTime(l.t)));
  const tx = h("span", "tx");
  if (l.kind === "sep") tx.textContent = l.what === "gone" ? "pedal gone" : "pedal back";
  else tx.appendChild(highlighted(l.text));
  row.appendChild(tx);
  return row;
}
function visible(l) { return l.t > state.clearedAt && (l.kind === "sep" || matches(l.text)); }
function renderCount() {
  const total = state.lines.filter((l) => l.kind === "line" && l.t > state.clearedAt).length;
  let s = total + (total === 1 ? " line" : " lines");
  if (state.paused) s += " - paused, " + (state.lines.length - state.pausedAt) + " new";
  el.count.textContent = s;
}
function renderEmpty() {
  if (el.log.querySelector(".row")) return;
  if (!el.log.querySelector(".empty"))
    el.log.appendChild(h("div", "empty", state.lines.length ? "No lines match the filter." : "No log lines yet."));
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
  if (state.stick) el.log.scrollTop = el.log.scrollHeight;
}
function renderLastLine(l) {
  el.drawerLast.textContent = "";
  if (!l) return;
  el.drawerLast.appendChild(h("span", "ts", fmtTime(l.t)));
  el.drawerLast.appendChild(document.createTextNode(l.kind === "sep" ? (l.what === "gone" ? "pedal gone" : "pedal back") : l.text));
  el.drawerLast.title = l.kind === "sep" ? "" : l.text;
}
function addLine(l) {
  if (l.kind === "line") onStateLine(l.text, l.t);
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
    if (state.stick) el.log.scrollTop = el.log.scrollHeight;
  }
  scheduleLogPaint(l);
}
let logPaintPending = false, logPaintLine = null;
function scheduleLogPaint(l) {
  logPaintLine = l;
  if (logPaintPending) return;
  logPaintPending = true;
  requestAnimationFrame(() => { logPaintPending = false; renderCount(); renderLastLine(logPaintLine); });
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
  el.jump.hidden = state.stick || el.log.hidden;
});
el.jump.addEventListener("click", () => { state.stick = true; el.jump.hidden = true; el.log.scrollTop = el.log.scrollHeight; });
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
  filterTimer = setTimeout(() => { setFilter(el.filter.value); store("devtool.filter", el.filter.value); fullRender(); }, 120);
});
el.filter.addEventListener("keydown", (e) => {
  if (e.key === "Escape") { el.filter.value = ""; el.filter.dispatchEvent(new Event("input")); }
});

// --- drawer: one line collapsed; open to a remembered, draggable height; Log / Flash output tabs ---
const DRAWER_MIN = 120;
const drawer = {
  open: store("devtool.drawerOpen") === "1",
  h: Number(store("devtool.drawerH")) || 300,
  tab: store("devtool.drawerTab") === "flash" ? "flash" : "log",
};
function drawerMax() { return Math.max(DRAWER_MIN, window.innerHeight - 200); }
function applyDrawer() {
  drawer.h = Math.max(DRAWER_MIN, Math.min(drawer.h, drawerMax()));
  el.drawer.classList.toggle("open", drawer.open);
  el.drawer.style.setProperty("--drawer-h", Math.round(drawer.h) + "px");
  el.drawerToggle.setAttribute("aria-expanded", String(drawer.open));
  el.drawerGrip.setAttribute("aria-valuenow", String(Math.round(drawer.h)));
  const lg = drawer.tab === "log";
  el.tabLog.setAttribute("aria-selected", String(lg));
  el.tabFlash.setAttribute("aria-selected", String(!lg));
  el.log.hidden = !lg;
  el.flashOut.hidden = lg;
  el.logTools.hidden = !lg;
  el.jump.hidden = !lg || state.stick;
  if (typeof applyDock === "function" && drawer.dock) applyDock();
}
// Dock: the log panel sits at the bottom (drawer) or as a right column; the status line stays at the bottom either way.
drawer.dock = store("devtool.drawerDock") === "right" ? "right" : "bottom";
function applyDock() {
  const body = document.getElementById("drawer-body"), work = document.getElementById("work");
  const right = drawer.dock === "right";
  if (right && body.parentElement !== work) work.appendChild(body);
  if (!right && body.parentElement !== el.drawer) el.drawer.appendChild(body);
  work.classList.toggle("logright", right && drawer.open);
  el.drawer.classList.toggle("dock-right", right);
  document.getElementById("drawer-dock").textContent = right ? "Log bottom" : "Log right";
}
document.getElementById("drawer-dock").addEventListener("click", () => {
  drawer.dock = drawer.dock === "right" ? "bottom" : "right";
  store("devtool.drawerDock", drawer.dock);
  if (!drawer.open) setDrawer(true); else applyDock();
});
function setDrawer(open) {
  drawer.open = open;
  store("devtool.drawerOpen", open ? "1" : "0");
  applyDrawer();
  if (open && drawer.tab === "log" && state.stick) el.log.scrollTop = el.log.scrollHeight;
}
function setTab(t) {
  drawer.tab = t;
  store("devtool.drawerTab", t);
  applyDrawer();
}
el.drawerToggle.addEventListener("click", () => setDrawer(!drawer.open));
el.drawerLast.addEventListener("click", () => { setTab("log"); setDrawer(true); });
el.flashBadge.addEventListener("click", () => { setTab("flash"); setDrawer(true); });
el.tabLog.addEventListener("click", () => setTab("log"));
el.tabFlash.addEventListener("click", () => setTab("flash"));
(function initGrip() {
  const g = el.drawerGrip;
  let y0 = 0, h0 = 0, moved = false;
  g.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    g.setPointerCapture(e.pointerId);
    g.classList.add("drag");
    y0 = e.clientY; h0 = drawer.open ? drawer.h : 0; moved = false;
  });
  g.addEventListener("pointermove", (e) => {
    if (!g.hasPointerCapture(e.pointerId)) return;
    const dy = y0 - e.clientY;
    if (Math.abs(dy) < 3 && !moved) return;
    moved = true;
    drawer.h = h0 + dy;
    drawer.open = drawer.h >= DRAWER_MIN * 0.6;
    applyDrawer();
  });
  const end = (e) => {
    if (!g.hasPointerCapture(e.pointerId)) return;
    g.releasePointerCapture(e.pointerId);
    g.classList.remove("drag");
    if (!moved) { setDrawer(!drawer.open); return; }
    store("devtool.drawerH", String(Math.round(drawer.h)));
    setDrawer(drawer.open);
  };
  g.addEventListener("pointerup", end);
  g.addEventListener("pointercancel", end);
  g.addEventListener("keydown", (e) => {
    const st = e.shiftKey ? 80 : 20;
    if (e.key === "ArrowUp") drawer.h += st;
    else if (e.key === "ArrowDown") drawer.h -= st;
    else if (e.key === "Enter" || e.key === " ") { e.preventDefault(); setDrawer(!drawer.open); return; }
    else return;
    e.preventDefault();
    drawer.open = true;
    applyDrawer();
    store("devtool.drawerH", String(Math.round(drawer.h)));
  });
})();
window.addEventListener("resize", applyDrawer);

// ---------------------------------------------------------------------------
// The pedal's current place, from its DIAG state lines (core.js parseState): top bar, nav dots, knob strip
// values, the preset grid's "now" row. Only these lines change it; it stays through reconnects.
// ---------------------------------------------------------------------------
function onStateLine(text, t) {
  const p = parseState(text);
  if (!p) return;
  p.t = t;
  state.st = p;
  const a = state.active;
  const moved = !a || a.bench !== p.bench || a.sw1 !== p.sw1 || a.preset !== p.preset;
  state.active = { bench: p.bench, sw1: p.sw1, preset: p.preset };
  scheduleStatePaint(moved);
}
let statePaintPending = false, statePaintMoved = false;
function scheduleStatePaint(moved) {
  statePaintMoved = statePaintMoved || moved;
  if (statePaintPending) return;
  statePaintPending = true;
  requestAnimationFrame(() => {
    statePaintPending = false;
    const m = statePaintMoved;
    statePaintMoved = false;
    renderWhere();
    paintStrip();
    paintWt();
    if (m) { renderNavActive(); renderPresetNow(); renderViewTitle(); }
  });
}
function renderWhere() {
  const p = state.st;
  if (!p) return;
  const b = benchOf(p.bench);
  const am = activeModeId();
  const m = b && am ? b.modes.find((x) => b.id + "/" + x.id === am) : null;
  el.whereBench.textContent = b ? b.name : "Bench " + p.bench;
  el.whereMode.textContent = m ? m.name : "SW1 " + p.sw1;
  el.whereK1.textContent = "K1 " + p.sel + (p.of ? " (" + p.of + ")" : "");
  el.where.title = "At " + fmtTime(p.t) + " - SW1 " + p.sw1 + (p.oct ? ", oct " + p.oct : "");
}

// ---------------------------------------------------------------------------
// Live constants: registry (devtool's `tun` snapshot) + the pedal's values (`tval`).
// The view is built once per structure / selection, then updated in place, so selection, open detail
// rows, scroll, focus and drags survive value updates and reconnects.
// ---------------------------------------------------------------------------
const LEVEL_TIP = {
  live: "Live: heard at once (next audio block).",
  reactivate: "Takes effect after the next mode / zone change.",
};
const DIR_ENDS = { ccw: "ccw", noon: "noon", cw: "cw" };
const tun = {
  snap: null,
  byName: new Map(),
  structKey: "",
  viewKey: store("devtool.view") || "",
  search: store("devtool.search") || "",
  openCell: store("devtool.openCell") || "",      // preset grid: the constant whose detail row is open
  compare: store("devtool.compare") || "",        // preset grid: the field shown as a fader bank
  ctls: new Map(),        // name -> [control {update()}]
  cardOf: new Map(),      // name -> section card record {msg, chg, list}
  dragging: null,
  local: new Map(),       // name -> value sent but not yet echoed by the pedal (shown meanwhile: no snap-back)
  inflight: new Map(),
  queued: new Map(),
  scroll: new Map(),      // viewKey -> scrollTop
  builtKey: "",
};

function layoutOf() { return (tun.snap && tun.snap.layout) || null; }
function benchOf(id) { const L = layoutOf(); return L ? L.benches.find((b) => b.id === id) : null; }
function modeOf(key) {
  const [bid, mid] = key.split("/");
  const b = benchOf(bid);
  return b ? { bench: b, mode: b.modes.find((x) => x.id === mid) } : null;
}
function placeName(bench, mode) {
  const b = benchOf(bench);
  const m = b && b.modes.find((x) => x.id === mode);
  return (b ? b.name : "Bench " + bench) + " / " + (m ? m.name : mode);
}
function activeModeId() {
  const a = state.active, b = a && benchOf(a.bench);
  if (!b) return null;
  const m = b.modes.find((x) => x.sw1 === a.sw1);
  return m ? b.id + "/" + m.id : null;
}
function targetName(id) {
  const t = ((tun.snap && tun.snap.targets) || []).find((x) => x.id === id);
  return t ? t.name : "";
}
function cur(c) {
  if (tun.local.has(c.name)) return tun.local.get(c.name);
  const cc = tun.byName.get(c.name) || c;
  return cc.value !== null && cc.value !== undefined ? cc.value : cc.default;
}
function isChanged(c) {
  c = tun.byName.get(c.name) || c;
  if (c.value === null || c.value === undefined || c.default === null) return false;
  if (c.type === "float") return Math.fround(c.value) !== Math.fround(c.default);
  return c.value !== c.default;
}
function writable() { return !!(tun.snap && tun.snap.writable && state.online); }
function canEdit(c) { c = tun.byName.get(c.name) || c; return writable() && c.in_firmware !== false; }
function canReset() { return !!(tun.snap && tun.snap.state === "synced" && state.online); }
function isPending(name) { return tun.inflight.has(name) || tun.queued.has(name); }
function pairOf(c) {
  const ps = tun.snap && tun.snap.pairs;
  return c.pair && ps && ps[c.pair.id] ? ps[c.pair.id] : null;
}
// the view keys a constant appears in: "<bench>/<mode>" (layout) or "mod:<module>" (no places)
function viewKeysOf(c) {
  return c.places && c.places.length ? [...new Set(c.places.map((p) => p.bench + "/" + p.mode))] : ["mod:" + c.module];
}
function sectionIn(c, viewKey) {
  const p = (c.places || []).find((x) => x.bench + "/" + x.mode === viewKey);
  return p ? p.section : (c.display_section || c.section || "(no section)");
}
function sectionOrder(title) { const m = /\bK(\d)\b/.exec(title || ""); return m ? Number(m[1]) : 9; }
function matchesSearch(c, q) {
  if (!q) return true;
  const hay = (c.name + "\n" + c.label + "\n" + c.note + "\n" + ((tun.snap.docs || {})[c.doc_of] || "")).toLowerCase();
  return q.toLowerCase().split(/\s+/).filter(Boolean).every((w) => hay.includes(w));
}
function sharedWith(c, viewKey) {
  if (!c.places || c.places.length < 2) return [];
  return [...new Set(c.places.filter((x) => x.bench + "/" + x.mode !== viewKey).map((x) => placeName(x.bench, x.mode)))];
}

function onTun(snap) {
  tun.snap = snap;
  tun.byName = new Map(snap.constants.map((c) => [c.name, c]));
  tun.local.clear();
  const key = snap.pedal + "|" + snap.constants.map((c) => c.name).join(",");
  if (key !== tun.structKey) {
    tun.structKey = key;
    const keys = allViewKeys();
    if (!keys.includes(tun.viewKey)) tun.viewKey = activeModeId() && keys.includes(activeModeId()) ? activeModeId() : (keys[0] || "");
    renderLegend();
    renderNav();
    renderView(true);
  } else {
    updateAll();
  }
  renderBanner();
  renderChanges();
  if (document.activeElement !== el.note) el.note.value = snap.note || "";
}
function onTval(d) {
  const c = tun.byName.get(d.name);
  if (!c) return;
  c.value = d.value;
  if (!isPending(d.name)) tun.local.delete(d.name);
  updateName(d.name);
  renderNavCounts();
  renderChanges();
}
function onTerr(d) {
  if (tun.snap) tun.snap.last_err = d;
  renderBanner();
}

// --- banner: pedal / guard state, only when there is something to say ---
function renderBanner() {
  const s = tun.snap;
  const parts = [];
  let warn = false, btn = false;
  if (!state.online) { parts.push("devtool offline - reconnecting…"); warn = true; }
  else if (s) {
    const st = {
      offline: "No pedal: showing " + s.pedal + "'s constants files (compiled defaults). Values arrive when a DIAG build connects.",
      asking: "Reading the pedal's values…",
      "no-answer": "The pedal does not answer `list` - flash a current DIAG build.",
    }[s.state];
    if (st) parts.push(st);
    const g = s.guard || { reasons: [], warnings: [] };
    const reasons = g.reasons.filter((r) => !(s.state === "offline" && /no pedal/.test(r)));
    if (reasons.length) {
      parts.push((s.override ? "Changes allowed despite: " : "Changes blocked: ") + reasons.join("; ") + ".");
      warn = !s.override;
      btn = s.state === "synced";
    }
    if (g.warnings.length) parts.push("Note: " + g.warnings.join("; ") + ".");
  }
  el.bannerText.textContent = parts.join("  ");
  if (state.online && s && s.last_err) {
    el.bannerText.appendChild(h("span", "err", "  Pedal error at " + fmtTime(s.last_err.t) + ": " + s.last_err.text));
  }
  el.banner.hidden = !el.bannerText.textContent;
  el.banner.classList.toggle("quiet", !warn);
  el.guardBtn.hidden = !btn;
  if (btn) el.guardBtn.textContent = s.override ? "Block changes again" : "Allow changes anyway";
}
el.guardBtn.addEventListener("click", () => { if (tun.snap) post("/api/override", { on: !tun.snap.override }); });

// --- legend -----------------------------------------------------------------
function renderLegend() {
  el.legend.textContent = "";
  for (const t of (tun.snap && tun.snap.targets) || []) {
    const li = h("li");
    li.dataset.target = t.id;
    li.append(h("i"), document.createTextNode(t.name));
    el.legend.appendChild(li);
  }
}

// --- nav: benches (headings) -> modes (items); constants without a place: per module ---
function allViewKeys() {
  const out = [];
  const L = layoutOf();
  if (L) for (const b of L.benches) for (const m of b.modes) out.push(b.id + "/" + m.id);
  for (const c of tun.snap.constants) for (const k of viewKeysOf(c)) if (!out.includes(k)) out.push(k);
  return out;
}
function listFor(viewKey) { return tun.snap.constants.filter((c) => viewKeysOf(c).includes(viewKey)); }
function navItem(key, name, title) {
  const b = h("button", "nav-item");
  b.type = "button";
  b.dataset.key = key;
  b.title = title || "";
  b.append(h("span", "live"), h("span", "nname", name), h("span", "nchg"));
  b.addEventListener("click", () => selectView(key));
  return b;
}
function renderNav() {
  el.navTree.textContent = "";
  const s = tun.snap;
  const L = layoutOf();
  if (L) {
    for (const b of L.benches) {
      el.navTree.appendChild(h("div", "nav-bench", b.name));
      if (!b.modes.length) el.navTree.appendChild(h("div", "nav-none", "No live constants yet"));
      for (const m of b.modes) el.navTree.appendChild(navItem(b.id + "/" + m.id, m.name, "SW3 " + b.sw3 + " · SW1 " + m.sw1));
    }
  }
  const mods = [...new Set(s.constants.filter((c) => !(c.places && c.places.length)).map((c) => c.module))];
  if (mods.length) {
    el.navTree.appendChild(h("div", "nav-bench", L ? "Not placed" : (s.pedal + " modules")));
    for (const m of mods) el.navTree.appendChild(navItem("mod:" + m, m, ""));
  }
  if (!s.constants.length) el.navTree.appendChild(h("div", "nav-none", "No live constants in this pedal's files."));
  if (L) {
    const lg = h("div", "nav-legend");
    lg.append(h("span", "live"), document.createTextNode("on the pedal now"));
    el.navTree.appendChild(lg);
  }
  renderNavActive();
  renderNavCounts();
}
function renderNavActive() {
  const am = activeModeId();
  for (const b of el.navTree.querySelectorAll(".nav-item")) {
    b.classList.toggle("active", b.dataset.key === am);
    b.setAttribute("aria-current", String(!tun.search.trim() && b.dataset.key === tun.viewKey));
  }
}
function renderNavCounts() {
  if (!tun.snap) return;
  for (const b of el.navTree.querySelectorAll(".nav-item")) {
    const n = listFor(b.dataset.key).filter(isChanged).length;
    const e = b.querySelector(".nchg");
    e.textContent = n ? String(n) : "";
    e.title = n ? n + " changed" : "";
  }
}
function selectView(key) {
  tun.scroll.set(tun.viewKey, el.view.scrollTop);
  tun.viewKey = key;
  store("devtool.view", key);
  if (tun.search) { tun.search = ""; el.search.value = ""; store("devtool.search", ""); }
  renderNavActive();
  renderView(true);
  el.view.scrollTop = tun.scroll.get(key) || 0;
  setNavOpen(false);
}

// ---------------------------------------------------------------------------
// The main view
// ---------------------------------------------------------------------------
function renderViewTitle() {
  el.viewTitle.textContent = "";
  const q = tun.search.trim();
  if (q) {
    el.viewTitle.append(h("h1", "", "Search"), h("span", "sub", "“" + q + "”"));
    return;
  }
  const mo = modeOf(tun.viewKey);
  if (mo && mo.mode) {
    el.viewTitle.append(h("h1", "", mo.mode.name),
      h("span", "sub", mo.bench.name + "  ·  SW3 " + mo.bench.sw3 + "  ·  SW1 " + mo.mode.sw1));
    if (activeModeId() === tun.viewKey) el.viewTitle.appendChild(h("span", "badge-live", "● on the pedal now"));
  } else {
    el.viewTitle.append(h("h1", "", tun.viewKey.replace(/^mod:/, "")), h("span", "sub", tun.snap ? tun.snap.pedal : ""));
  }
}

function renderView(force) {
  const s = tun.snap;
  if (!s) return;
  const q = tun.search.trim();
  const key = tun.structKey + "|" + (q ? "?" + q : tun.viewKey);
  if (!force && key === tun.builtKey) return;
  tun.builtKey = key;
  closePop();
  tun.ctls.clear();
  tun.cardOf.clear();
  renderViewTitle();
  renderStrip();
  el.sections.textContent = "";
  const frag = document.createDocumentFragment();
  if (q) {
    const hits = s.constants.filter((c) => matchesSearch(c, q));
    const groups = new Map();         // "<view>|<section>" -> constants
    for (const c of hits) {
      const vk = viewKeysOf(c)[0];
      const k = vk + "|" + sectionIn(c, vk);
      if (!groups.has(k)) groups.set(k, []);
      groups.get(k).push(c);
    }
    for (const [k, list] of groups) {
      const vk = k.split("|")[0];
      const mo = modeOf(vk);
      frag.appendChild(buildSection(k.slice(vk.length + 1), list, vk,
        mo && mo.mode ? mo.bench.name + " / " + mo.mode.name : vk.replace(/^mod:/, "")));
    }
    if (!hits.length) frag.appendChild(h("div", "empty-view", "Nothing matches “" + q + "”."));
  } else {
    const list = listFor(tun.viewKey);
    const secs = new Map();
    for (const c of list) {
      const t = sectionIn(c, tun.viewKey);
      if (!secs.has(t)) secs.set(t, []);
      secs.get(t).push(c);
    }
    const order = [...secs.keys()].map((t, i) => [t, i])
      .sort((x, y) => (sectionOrder(x[0]) - sectionOrder(y[0])) || (x[1] - y[1])).map((x) => x[0]);
    for (const t of order) frag.appendChild(buildSection(t, secs.get(t), tun.viewKey, ""));
    if (!list.length) frag.appendChild(h("div", "empty-view", "No live constants in this mode yet."));
  }
  el.sections.appendChild(frag);
  updateAll();
}

// --- knob strip: the mode's knobs as cards (registry LAYOUT knobs) ---
let stripCards = [];
function renderStrip() {
  el.strip.textContent = "";
  stripCards = [];
  if (tun.search.trim()) return;
  const mo = modeOf(tun.viewKey);
  if (!mo || !mo.mode || !mo.mode.knobs) return;
  for (const k of mo.mode.knobs) {
    const card = h("div", "kcard" + (k.target ? "" : " inert"));
    card.dataset.target = k.target || "";
    const top = h("div", "k-top");
    top.append(h("span", "k-id", "K" + k.k));
    const kind = h("span", "k-kind");
    kind.appendChild(icon(k.kind));
    kind.title = { uni: "Unipolar", bipolar: "Bipolar (noon = centre)", steps: "Stepped" }[k.kind] || "";
    top.appendChild(kind);
    const val = h("div", "k-val");
    card.append(top, h("div", "k-job", k.job), val);
    card.title = k.target ? "K" + k.k + " " + k.job + " - acts on: " + targetName(k.target) : "K" + k.k + ": no job in this mode";
    el.strip.appendChild(card);
    stripCards.push({ k, val });
  }
  paintStrip();
}
function paintStrip() {
  if (!stripCards.length) return;
  const here = activeModeId() === tun.viewKey && state.st;
  for (const { k, val } of stripCards) {
    let t, stale = false;
    if (!k.target) t = "";
    else if (here) t = knobShow(k.show, state.st, k.k);
    else { t = state.st ? "pedal is elsewhere" : "no state line yet"; stale = true; }
    if (val.textContent !== t) val.textContent = t;
    val.classList.toggle("stale", stale);
    val.title = here ? "At " + fmtTime(state.st.t) : "";
  }
}

// --- one section card --------------------------------------------------------
function splitTitle(t) {
  const i = t.lastIndexOf(" · ");
  if (i > 0 && /^K\d/.test(t.slice(i + 3))) return { title: t.slice(0, i), k: t.slice(i + 3) };
  return { title: t, k: "" };
}
function buildSection(titleText, list, viewKey, where) {
  const st = splitTitle(titleText);
  const tgt = (list.find((c) => c.section_target) || {}).section_target || "";
  const sec = h("section", "sec");
  sec.dataset.target = tgt;
  const head = h("div", "sec-head");
  if (st.k) head.appendChild(h("span", "sec-k", st.k));
  const title = h("div", "sec-title", st.title);
  if (where) title.appendChild(h("span", "sec-where", where));
  const chg = h("span", "sec-chg");
  head.append(title, chg);
  const body = h("div", "sec-body");
  const msg = h("div", "sec-msg");
  sec.append(head, body, msg);
  const rec = { msg, chg, list };
  for (const c of list) tun.cardOf.set(c.name, rec);

  const presets = list.filter((c) => c.preset);
  const rest = list.filter((c) => !c.preset);
  let count = 0;
  if (presets.length) {
    sec.classList.add("full");
    body.appendChild(buildPresetEditor(presets, viewKey));
  }
  // pairs -> fader banks (knob-end pairs of one unit / taper share a bank, side by side; each fader its own range)
  const inList = new Set(rest.map((c) => c.name));
  const done = new Set();
  const banks = new Map();
  const singles = [];
  for (const c of rest) {
    if (done.has(c.name)) continue;
    const P = pairOf(c);
    const mem = P ? P.members.filter((n) => inList.has(n)).map((n) => tun.byName.get(n)) : [];
    if (P && mem.length >= 2) {
      mem.forEach((m) => done.add(m.name));
      const bk = P.kind === "knob" ? "knob|" + mem[0].unit + "|" + mem[0].taper : "range|" + c.pair.id;
      if (!banks.has(bk)) banks.set(bk, []);
      banks.get(bk).push({ P, mem });
    } else {
      done.add(c.name);
      singles.push(c);
    }
  }
  for (const group of banks.values()) {
    body.appendChild(buildPairBank(group, viewKey));
    count += group.reduce((n, g) => n + g.mem.length, 0);
  }
  const faders = singles.some((c) => c.view === "faders");
  const nums = singles.filter((c) => c.type !== "bool");
  const bools = singles.filter((c) => c.type === "bool");
  if (bools.length || (!faders && nums.length)) {
    const grid = h("div", "ctl-grid");
    for (const c of (faders ? [] : nums)) grid.appendChild(buildDialCell(c, viewKey));
    for (const c of bools) grid.appendChild(buildToggleCell(c, viewKey));
    body.appendChild(grid);
    count += (faders ? 0 : nums.length) + bools.length;
  }
  if (faders && nums.length) {
    const axis = nums.map((c) => c.unit).join("") ? "each its own range" : "";
    body.appendChild(buildBank(titleText.split(" · ")[0] + " stages",
      nums.map((c) => ({ c, cap: c.label.split(":")[0], wide: true })), axis, viewKey));
    count += nums.length;
  }
  if (count > 8 && !presets.length) sec.classList.add("wide");
  return sec;
}
function updateSectionCounts() {
  for (const rec of new Set(tun.cardOf.values())) {
    const n = rec.list.filter(isChanged).length;
    rec.chg.textContent = n ? n + " changed" : "";
  }
}
function showMsg(name, text) {
  const rec = tun.cardOf.get(name);
  if (!rec) return;
  const c = tun.byName.get(name);
  rec.msg.textContent = text ? (c ? c.label + ": " : "") + text : "";
}

// --- explanation (info popover, preset detail) ---
function explainInto(box, c, viewKey, heading) {
  const ex = h("div", "explain");
  if (heading) ex.appendChild(h("div", "ex-h", heading));
  if (c.note) ex.appendChild(h("p", "ex-note", c.note));
  const docs = (tun.snap && tun.snap.docs) || {};
  if (c.doc_of && docs[c.doc_of]) {
    ex.appendChild(h("p", "ex-meta", c.doc_of === c.name ? "Notes above it:" : "Notes above " + c.doc_of + " (shared by the constants below it):"));
    ex.appendChild(h("div", "ex-doc", docs[c.doc_of]));
  }
  const meta = h("p", "ex-meta");
  const code = h("code", "", c.name);
  meta.append(code, document.createTextNode(
    "  ·  default " + fmtWithUnit(c.default, c.unit, c.type) +
    (c.range ? "  ·  " + c.taper + " taper" : "") +
    "  ·  " + (LEVEL_TIP[c.level] || c.level) + (c.when ? " " + c.when : "") +
    (c.target ? "  ·  acts on: " + targetName(c.target) : "")));
  ex.appendChild(meta);
  if (c.range) ex.appendChild(h("p", "ex-meta", "Range: " + fmtWithUnit(c.range[0], c.unit, c.type) + " .. "
    + fmtWithUnit(c.range[1], c.unit, c.type) + (c.why ? " - " + c.why : "")));
  if (c.below) {
    const o = tun.byName.get(c.below);
    ex.appendChild(h("p", "ex-meta", "Must stay below " + (o ? o.label : c.below) + " (the code divides by their difference / ratio)."));
  }
  const sh = sharedWith(c, viewKey);
  if (sh.length) ex.appendChild(h("p", "ex-meta", "Also acts on " + sh.join(", ") + " - one value, changed everywhere."));
  ex.appendChild(h("p", "ex-meta", c.file + ":" + c.line + (c.in_firmware === false ? "  ·  not live in this firmware" : "")));
  box.appendChild(ex);
}
let popAnchor = null;
function openPop(anchor, fill) {
  if (popAnchor === anchor && !el.pop.hidden) { closePop(); return; }
  el.pop.textContent = "";
  fill(el.pop);
  el.pop.hidden = false;
  popAnchor = anchor;
  const r = anchor.getBoundingClientRect();
  const pw = el.pop.offsetWidth, ph = el.pop.offsetHeight;
  let x = Math.min(window.innerWidth - pw - 12, Math.max(12, r.left - pw / 2));
  let y = r.bottom + 6;
  if (y + ph > window.innerHeight - 12) y = Math.max(12, r.top - ph - 6);
  el.pop.style.left = x + "px";
  el.pop.style.top = y + "px";
}
function closePop() { el.pop.hidden = true; popAnchor = null; }
function infoButton(fill) {
  const b = h("button", "info");
  b.type = "button";
  b.setAttribute("aria-label", "Explanation");
  b.title = "Explanation";
  b.appendChild(glyph("info"));
  b.addEventListener("click", (e) => { e.stopPropagation(); openPop(b, fill); });
  return b;
}
function resetButton(c) {
  const b = h("button", "rst");
  b.type = "button";
  b.title = "Back to the default (" + fmtWithUnit(c.default, c.unit, c.type) + ")";
  b.setAttribute("aria-label", "Reset " + c.label);
  b.appendChild(glyph("reset"));
  b.addEventListener("click", (e) => { e.stopPropagation(); sendReset(c.name); });
  return b;
}
document.addEventListener("pointerdown", (e) => {
  if (!el.pop.hidden && !el.pop.contains(e.target) && !(popAnchor && popAnchor.contains(e.target))) closePop();
  if (!el.changesPop.hidden && !el.changesPop.contains(e.target) && !el.changesBtn.contains(e.target)) setChangesOpen(false);
});
document.addEventListener("keydown", (e) => {
  if (e.key !== "Escape") return;
  if (!el.pop.hidden) { closePop(); if (popAnchor) popAnchor.focus(); }
  if (!el.changesPop.hidden) { setChangesOpen(false); el.changesBtn.focus(); }
});
el.view.addEventListener("scroll", () => { if (!el.pop.hidden) closePop(); }, { passive: true });

// ---------------------------------------------------------------------------
// Value controls: rotary knob (dial) and vertical fader share one input model (attachValueInput):
//   drag vertically (Shift = fine), mouse wheel over it (Shift = fine), arrows / PageUp / PageDown /
//   Home / End when focused, double-click = back to the default. Values go through core.js fromFrac (taper,
//   snap to the shown precision, clamp) and sendSet (one request in flight per constant, latest wins).
// ---------------------------------------------------------------------------
// Controls built while `regTag` is set carry that tag, so a part of the view (preset detail row, compare
// bank) can be rebuilt without touching the others (dropTag).
let regTag = "";
function register(name, ctl) {
  if (!tun.ctls.has(name)) tun.ctls.set(name, []);
  ctl.tag = regTag;
  tun.ctls.get(name).push(ctl);
}
function dropTag(tag) {
  for (const [n, list] of tun.ctls) tun.ctls.set(n, list.filter((x) => x.tag !== tag));
}
function tagged(tag, fn) {
  const prev = regTag;
  regTag = tag;
  try { return fn(); } finally { regTag = prev; }
}
function updateName(name) {
  for (const ctl of tun.ctls.get(name) || []) ctl.update();
  updateSectionCounts();
}
function updateAll() {
  for (const list of tun.ctls.values()) for (const ctl of list) ctl.update();
  updateSectionCounts();
  renderNavCounts();
}

// value from a control position (0..1 over the constant's own range and taper), kept in its range
function valueAt(c, f) {
  let v = fromFrac(c, f);
  if (c.type === "int") v = Math.round(v);
  return Math.min(c.range[1], Math.max(c.range[0], v));
}
// The page zoom (top bar A- / A+ = CSS zoom on <html>).
function pageZoom() {
  const z = parseFloat(getComputedStyle(document.documentElement).zoom);
  return z > 0 && isFinite(z) ? z : 1;
}
// Drag geometry. Pointer events report clientY in viewport pixels; with Chrome's standardized CSS zoom (128+)
// getBoundingClientRect() is in the same viewport pixels (the zoom applied), so a fader's rect height at drag start
// is exactly the on-screen distance its cap travels: frac = pixels moved / that height -> the cap stays under the
// pointer at any page zoom. A dial has no track: its full travel is DIAL_TRAVEL_PX at zoom 1, x the page zoom (the
// same on-screen feel as the drawn size). Shift = DRAG_FINE x finer, for both.
const DIAL_TRAVEL_PX = 200, DRAG_FINE = 10;
function attachValueInput(target, c, opts) {
  // opts: {paint(v), frac(e) -> absolute position for a click on the track (faders) or null,
  //        travel() -> on-screen px for the full range (read at drag start)}
  let drag = null;
  const set = (v) => {
    if (v === drag.last) return;
    drag.last = v;
    opts.paint(v);
    sendSet(c.name, v);
  };
  const step = (dir, fine, big) => {
    if (!canEdit(c)) return;
    const v0 = cur(c);
    let v;
    if (c.type === "int") v = Math.min(c.range[1], Math.max(c.range[0], v0 + dir * (big ? Math.max(1, Math.round((c.range[1] - c.range[0]) / 10)) : 1)));
    else v = valueAt(c, toFrac(c, v0) + dir * (big ? 0.1 : fine ? 0.002 : 0.01));
    if (v === v0) return;
    opts.paint(v);
    sendSet(c.name, v);
  };
  target.addEventListener("pointerdown", (e) => {
    if (e.button !== 0 || !canEdit(c)) return;
    e.preventDefault();
    target.focus({ preventScroll: true });
    target.setPointerCapture(e.pointerId);
    tun.dragging = c.name;
    const abs = opts.frac ? opts.frac(e) : null;
    drag = { y0: e.clientY, f0: abs === null ? toFrac(c, cur(c)) : abs, fine: e.shiftKey, last: undefined,
             px: Math.max(1, opts.travel()) };
    if (abs !== null) set(valueAt(c, abs));
  });
  target.addEventListener("pointermove", (e) => {
    if (!drag || !target.hasPointerCapture(e.pointerId)) return;
    if (e.shiftKey !== drag.fine) {           // fine on / off mid-drag: continue from here
      drag.f0 = toFrac(c, drag.last === undefined ? cur(c) : drag.last);
      drag.y0 = e.clientY;
      drag.fine = e.shiftKey;
    }
    const sens = drag.px * (drag.fine ? DRAG_FINE : 1);
    set(valueAt(c, drag.f0 + (drag.y0 - e.clientY) / sens));
  });
  const end = (e) => {
    if (!drag) return;
    if (target.hasPointerCapture(e.pointerId)) target.releasePointerCapture(e.pointerId);
    drag = null;
    tun.dragging = null;
    updateName(c.name);
  };
  target.addEventListener("pointerup", end);
  target.addEventListener("pointercancel", end);
  target.addEventListener("wheel", (e) => {
    if (!canEdit(c)) return;
    const d = e.deltaY || e.deltaX;
    if (!d) return;
    e.preventDefault();
    step(d < 0 ? 1 : -1, e.shiftKey, false);
  }, { passive: false });
  target.addEventListener("keydown", (e) => {
    const k = e.key;
    if (k === "ArrowUp" || k === "ArrowRight") step(1, e.shiftKey, false);
    else if (k === "ArrowDown" || k === "ArrowLeft") step(-1, e.shiftKey, false);
    else if (k === "PageUp") step(1, false, true);
    else if (k === "PageDown") step(-1, false, true);
    else if ((k === "Home" || k === "End") && canEdit(c)) {
      const v = k === "Home" ? c.range[0] : c.range[1];
      opts.paint(v);
      sendSet(c.name, v);
    } else return;
    e.preventDefault();
  });
  target.addEventListener("dblclick", (e) => { e.preventDefault(); if (canReset()) sendReset(c.name); });
  target.setAttribute("role", "slider");
  target.tabIndex = 0;
  target.setAttribute("aria-valuemin", String(c.range[0]));
  target.setAttribute("aria-valuemax", String(c.range[1]));
}
function ariaValue(target, c, v) {
  target.setAttribute("aria-valuenow", String(v));
  target.setAttribute("aria-valuetext", fmtWithUnit(v, c.unit, c.type));
}

// number field: type + Enter (range / type checked as devtool does)
function commitNum(c, num) {
  const cc = tun.byName.get(c.name);
  const v = parseNum(num.value);
  let why = "";
  if (!isFinite(v)) why = "not a number: " + num.value;
  else if (cc.type === "int" && v !== Math.round(v)) why = "whole numbers only";
  else if (cc.range && (v < cc.range[0] || v > cc.range[1]))
    why = "outside " + fmtWithUnit(cc.range[0], cc.unit, cc.type) + " – " + fmtWithUnit(cc.range[1], cc.unit, cc.type);
  if (why) { showMsg(c.name, why); num.value = fmtNum(cur(cc), cc.unit, cc.type, true); return; }
  if (v !== cur(cc)) sendSet(c.name, v);
}
function makeNum(c, aria) {
  const num = h("input", "num");
  num.type = "text";
  num.inputMode = "decimal";
  num.spellcheck = false;
  num.setAttribute("aria-label", aria);
  num.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); commitNum(c, num); num.select(); }
    if (e.key === "Escape") { num.value = fmtNum(cur(c), c.unit, c.type, true); num.blur(); }
  });
  num.addEventListener("change", () => commitNum(c, num));
  num.addEventListener("blur", () => { num.value = fmtNum(cur(c), c.unit, c.type, true); });
  return num;
}
function valueRow(c, num) {
  const row = h("div", "val");
  row.append(num, h("span", "unit", unitText(c.unit)));
  return row;
}

// arc on the manual's sweep (viewBox 0 0 100 100, centre 50 50)
const DIAL_R = 34;
function pt(r, deg) { const a = deg * Math.PI / 180; return [50 + r * Math.sin(a), 50 - r * Math.cos(a)]; }
function arcD(a, b) {
  const lo = Math.min(a, b), hi = Math.max(a, b);
  if (hi - lo < 0.2) return "";
  const s = pt(DIAL_R, lo), e = pt(DIAL_R, hi);
  return "M " + s[0].toFixed(2) + " " + s[1].toFixed(2) + " A " + DIAL_R + " " + DIAL_R + " 0 " + (hi - lo > 180 ? 1 : 0) +
    " 1 " + e[0].toFixed(2) + " " + e[1].toFixed(2);
}
function makeDial(c, aria) {
  const svg = document.createElementNS(SVGNS, "svg");
  svg.setAttribute("viewBox", "0 0 100 100");
  svg.setAttribute("class", "dial");
  svg.setAttribute("aria-label", aria);
  svg.innerHTML = '<path class="trk" d="' + arcD(KNOB_MIN_DEG, KNOB_MAX_DEG) + '"/><path class="arc"/>' +
    '<circle class="cap" cx="50" cy="50" r="15"/><line class="ptr"/>';
  const arc = svg.querySelector(".arc"), ptr = svg.querySelector(".ptr");
  const o = fracToDeg(originFrac(c));
  const paint = (v) => {
    const d = fracToDeg(toFrac(c, v));
    arc.setAttribute("d", arcD(o, d));
    const p0 = pt(3, d), p1 = pt(13, d);
    ptr.setAttribute("x1", p0[0].toFixed(2)); ptr.setAttribute("y1", p0[1].toFixed(2));
    ptr.setAttribute("x2", p1[0].toFixed(2)); ptr.setAttribute("y2", p1[1].toFixed(2));
    ariaValue(svg, c, v);
  };
  return { svg, paint };
}

// one knob cell: label above, info + reset slots, dial, number field + unit, default when changed
function buildDialCell(c, viewKey) {
  const cell = h("div", "cell");
  cell.dataset.target = c.target || c.section_target || "";
  cell.dataset.name = c.name;
  const lab = h("div", "lab", c.label);
  lab.title = c.name;
  const d = makeDial(c, c.label);
  const num = makeNum(c, c.label + " value");
  const def = h("div", "def", "default " + fmtWithUnit(c.default, c.unit, c.type));
  cell.append(lab, resetButton(c), infoButton((box) => explainInto(box, c, viewKey, c.label)), d.svg, valueRow(c, num), def);
  const paint = (v) => { d.paint(v); if (document.activeElement !== num) num.value = fmtNum(v, c.unit, c.type, true); };
  if (c.range) attachValueInput(d.svg, c, { paint, travel: () => DIAL_TRAVEL_PX * pageZoom() });
  register(c.name, {
    update() {
      const ok = canEdit(c), ch = isChanged(c);
      if (tun.dragging !== c.name) paint(cur(c));
      cell.classList.toggle("changed", ch);
      cell.classList.toggle("off", c.in_firmware === false || (tun.byName.get(c.name) || c).in_firmware === false);
      cell.classList.toggle("pending", isPending(c.name));
      d.svg.classList.toggle("off", !ok);
      d.svg.setAttribute("aria-disabled", String(!ok));
      num.disabled = !ok;
      cell.querySelector(".rst").disabled = !canReset() || !ch;
    },
  });
  return cell;
}

// bool: a toggle switch (its @states words, else off / on)
function stateWord(c, v) { return c.states ? c.states[v ? 1 : 0] : (v ? "on" : "off"); }
function buildToggleCell(c, viewKey) {
  const cell = h("div", "cell");
  cell.dataset.target = c.target || c.section_target || "";
  cell.dataset.name = c.name;
  const lab = h("div", "lab", c.label);
  lab.title = c.name;
  const sw = h("label", "switch");
  const chk = h("input");
  chk.type = "checkbox";
  chk.setAttribute("role", "switch");
  chk.setAttribute("aria-label", c.label);
  const st = h("span", "st");
  sw.append(chk, st);
  chk.addEventListener("change", () => sendSet(c.name, chk.checked));
  const def = h("div", "def", "default " + stateWord(c, c.default));
  cell.append(lab, resetButton(c), infoButton((box) => explainInto(box, c, viewKey, c.label)), sw, def);
  register(c.name, {
    update() {
      const v = !!cur(c), ch = isChanged(c);
      chk.checked = v;
      st.textContent = stateWord(c, v);
      chk.disabled = !canEdit(c);
      cell.classList.toggle("changed", ch);
      cell.classList.toggle("off", (tun.byName.get(c.name) || c).in_firmware === false);
      cell.querySelector(".rst").disabled = !canReset() || !ch;
    },
  });
  return cell;
}

// vertical fader (in a bank): its own full range (and taper) over the whole track
function buildFader(c, capNode, viewKey, wide) {
  const fd = h("div", "fd" + (wide ? " wide" : ""));
  fd.dataset.target = c.target || c.section_target || "";
  fd.dataset.name = c.name;
  const fader = h("div", "fader");
  fader.setAttribute("aria-label", c.label);
  const trk = h("div", "f-trk"), fill = h("div", "f-fill"), cap = h("div", "f-cap");
  fader.append(trk, fill, cap);
  const o = originFrac(c);
  const num = makeNum(c, c.label + " value");
  const paint = (v) => {
    const f = toFrac(c, v);
    cap.style.top = ((1 - f) * 100) + "%";
    fill.style.top = ((1 - Math.max(f, o)) * 100) + "%";
    fill.style.height = (Math.abs(f - o) * 100) + "%";
    ariaValue(fader, c, v);
    if (document.activeElement !== num) num.value = fmtNum(v, c.unit, c.type, true);
  };
  attachValueInput(fader, c, {
    paint,
    travel: () => fader.getBoundingClientRect().height,
    frac: (e) => {
      if (e.target === cap) return null;     // grab the cap: relative; click the track: jump there
      const r = fader.getBoundingClientRect();
      return Math.min(1, Math.max(0, 1 - (e.clientY - r.top) / r.height));
    },
  });
  const capLab = h("div", "cap-lab");
  capLab.appendChild(capNode);
  const info = infoButton((box) => explainInto(box, c, viewKey, c.label));
  fd.append(resetButton(c), info, fader, valueRow(c, num), capLab);
  register(c.name, {
    update() {
      const ok = canEdit(c), ch = isChanged(c);
      if (tun.dragging !== c.name) paint(cur(c));
      fd.classList.toggle("changed", ch);
      fd.classList.toggle("off", (tun.byName.get(c.name) || c).in_firmware === false);
      fd.classList.toggle("pending", isPending(c.name));
      fader.classList.toggle("off", !ok);
      fader.setAttribute("aria-disabled", String(!ok));
      num.disabled = !ok;
      fd.querySelector(".rst").disabled = !canReset() || !ch;
    },
  });
  return fd;
}
// items: [{c, cap (text or node), wide}]; every fader on its own range
function buildBank(title, items, axisText, viewKey) {
  const bank = h("div", "bank");
  const tg = items.map((x) => x.c.target || x.c.section_target).filter(Boolean);
  bank.dataset.target = tg.length && tg.every((t) => t === tg[0]) ? tg[0] : "";
  const head = h("div", "bank-head");
  head.appendChild(h("span", "bank-title", title));
  if (axisText) head.appendChild(h("span", "bank-axis", axisText));
  const row = h("div", "bank-row");
  for (const it of items) {
    const cap = typeof it.cap === "string" ? document.createTextNode(it.cap) : it.cap;
    row.appendChild(buildFader(it.c, cap, viewKey, it.wide));
  }
  bank.append(head, row);
  return bank;
}
// pairs: one fader per end, captioned with the manual's direction icon (knob ends) or the end's name
function buildPairBank(group, viewKey) {
  const items = [];
  for (const g of group) {
    for (const c of g.mem) {
      const cap = h("span");
      if (DIR_ENDS[c.pair.end]) {          // knob end: the manual's direction icon + the constant's short label
        cap.appendChild(icon(DIR_ENDS[c.pair.end]));
        cap.title = "Knob at " + c.pair.end_label;
      }
      cap.appendChild(document.createTextNode(c.label));
      items.push({ c, cap });
    }
  }
  const title = group.map((g) => g.P.label).join("  ·  ");
  return buildBank(title, items, "", viewKey);
}
function shortPairLabel(l) { return l.split(" ")[0]; }

function fieldOf(c) { return c.name.slice(c.name.indexOf(c.preset + "_") + c.preset.length + 1); }

// ---------------------------------------------------------------------------
// Preset editor (registry @family): ONE preset at a time. A row of preset names on top; by default it
// follows the pedal's K1 preset ("Follow K1"); clicking a name pins that preset. Below: only that preset's
// values, grouped by target (timbre / filter / pitch knobs, LFO rates as a CCW / CW fader pair, source switch).
// ---------------------------------------------------------------------------
const PRESET_GROUPS = [
  { title: "Timbre", target: "timbre", fields: ["TIMBRE", "STEPPED"] },
  { title: "Filter", target: "filter", fields: ["FILTER", "FILTER_SWELL"] },
  { title: "Pitch", target: "pitch", fields: ["PITCH"] },
  { title: "LFO speed", target: "pitch", fields: ["RATE_CCW", "RATE_CW"], faders: true },
  { title: "Source", target: "env", fields: ["ENV"] },
  { title: "Lofi", target: "lofi", fields: ["LOFI"] },
];
let presetEd = null;     // {presets, byPreset, viewKey, chips, body, follow}
function presetShown() {
  const a = state.active;
  if (tun.presetFollow !== false && a && a.preset && activeModeId() === (presetEd && presetEd.viewKey)
      && presetEd.byPreset.has(a.preset)) return a.preset;
  if (tun.presetSel && presetEd && presetEd.byPreset.has(tun.presetSel)) return tun.presetSel;
  return presetEd ? presetEd.presets[0].name : "";
}
function buildPresetEditor(list, viewKey) {
  const wrap = h("div", "ped");
  const presets = [], byPreset = new Map();
  for (const c of list) {
    if (!byPreset.has(c.preset)) { byPreset.set(c.preset, new Map()); presets.push({ name: c.preset, index: c.preset_index }); }
    byPreset.get(c.preset).set(fieldOf(c), c);
  }
  presets.sort((a, b) => a.index - b.index);
  if (tun.presetFollow === undefined) tun.presetFollow = store("devtool.presetFollow") !== "0";
  if (tun.presetSel === undefined) tun.presetSel = store("devtool.presetSel") || "";
  const bar = h("div", "ped-bar");
  const follow = h("button", "ped-follow", "Follow K1");
  follow.type = "button";
  follow.addEventListener("click", () => { tun.presetFollow = true; store("devtool.presetFollow", "1"); renderPresetBody(true); });
  bar.appendChild(follow);
  const chips = new Map();
  for (const p of presets) {
    const b = h("button", "ped-chip");
    b.type = "button";
    b.append(h("span", "pk", String(p.index + 1)), document.createTextNode(p.name), h("span", "pnow"));
    b.addEventListener("click", () => {
      tun.presetFollow = false; tun.presetSel = p.name;
      store("devtool.presetFollow", "0"); store("devtool.presetSel", p.name);
      renderPresetBody(true);
    });
    chips.set(p.name, b);
    bar.appendChild(b);
  }
  const body = h("div", "ped-body");
  wrap.append(bar, body);
  presetEd = { presets, byPreset, viewKey, chips, body, follow, shown: "" };
  renderPresetBody(true);
  return wrap;
}
function renderPresetBody(force) {
  if (!presetEd) return;
  const name = presetShown();
  const a = state.active;
  for (const [n, b] of presetEd.chips) {
    b.setAttribute("aria-pressed", String(n === name));
    b.classList.toggle("now", !!a && a.preset === n && activeModeId() === presetEd.viewKey);
  }
  presetEd.follow.setAttribute("aria-pressed", String(tun.presetFollow !== false));
  if (!force && name === presetEd.shown) return;
  presetEd.shown = name;
  dropTag("preset");
  const body = presetEd.body, vk = presetEd.viewKey;
  body.textContent = "";
  body.appendChild(buildWtPanel(name));
  paintWt();
  const fm = presetEd.byPreset.get(name);
  if (!fm) return;
  tagged("preset", () => {
    for (const g of PRESET_GROUPS) {
      const mem = g.fields.map((f) => fm.get(f)).filter(Boolean);
      if (!mem.length) continue;
      const box = h("div", "ped-group");
      box.dataset.target = g.target;
      box.appendChild(h("div", "ped-gtitle", g.title));
      if (g.faders) {
        const items = mem.map((c) => {
          const cap = h("span");
          if (c.pair && DIR_ENDS[c.pair.end]) cap.appendChild(icon(DIR_ENDS[c.pair.end]));
          cap.appendChild(document.createTextNode(c.pair ? c.pair.end_label : c.label));
          return { c, cap };
        });
        box.appendChild(buildBank("", items, "", vk));
      } else {
        const row = h("div", "ctl-grid");
        for (const c of mem) row.appendChild(c.type === "bool" ? buildToggleCell(c, vk) : buildDialCell(c, vk));
        box.appendChild(row);
      }
      body.appendChild(box);
    }
  });
  for (const c of fm.values()) updateName(c.name);
}
function renderPresetNow() { renderPresetBody(false); }

// ---------------------------------------------------------------------------
// Wavetable panel (preset editor, above the knob groups): the shown preset's frames as the pedal builds them
// (GET /api/tables = tools/hostsim/tbdump: full-band version, engine amplitude, 128 points). Left a stacked
// waterfall (frame 0 in front, later frames back / up, fading), right one tile per frame (key stages marked).
// The frame K2 plays (core.js tablePlay from the `tab` state line) is highlighted live: paintWt only toggles
// classes / moves one polyline, the panel is built once per shown preset (no rebuild, no layout change).
// ---------------------------------------------------------------------------
const wt = { status: "idle", data: null, reason: "", byName: new Map(), panel: null };
const WF = { w: 360, h: 220, dx: 96, dy: 104, pad: 6 };     // waterfall viewBox; depth offset of the last frame
const TILE = { w: 84, h: 48, pad: 3 };
async function loadTables() {
  if (wt.status === "loading") return;
  wt.status = "loading";
  let res = null;
  try {
    const r = await fetch("/api/tables", { cache: "no-store" });
    res = r.ok ? await r.json() : { ok: false, reason: "HTTP " + r.status };
  } catch (e) { res = { ok: false, reason: "devtool not reachable" }; }
  wt.byName = new Map();
  if (res && res.ok && res.data && Array.isArray(res.data.presets)) {
    wt.status = "ok"; wt.data = res.data; wt.reason = "";
    for (const p of res.data.presets) wt.byName.set(p.name, p);
  } else {
    wt.status = "err"; wt.data = null; wt.reason = (res && res.reason) || "unknown error";
  }
  if (wt.panel && wt.panel.box.isConnected) fillWtPanel(wt.panel.box, wt.panel.name);
}
function buildWtPanel(name) {
  const box = h("div", "wt");
  box.dataset.target = "timbre";
  wt.panel = { box, name, frames: null };
  if (wt.status === "idle") loadTables();
  fillWtPanel(box, name);
  return box;
}
function wavePoints(w, x0, y0, xw, amp) {
  const n = w.length, out = new Array(n + 1);
  for (let i = 0; i <= n; i++) out[i] = (x0 + xw * i / n).toFixed(1) + "," + (y0 - amp * w[i % n]).toFixed(1);
  return out.join(" ");   // one full cycle, closed at the start value
}
function svgEl(tag, attrs) {
  const e = document.createElementNS(SVGNS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  return e;
}
function fillWtPanel(box, name) {
  box.textContent = "";
  const head = h("div", "wt-head");
  head.appendChild(h("span", "ped-gtitle", "Wavetable"));
  const info = h("span", "wt-info");
  const play = h("span", "wt-play");
  head.append(info, play);
  box.appendChild(head);
  const P = wt.byName.get(name);
  if (!P) {
    const msg = wt.status === "err" ? "tables unavailable: " + wt.reason
      : wt.status === "ok" ? "no table data for " + name : "loading tables …";
    const m = h("div", "wt-msg", msg);
    if (wt.status === "err") {
      const b = h("button", "wt-retry", "retry");
      b.type = "button";
      b.addEventListener("click", () => { wt.status = "idle"; loadTables(); fillWtPanel(box, name); });
      m.appendChild(b);
    }
    box.appendChild(m);
    wt.panel = { box, name, frames: null };
    return;
  }
  const F = P.waves.length;
  let peak = 0;
  for (const w of P.waves) for (const v of w) peak = Math.max(peak, Math.abs(v));
  const g = peak > 0 ? 1 / peak : 1;
  info.textContent = P.kind + " · " + fmtNum(F, "", "int") + (F === 1 ? " frame" : " frames") + " · peak "
    + fmtNum(peak, "0..1") + " (drawn at full height)";
  info.title = "Full-band version (harmonics 1.." + wt.data.harmonics + "), engine amplitude = int16 / scale "
    + fmtNum(P.scale, "") + ", before the tonal gain; " + wt.data.size + " points. Table hash " + wt.data.hash + ".";
  // key stages -> frames (a stage between two frames marks both, as "between")
  const keyAt = new Map();   // frame -> "key" | "half"
  for (const k of P.key_stages) {
    const f0 = Math.floor(k + 1e-6), t = k - f0;
    if (t < 1e-3) keyAt.set(f0, "key");
    else { for (const f of [f0, f0 + 1]) if (!keyAt.has(f)) keyAt.set(f, "half"); }
  }
  const row = h("div", "wt-row");
  // waterfall: back (last frame) first, each frame a filled-under polyline that hides what is behind it
  const svg = svgEl("svg", { viewBox: "0 0 " + WF.w + " " + WF.h, class: "wt-wf", role: "img",
    "aria-label": name + " wavetable, " + F + " frames" });
  const xw = WF.w - WF.dx - 2 * WF.pad;
  const amp = Math.min(48, (WF.h - WF.dy - 2 * WF.pad) / 2) ;
  const base = WF.h - WF.pad - amp;                          // frame 0's zero line
  const at = (f) => (F > 1 ? f / (F - 1) : 0);
  const lines = new Array(F);
  for (let f = F - 1; f >= 0; f--) {
    const u = at(f);
    const x0 = WF.pad + WF.dx * u, y0 = base - WF.dy * u;
    const pts = wavePoints(P.waves[f].map((v) => v * g), x0, y0, xw, amp);
    const grp = svgEl("g", { class: "wf-f" + (keyAt.get(f) === "key" ? " key" : ""), style: "opacity:" + (1 - 0.7 * u).toFixed(3) });
    grp.appendChild(svgEl("polygon", { class: "wf-fill", points: pts + " " + (x0 + xw).toFixed(1) + "," + (y0 + amp + 2).toFixed(1)
      + " " + x0.toFixed(1) + "," + (y0 + amp + 2).toFixed(1) }));
    grp.appendChild(svgEl("polyline", { class: "wf-line", points: pts }));
    svg.appendChild(grp);
    lines[f] = grp;
  }
  const playLine = svgEl("polyline", { class: "wf-play", points: "" });
  svg.appendChild(playLine);
  row.appendChild(svg);
  // tiles: one per frame, frame number under it, key stage marker
  const grid = h("div", "wt-tiles");
  const tiles = new Array(F);
  for (let f = 0; f < F; f++) {
    const t = h("div", "wt-tile");
    const k = keyAt.get(f);
    if (k) t.classList.add(k === "key" ? "key" : "half");
    t.title = "frame " + f + (k === "key" ? " · key stage" : k === "half" ? " · next to a key stage between two frames" : "");
    const ts = svgEl("svg", { viewBox: "0 0 " + TILE.w + " " + TILE.h, width: TILE.w, height: TILE.h, "aria-hidden": "true" });
    ts.appendChild(svgEl("line", { class: "wt-zero", x1: 0, x2: TILE.w, y1: TILE.h / 2, y2: TILE.h / 2 }));
    ts.appendChild(svgEl("polyline", { class: "wt-wave",
      points: wavePoints(P.waves[f].map((v) => v * g), TILE.pad, TILE.h / 2, TILE.w - 2 * TILE.pad, TILE.h / 2 - TILE.pad) }));
    t.appendChild(ts);
    const cap = h("div", "wt-cap");
    cap.appendChild(h("span", "wt-n", fmtNum(f, "", "int")));
    if (k) cap.appendChild(h("span", "wt-key", k === "key" ? "◆" : "◇"));
    t.appendChild(cap);
    grid.appendChild(t);
    tiles[f] = t;
  }
  row.appendChild(grid);
  box.appendChild(row);
  wt.panel = { box, name, frames: F, P, g, lines, tiles, playLine, play, xw, amp, base, last: null };
  paintWt(true);
}
// Live highlight: the frame K2 plays on the shown preset (only while the pedal is on it).
function paintWt(force) {
  const W = wt.panel;
  if (!W || !W.frames || !W.box.isConnected) return;
  const p = state.st && state.st.bench === "2" && state.st.preset === W.name
    ? tablePlay(state.st, W.P.kind, W.frames, W.P.key_stages) : null;
  const key = p ? p.pos.toFixed(3) + "|" + p.stage : "";
  if (!force && key === W.last) return;
  W.last = key;
  for (const t of W.tiles) t.classList.remove("play", "play2");
  for (const l of W.lines) l.classList.remove("play");
  W.box.classList.toggle("playing", !!p);
  if (!p) {
    W.play.textContent = state.st && state.st.preset === W.name ? "" : "not playing";
    W.playLine.setAttribute("points", "");
    return;
  }
  const F = W.frames, f0 = Math.min(F - 1, Math.floor(p.pos)), t = p.pos - f0;
  const near = t < 0.5 ? f0 : Math.min(F - 1, f0 + 1), other = t < 0.5 ? f0 + 1 : f0;
  W.tiles[near].classList.add("play");
  W.lines[near].classList.add("play");
  if (t > 1e-3 && other < F && other !== near) W.tiles[other].classList.add("play2");
  // the read wave: linear blend of the two frames (as the engine's morph), drawn at its depth
  const a = W.P.waves[f0], b = W.P.waves[Math.min(F - 1, f0 + 1)];
  const u = F > 1 ? p.pos / (F - 1) : 0;
  const w = a.map((v, i) => (v + t * (b[i] - v)) * W.g);
  W.playLine.setAttribute("points", wavePoints(w, WF.pad + WF.dx * u, W.base - WF.dy * u, W.xw, W.amp));
  W.play.textContent = (p.stage ? "K2 step " + p.stage + " · " : "") + "plays frame " + fmtNum(p.pos, "knob");
}

// ---------------------------------------------------------------------------
// Sending: one request per constant in flight, latest value wins (devtool throttles the serial side)
// ---------------------------------------------------------------------------
async function post(path, body) {
  try {
    const r = await fetch(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
    const js = await r.json().catch(() => ({}));
    return { ok: r.ok, status: r.status, message: js.message || String(r.status) };
  } catch (e) {
    return { ok: false, status: 0, message: "devtool unreachable" };
  }
}
function sendSet(name, value) {
  tun.local.set(name, value);
  if (tun.inflight.has(name)) { tun.queued.set(name, value); updateName(name); return; }
  tun.inflight.set(name, true);
  updateName(name);
  post("/api/set", { name, value }).then((res) => {
    tun.inflight.delete(name);
    showMsg(name, res.ok ? "" : res.message);
    if (!res.ok && !tun.queued.has(name)) tun.local.delete(name);
    if (tun.queued.has(name)) {
      const v = tun.queued.get(name);
      tun.queued.delete(name);
      sendSet(name, v);
    } else updateName(name);
  });
}
function sendReset(name) {
  tun.queued.delete(name);
  tun.local.delete(name);
  post("/api/reset", { name }).then((res) => showMsg(name, res.ok ? "" : res.message));
}

// ---------------------------------------------------------------------------
// Changes popover: default -> new, "what I heard", reset all
// ---------------------------------------------------------------------------
function renderChanges() {
  const s = tun.snap;
  const list = s ? s.constants.map((c) => tun.byName.get(c.name)).filter(isChanged) : [];
  el.changesCount.textContent = String(list.length);
  el.changesWord.textContent = list.length === 1 ? "change" : "changes";
  el.changesBtn.classList.toggle("on", list.length > 0);
  el.changesSub.textContent = s ? "- " + s.pedal + (s.firmware ? " " + s.firmware : "") : "";
  el.changesList.textContent = "";
  if (!list.length)
    el.changesList.appendChild(h("li", "empty", s && s.state === "synced" ? "Everything at the compiled defaults." : "No pedal values yet."));
  for (const c of list) {
    const li = h("li");
    li.dataset.target = c.target || c.section_target || "";
    const lab = h("span", "c-label", (c.preset ? c.preset + " - " : "") + c.label);
    lab.title = "Show " + c.name;
    lab.addEventListener("click", () => { setChangesOpen(false); reveal(c); });
    const val = h("span", "c-val");
    val.append(document.createTextNode(fmtNum(c.default, c.unit, c.type) + " → "));
    val.appendChild(h("b", "", c.type === "bool" ? stateWord(c, c.value) : fmtNum(c.value, c.unit, c.type)));
    if (unitText(c.unit)) val.appendChild(document.createTextNode(" " + c.unit));
    const vk = viewKeysOf(c)[0];
    const mo = modeOf(vk);
    li.append(h("span", "swatch"), lab, val,
      h("span", "c-where", (mo && mo.mode ? mo.bench.name + " / " + mo.mode.name : c.module) + "  ·  " + sectionIn(c, vk) + "  ·  " + c.name));
    el.changesList.appendChild(li);
  }
  el.resetAll.disabled = !list.length || !canReset();
}
function reveal(c) {
  const keys = viewKeysOf(c);
  const k = keys.includes(tun.viewKey) && !tun.search ? tun.viewKey : keys[0];
  if (k !== tun.viewKey || tun.search) selectView(k);
  if (c.preset && presetEd) { tun.presetFollow = false; tun.presetSel = c.preset; renderPresetBody(true); }
  let node = el.sections.querySelector('.pcell[data-name="' + c.name + '"]') ||
    el.sections.querySelector('[data-name="' + c.name + '"]');
  if (node && node.classList.contains("pcell") && tun.openCell !== c.name) {
    node.click();
    node = el.sections.querySelector('.pdetail [data-name="' + c.name + '"]') || node;
  }
  if (node) { node.scrollIntoView({ block: "center" }); const f = node.querySelector(".dial, .fader, input"); if (f) f.focus({ preventScroll: true }); }
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
  el.changesPop.hidden = !o;
  el.changesBtn.setAttribute("aria-expanded", String(o));
}
el.changesBtn.addEventListener("click", () => setChangesOpen(el.changesPop.hidden));

// --- nav overlay (narrow windows) + search ---
function setNavOpen(o) {
  el.nav.classList.toggle("open", o);
  el.navToggle.setAttribute("aria-expanded", String(o));
}
el.navToggle.addEventListener("click", () => setNavOpen(!el.nav.classList.contains("open")));
let searchTimer = 0;
el.search.addEventListener("input", () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(() => {
    tun.search = el.search.value;
    store("devtool.search", tun.search);
    if (!tun.snap) return;
    renderNavActive();
    renderView(true);
  }, 150);
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
    // a (re)connect replays the server's backlog: start the log over from it
    state.online = true;
    for (const l of state.lines) l.el = null;
    state.lines = [];
    state.pausedAt = 0;
    el.flashOut.textContent = "";
    flashLast = "";
    loadMeta();
    if (wt.status === "err") loadTables();
    fullRender();
    renderBanner();
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
    if (tun.snap) updateAll();
    es.close();
    es = null;
    retryTimer = setTimeout(connect, 1000);
  };
}

const savedFilter = store("devtool.filter");
if (savedFilter) el.filter.value = savedFilter;
setFilter(el.filter.value);
el.search.value = tun.search;
applyDrawer();
fillPedals();
renderStatus();
renderFlash();
fullRender();
renderChanges();
connect();

// UI zoom (top bar A- / A+): CSS zoom on the root, 0.7 .. 2, remembered (try/catch: storage may be blocked)
(function () {
  var z = 1.15;
  try { var v = parseFloat(localStorage.getItem("devtool.zoom")); if (v >= 0.7 && v <= 2) z = v; } catch (e) {}
  function apply() { document.documentElement.style.zoom = String(z); try { localStorage.setItem("devtool.zoom", String(z)); } catch (e) {} }
  function step(d) { z = Math.round(Math.min(2, Math.max(0.7, z + d)) * 100) / 100; apply(); }
  var o = document.getElementById("zoom-out"), i = document.getElementById("zoom-in");
  if (o) o.addEventListener("click", function () { step(-0.1); });
  if (i) i.addEventListener("click", function () { step(0.1); });
  apply();
})();
