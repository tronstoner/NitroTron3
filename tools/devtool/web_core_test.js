// node tools/devtool/web_core_test.js - unit checks of web/core.js (taper mapping, formatting, state lines).
"use strict";
const C = require("./web/core.js");
let fails = 0;
function check(ok, what) { console.log((ok ? "ok   " : "FAIL ") + what); if (!ok) fails++; }
const near = (a, b, e) => Math.abs(a - b) <= (e || 1e-9);

// taper
const lin = { range: [0, 6], taper: "lin", type: "float", unit: "oct" };
const log = { range: [5, 80], taper: "log", type: "float", unit: "Hz" };
const bip = { range: [-96, 0], taper: "lin", type: "float", unit: "dB" };
const sym = { range: [-1, 1], taper: "lin", type: "float", unit: "" };
const int = { range: [1, 16], taper: "lin", type: "int", unit: "" };
check(C.toFrac(lin, 0) === 0 && C.toFrac(lin, 6) === 1 && near(C.toFrac(lin, 3), 0.5), "lin: ends + middle");
check(near(C.toFrac(log, 20), 0.5), "log: geometric middle (5..80 -> 20) at 0.5");
check(C.fromFrac(log, 0.5) === 20 && C.fromFrac(log, 0) === 5 && C.fromFrac(log, 1) === 80, "log: fromFrac ends + middle");
check(C.toFrac(log, 1) === 0 && C.toFrac(log, 1000) === 1, "out-of-range values clamp");
for (const c of [lin, log]) {
  let ok = true;
  for (let i = 0; i <= 100; i++) { const v = C.fromFrac(c, i / 100); if (Math.abs(C.toFrac(c, v) - i / 100) > 0.002) ok = false; }
  check(ok, c.taper + ": fromFrac -> toFrac round trip within 0.2 %");
}
check(C.fromFrac(int, 0.51) === Math.round(1 + 15 * 0.51), "int: rounded");
check(C.fromFrac(lin, 0.123456) === Number(C.fmtNum(C.fromFrac(lin, 0.123456), "oct", "float", true)), "sent value = shown value (snapped)");
check(C.originFrac(lin) === 0 && C.originFrac(bip) === 0 && near(C.originFrac(sym), 0.5), "arc origin: range start, or the zero point when the range crosses 0");
check(C.fracToDeg(0) === -150 && C.fracToDeg(1) === 150 && C.fracToDeg(0.5) === 0, "sweep 7 -> 5 o'clock, noon at 0.5");

// formatting
check(C.fmtNum(0.45, "s") === "0.45" && C.fmtNum(5.834, "x") === "5.834", "fmtNum: unit precision + extra digits when needed");
check(C.fmtNum(22050, "Hz") === "22,050" && C.fmtNum(22050, "Hz", "float", true) === "22050", "fmtNum: grouping only for display");
check(C.fmtWithUnit(0.3, "0..1") === "0.300" && C.fmtWithUnit(1, "oct") === "1.00 oct", "fmtWithUnit: silent units");
check(C.fmtFreq(907) === "907 Hz" && C.fmtFreq(2689) === "2.69 kHz", "fmtFreq: Hz / kHz");
check(C.fmtNum(true, "bool", "bool") === "on" && C.fmtNum(null) === "–", "fmtNum: bool, missing");
check(C.parseNum("1.5") === 1.5 && isNaN(C.parseNum("abc")), "parseNum");
check(C.fmtTime(new Date(2026, 10, 6, 9, 5, 3, 7)) === "09:05:03.007", "fmtTime clock");

// state lines (real lines from log/)
const wt = "XP auto K1 0.00 K2 1.00 K3 0.31 K4 0.57 K5 0.74 SW1 MID oct played | tab 1/19 SAW ANALOG Q 3.00 lp 907 eff 92 gd 0.93 Td 8.00 gr 1.00 Tr 0.01 w 0.75 L 0.00 o 0.01 lfo stp d 1.00  1.81Hz mute 0 cpu 80%";
const s = C.parseState(wt);
check(s && s.bench === "2" && s.sw1 === "MID" && s.preset === "SAW" && s.knobs[3] === 0.57, "parseState: explorer tab line");
check(C.knobShow("{sel}", s, 1) === "SAW", "knobShow: selection");
check(C.knobShow("{hz:eff}", s, 3) === "92 Hz", "knobShow: frequency token");
check(C.knobShow("{env}", s, 4) === "swell 0.01 s", "knobShow: touch env (swell side)");
check(C.knobShow("{lfo}", s, 5) === "steps 1.81 Hz", "knobShow: LFO");
check(C.knobShow("{k}", s, 2) === "1.00", "knobShow: knob position");
check(C.knobShow("{num:zz}", s, 2) === "1.00", "knobShow: missing token -> knob position");
const fm = "XP auto K1 0.73 K2 0.34 K3 0.00 K4 0.42 K5 0.04 SW1 UP oct played | FM r 3.00 b 0.50 fc 150 gd 0.97 Td 8.00 gr 1.00 Tr 0.01 w 1.00 L 0.94 o 1.00 fce 150 lfo tri d 1.00  13.77Hz mute 0 cpu 70%";
const f = C.parseState(fm);
check(C.knobShow("{sel}", f, 1) === "r 3" && C.knobShow("b {num:b}", f, 2) === "b 0.5" && C.knobShow("{env}", f, 4) === "pluck 8 s",
  "knobShow: FM ratio, feedback, pluck side");
const lab = "XP auto K1 0.00 K2 0.80 K3 0.77 K4 0.22 K5 0.52 SW1 UP oct down | LAB 2OP-HARM r 3.00 fb fc 1644 b 0.80 u 0.05 gd 0.99 Td 1.60 gr 1.00 Tr 0.01 w 0.00 L 0.09 o 0.09 lfo stp d 0.12 > 0.12  0.24Hz mute 0 cpu 78%";
const l = C.parseState(lab);
check(l.bench === "3" && l.sel === "HARM" && C.knobShow("{word2:r} {hz:fc}", l, 3) === "fb 1.64 kHz" && C.knobShow("{lfo}", l, 5) === "steps 0.24 Hz",
  "parseState / knobShow: FM LAB 2-OP");
const spec = "XP auto K1 1.00 K2 0.00 K3 0.56 K4 0.18 K5 0.11 SW1 MID oct played | LAB SPEC bank 9/9 HARM nu 0.39 k2 0.00 lpf 250 sub  0.11 gd 0.00 Td 1.26";
const sp = C.parseState(spec);
check(sp.sel === "HARM" && C.knobShow("{num:sub}", sp, 5) === "0.11" && C.knobShow("{hz:lpf}", sp, 3) === "250 Hz", "FM LAB spectral");
const iv = "XP auto K1 0.76 K2 0.82 K3 0.24 K4 0.48 K5 0.46 SW1 DOWN oct up | INTERVAL A 8.00 B-25.95 ps - lfo - mute 0 cpu 30%";
check(C.parseState(iv).sel === "INTERVAL" && C.knobShow("{lfo}", C.parseState(iv), 5) === "off", "interval line, lfo off");
check(C.parseState("hello") === null, "other lines ignored");

console.log(fails ? fails + " FAILED" : "all passed");
process.exit(fails ? 1 : 0);
