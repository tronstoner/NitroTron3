// Pedal devtool page (step 1): connection + build info, flash, live log.
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
  es.onerror = () => {
    state.online = false;
    renderStatus();
    es.close();
    es = null;
    retryTimer = setTimeout(connect, 1000);
  };
}

const savedFilter = store("devtool.filter");
if (savedFilter) { el.filter.value = savedFilter; }
setFilter(el.filter.value);
fillPedals();
renderStatus();
renderFlash();
fullRender();
connect();
