#pragma once
//
// explorer — sound-exploration module on the armitage band engine.  SW3 MIDDLE (ONE-SHOT: K4 touch env (pluck <-> swell,
// docs/DESIGN_PHILOSOPHY.md §12) on the K3 fc (UP index / MID 2-pole lowpass), K5 LFO on K2 feedback / morph) and SW3 DOWN (FM LAB: the same one-shot
// engine with its own SW1 families - UP 2-OP FM, MID SPECTRAL, DOWN ADDITIVE, see FM LAB) on NitroLab: two instances of this class (constructor flag),
// sharing the read-only tables. (The former SW3 DOWN LOOPING variant is retired: EXP_LOOP_ENABLE, code kept.)
//
// The band engine is COPIED from armitage.h (not shared): the input is decimated 48 -> 8 kHz,
// split into 84 complex bands (armitage_bands.h, included read-only), each band reshaped by one
// of the models, summed, interpolated back to 48 kHz, DC-blocked, (SW1 MID: 2-pole lowpass,) soft-clipped, FS1-muted. LFO: K5, both instances.
// No pitch tracking: every band runs the selected model on its OWN phase phi_k and magnitude
// a_k = |z'_k|; nothing picks or decides per band. The model output is always full (no unshaped crossfade).
//
// SLOTS (each a full voice; SW2 selects ONE, as armitage):
//   played  z'_k = (-1)^k z_k (phase alignment, ARM_ALIGN), output weight w_k (fitted, armitage's wmid_)
//   up      z'_k = z_k^2 / |z_k|                 (POG Up1, f'_k = 2 f_k), slot gain EXP_GAIN_UP
//   down    z'_k = Down1(z_k) (half phase, sign continuation; f'_k = f_k / 2), slot gain EXP_GAIN_DOWN
//   u_k = z'_k / |z'_k| = (cos 2 pi phi_k, sin 2 pi phi_k), phi_k = atan2 / 2 pi in [0, 1) (polynomial)
//   slot = sum_k w_k a_k W_k                     (W per model, below; w_k = 1 in the octave slots)
//
// MIX:  wet8 = EXP_OUT_GAIN [ u G_UP up + p played + d G_DOWN down ], (u, p, d) one-hot from SW2
//       (UP / MID / DOWN), slid linearly over EXP_OCT_XFADE_MS on an SW2 change (old -> new slot
//       crossfade). A slot with weight exactly 0 is NOT computed; a slot that starts again after being
//       idle starts from cleared model state.
//   wet = interpolate(wet8) -> DC blocker -> K3 2-POLE LP (mix m; SW1 MID) -> soft clipper -> FS1 mute
//
// K3 2-POLE LOWPASS (SW1 MID = tables, both instances; 2026-10-28 late, ON TOP of the K3 dampener, replaces the
//   ladder): the parked K4 post filter's SVF (ExpSVF, TPT, ONE stage = 12 dB/oct, Q EXP_TF_Q 1.5, no
//   resonance), LOWPASS output only, at 48 kHz on the wet after the DC blocker, before the soft clipper (the
//   ladder's position). No drive, no trim (unity passband).
//   fc_f = 250 (16000 / 250)^k3 Hz (EXP_TF_FMIN / FMAX, plain exponential on the smoothed K3; noon 2 kHz, CW =
//   practically open), per 8 kHz sample (TfUpdate; + the K4 / K5 envelope, see K4 / K5 - the envelope moves ONLY
//   this cutoff on tables, not the dampener), coefficients set once per 8 kHz sample (only when fc changed): k3 is
//   10 ms-smoothed, the envelope values 1 ms-slid -> no zipper.
//   Mix: wet = x + m (lp(x) - x), m slid linearly 0 <-> 1 over EXP_LAD_XFADE_MS (10 ms) when the current model
//   enters / leaves the MID family; SVF state cleared when it starts from m = 0; m = 0: not run.
//
// K3 LADDER (PARKED 2026-10-28 late: EXP_LAD_ENABLE = false; true = this ladder instead of the 2-pole LP, same
//   position / mix / modulation laws, EXP_LAD_* range; the dampener stays on tables either way): NitroTron3 Mode A's Moog
//   ladder (MoogLadder, src/core/blocks/moog_ladder.h, as is): Huovilainen 4-pole 24 dB/oct lowpass, tanh (Pade)
//   on the input and per stage, single rate 48 kHz (no oversampling), on the 48 kHz wet after the DC blocker, before
//   the soft clipper. Resonance EXP_LAD_RES = 0 (Mode A's: no feedback, no peak), drive EXP_LAD_DRIVE = 1.8 (Mode
//   A's LADDER_DRIVE; small-signal passband gain 1.8 = +5.1 dB), no makeup gain.
//   fc_lad = 250 (8000 / 250)^k3 Hz (EXP_LAD_FMIN / FMAX, plain exponential on the smoothed K3; noon 1.4 kHz), per
//   8 kHz sample (LadUpdate; + the K4 / K5 envelope, see K4 / K5), set on the ladder once per 8 kHz sample (only when
//   it changed): k3 is 10 ms-smoothed, the envelope values 1 ms-slid -> no zipper.
//   Mix: wet = x + m (ladder(x) - x), m slid linearly 0 <-> 1 over EXP_LAD_XFADE_MS (10 ms) when the current
//   model enters / leaves the MID family (independent of the 1 ms model crossfade); the ladder state is cleared when
//   it starts from m = 0; m = 0: not run (SW1 UP / DOWN cost nothing).
//
// K3 DAMPENER (SW1 UP and MID; DOWN: none): fc = 150 (4000 / 150)^(k3^0.5) Hz (EXP_DAMP_FMIN / FMAX, exponent
//   EXP_DAMP_CURVE on k3 = smoothed K3 0..1; k3 = 1 -> exactly 4000, noon ~1.5 kHz). UP (FM caps): per 8 kHz sample
//   in DampFmUpdate, when the smoothed K3 or its envelope value moved; SW3 MID: scaled by the K4 touch env openness
//   o (both instances; retired LOOPING: + the K5-CW envelope octave offset), see K4 / K5. MID (tables; back 2026-10-28 late, as before
//   the ladder): from the smoothed K3 ONLY - NOT modulated (the envelope moves the 2-pole LP instead).
//   Every per-band harmonic ceiling on f'_k (after SW2) becomes
//   min(ceiling, fc); each band keeps its level and fundamental, loses only harmonics above fc:
//     FM caps    L = min(4000, fc) in the FM index / feedback caps and the XBAND index cap (down to 0 = plain
//                fundamental); EXP_FBFM_PUSHED = true or EXP_ANTIFOLD = false: no FM caps -> dampener inert on FM
//     TABLES     hf = min(4000, fc) / f'_k (fractional harmonic count). EXP_TB_BLEND = true: lo / hi = the two
//                neighbouring versions, LEVEL_H[lo] <= hf < LEVEL_H[hi = lo + 1], W = (1 - x) read(lo) + x read(hi),
//                x = (hf - H_lo) / (H_hi - H_lo) linear (hf < 1: version 0 only; hf >= 127: last only); x = 0 ->
//                one read. lo / x per band and slot, recomputed per block when fc moved, read by both sides of a
//                zone crossfade. Continuous in hf (x -> 1 at H_hi = x = 0 of the next pair), no slide.
//                NOTE: x > 0 admits harmonics of version hi above hf f'_k, i.e. above min(4000, fc) (at K3 CW
//                above the 4 kHz engine Nyquist: they fold, weight x); full CW is no longer the old open pick.
//                EXP_TB_BLEND = false: version = largest H <= hf (floor H = 1); a moved pick crossfades
//                old -> new over EXP_DAMP_SLIDE_MS (1 ms = one 48-frame block; one fade counter).
//     BITTAB     S with (S/2) f' <= L (floor S = 2); a moved S crossfades old -> new over EXP_DAMP_SLIDE_MS.
//     (TABLES / BITTAB: fc = the unmodulated knob value; re-picked per block when the smoothed K3 moved, only for
//     the running SW2 slot(s); a slot re-picks when it starts.)
//
// DISCRETE VALUES are stepped on the knob (snap to the item): items that change the waveform
// (table zone: EXP_TB_ZONE_XFADE_MS; FM ratio <-> XBAND, bit-table waveform: EXP_MODEL_XFADE_MS) crossfade
// old -> new at the switch moment; snapped numbers inside a running oscillator (FM ratio, interval) slide linearly
// to the new item over EXP_DISC_SLIDE_MS. Never blended between items.
//
// MODELS (L = min(EXP_FOLD_LIMIT_HZ, fc); "cap" = anti-fold, only with EXP_ANTIFOLD, floor 0)
//  SW1 UP — FM (one engine, K1 = 15 equal zones: the 14 EXP_FM_RATIOS in list order, then XBAND at full CW)
//   FM (ratio zones)  pm += r wrap(phi - phi_prev) (modulator at r x the band's own phase; integer r = r phi),
//                m = sin(2 pi pm + b (m1 + m2) / 2) (modulator self-feedback, DX7 two-sample average),
//                W = cos(2 pi phi + I m); r = the zone's ratio (slid 1 ms), b = EXP_FBFM_FB_MAX K2 (GENTLE 1.5),
//                I = EXP_FM_INDEX = 12 (fixed). GENTLE caps: I <= (L / f' - 1) / r - 1,
//                b <= (L / (r f') - 1) / EXP_FBFM_FB_SPAN (4); PUSHED (b = 3 K2): no caps.
//                (r = 1: the former FB FM; b = 0: the former ratio FM.)
//   XBAND        W = cos(2 pi phi_k + I sin(2 pi phi_{k+n})), n = EXP_XB_PARTNER = 12 (fixed); partner beyond
//                the top band: 0. I = 12, cap I <= (L - f'_k) / f'_{k+n} - 1. K2 inert.
//  SW1 MID — tables (K1 = zone, 10 equal stepped zones = WaveMorph's tables, COPIED; see TABLES below)
//   WAVETABLE    W = lerp_frames(lerp_phase(table[zone][f0][level_k]), table[zone][f0 + 1][level_k]) / scale_z:
//                morph m = K2 (smoothed), position (F_z - 1) m, f0 = frame below (<= F_z - 2), linear crossfade
//                between the two adjacent frames; linear interpolation in phase (256 points); scale_z =
//                EXP_TB_ZONE_I16_SCALE[zone]; level_k (or the lo / hi blend) = the static open pick (hf =
//                min(4000, fc) / f'_k, K3 dampener; + the 2-pole LP, see K3 2-POLE LOWPASS). A K1 zone change
//                crossfades old -> new zone over EXP_TB_ZONE_XFADE_MS (both read the same morph m and the same
//                per-band versions / blend); stateless.
//   BITTAB       (PARKED: EXP_BITTAB_ENABLE = false; true -> an 11th K1 zone on MID after the 10 table zones)
//                16-step pattern v[0..15], one step per 1/16 of the band cycle; per step (position f in
//                [0, 1), v0 this step, v1 the next): tri = v0 + f (v1 - v0), saw = tri if v1 > v0 else v0,
//                pulse = v0; K3 = shape (and the dampener + LP cutoff): 0 -> 0.5 crossfades tri -> saw, 0.5 -> 1 saw -> pulse.
//                K2 steps through 32 waveforms (equal zones, 1 ms crossfade): 1..16 = LFSR patterns (+-1,
//                16-bit Fibonacci LFSR, seed EXP_BT_SEED, tap set EXP_BT_TAPS[i], first 16 new bits),
//                17..32 = SQR v[j] = sq(j) cos(2 pi n j / 16), n = 1..16. Anti-fold: S_k steps (16 / 8 / 4 / 2,
//                (S/2) f' <= L), pattern subsampled every 16 / S_k-th step.
//  SW3 DOWN — FM LAB: OWN SW1 families (2026-10-28; families 2026-11-02, steps 1 - 3). SW3 MID's FM / tables /
//   harmonics & shift are NOT offered here (they stay on SW3 MID). UP runs the kFM 2OP oscillator above with its own
//   ratio slide (l2r_, linear over EXP_DISC_SLIDE_MS 1 ms) and its own control law; MID = its own model kLSPEC, DOWN =
//   kLADD (every SW1 change = the 1 ms model crossfade, the entering model from cleared state).
//   SW1 UP   = 2-OP FM (control model 2026-11-03; the former 12 sorted ratio zones EXP_LAB_ZONES are in git history,
//              0a96e41). Oscillator as kFM: pm += r wrap(phi - phi_prev) (fresh r phi), m = sin(2 pi pm + b (m1 + m2) / 2),
//              W = cos(2 pi phi + I m).
//              K1 = 3 equal stepped MODES (EXP_LAB_MODE_NAMES): HARMONIC | DISHARMONIC | FREE; a mode change = the ratio
//                slides over EXP_DISC_SLIDE_MS (1 ms), no crossfade (the model stays kFM, shared state).
//              K2 = RATIO r: HARMONIC 15 equal stepped zones (EXP_LAB_HARM_R) 0.125 0.2 0.25 0.333 0.5 0.667 0.75 [1]
//                1.333 1.5 2 2.5 3 4 5; DISHARMONIC 15 zones (EXP_LAB_DISH_R) 0.354 0.707 0.809 1.059 1.122 1.225 1.260
//                [1.414] 1.618 1.732 2.236 2.414 2.718 3.142 3.606 ([ ] = noon); stepped values slide 1 ms. FREE:
//                r = 0.125 64^k2 (EXP_LAB_FREE_RMIN / RMAX: 0.125 -> 8, noon 1) on the smoothed K2, continuous (the
//                1 ms slide follows the target).
//              K3 = BIPOLAR CHARACTER (LabChar): t3 = 2 k3 - 1 (smoothed, lands exactly), u = |t3|, u_eff = o u (K4 o);
//                PITCH-DEPENDENT index (2026-11-03, SW3 MID's approved law with k3 -> u_eff; LabFmUpdate, per 8 kHz
//                sample): index fc = EXP_DAMP_FMIN 150 (EXP_DAMP_FMAX 4000 / 150)^(DampCurve(u_eff)) (= DampFc(u_eff),
//                sqrt law), L = min(EXP_FOLD_LIMIT_HZ, fc); I = EXP_FM_INDEX 12 capped per band I <= (L / f' - 1) / r - 1
//                (floor 0) on both sides; noon (u_eff 0) = L 150 Hz -> index ~0 = pure sine. CCW (t3 < 0) "hollow":
//                b = 0. CW (t3 > 0): b = EXP_FBFM_FB_MAX 1.5 u_eff (linear from noon, with the index), capped per band
//                b <= (L / (r f') - 1) / EXP_FBFM_FB_SPAN 4 (floor 0), the same L.
//              K4 = TOUCH ENV ALONG K3 (2026-11-05, docs/DESIGN_PHILOSOPHY.md §12; TouchEnvUpdate / TouchOpenUpdate, see K4 /
//                K5): u_eff = o u - the openness o moves between the sine (o 0) and the K3 position (o 1), on whichever
//                side K3 is; full travel (no time-scaled depth).
//              K5 = LFO on b +- EXP_LFO_FB_SWING 0.3, added after the caps, [0, 1.5]; depth x the touch env fade-in
//                1 - w (1 - o_r) (1 at / CCW of noon, the swell's openness o_r past the morph zone; factor slid 1 ms). No XBAND.
//   SW1 MID  = SPECTRAL (2026-11-03, Odessa-style; model kLSPEC; replaces DUAL VOICE, in git history at 224f328).
//              Per band the ADDITIVE SAW machinery WITHOUT FM: v = u_k (the slot's unit phasor), partial n of
//              S_k = sum_n W(n) h_nk v^n (analytic, Re and Im; SpecSeries), n = 1..16 (EXP_SPEC_M); band out
//              W_k = Re(S_k e^{j 2 pi theta_k}) (K4 shift, below; theta 0 -> Re S_k). Anti-fold edge on the SHIFTED
//              partial frequency: h_nk = clamp((3800 - (n + s) f') / 400, 0, 1) (EXP_HARM_FMAX / FEDGE; n = 1 no edge).
//              Per-band state: theta_k per slot (explorer_spec_st, D2 SRAM).
//              Weights in the PARTIAL-NUMBER domain, the same for every band and slot, per 8 kHz sample and only when
//              an input moved (tilt when gamma moved, comb when nu / e moved, partials while p slides; SpecWeights):
//                W(n) = tilt(n) comb(n) p(n), then W /= sqrt(sum_n W(n)^2) (n = 1 counts; the per-band edge is NOT renormalised)
//                tilt(n) = n^-gamma;  comb(1) = 1 (fundamental always kept), n >= 2:
//                comb(n) = (0.5 + 0.5 cos(2 pi nu (n - 1)))^e (comb position phi fixed 0; warp removed 2026-11-04, w = 0)
//                (n = member index i of the K1 bank, all 16 on; bank-change gain g after the norm, below)
//              PHASE CONVENTION: y(n) = n - 1 counts from the fundamental, so nu = 0 = no notches and nu = 0.5
//                removes the EVEN partials (odd series).
//              CONTROLS (2026-11-05 consolidation; K2 / K3 / K4 smoothed, EXP_CTRL_SMOOTH_MS; K1 = raw knob zones;
//                K5 = SUB level, no LFO):
//              K1 = BANK SELECT (below; moved from K5).
//              K2 = DENSITY: nu = 0.5 clamp(k2 + travel, 0, 1) (EXP_SPEC_NU_MAX); travel = the K4 envelopes' share
//                (K4 below); travel 0 -> plain K2. Comb recomputed while nu moves at most every EXP_SPEC_NU_DIV 8
//                samples (1 kHz) and on the sample nu stops (lands exactly).
//              K4 = TOUCH ENV on the density (K2) and the K3 LPF (2026-11-05; built here as the reference implementation of
//                docs/DESIGN_PHILOSOPHY.md §12, shared since 2026-11-05 by every K4 mode - the envelopes g_d / g_r, T_d /
//                T_r, w, the live level L / L_d / G and o_d / o_r / o: see K4 / K5, ONE-SHOT). SPECTRAL's own part is
//                the density TRAVEL (SpecTravel, the only K4 mode with a time-scaled depth):
//                depth_d / depth_r from T_d / T_r, log over each side's own range: x = ln(T / FAST) / ln(SLOW / FAST)
//                clamped [0, 1], depth = DEPTH_MIN + (DEPTH_MAX - DEPTH_MIN) x (EXP_SPEC_ENV_DEPTH_* 0.08 .. 0.5), only
//                when that T changed.
//                  travel = (1 - w) depth_d (1 - o_d) + w depth_r (1 - o_r) (density, K2 above)
//                  o = the touch env openness (the K3 LPF, K3 below). Looping: travel 0, o = 1.
//                Per 8 kHz sample over the whole note (never held at the onset); no law switch, no slide on o / travel.
//                Exact noon = pure decay at 8 s (continuous with just CCW of noon).
//              K5 = SUB LEVEL (2026-11-05, EXP_SPEC_SUB_*): per active band an octave divider at f'_k / 2 - the input-
//                tracked v_k is halved by a half-angle phasor u (u^2 = v, sign continued, i.e. flipped on each 2 pi
//                wrap of v; own state explorer_spec_st.ur / ui per slot, so it follows every SW2 slot's f');
//                band out += a_k Re(u) g_sub, g_sub = SUB_MAX k5 (smoothed, linear; 1 = a member of weight 1), after
//                the RMS-normalised weights, before the K3 LPF; raw level. K5 full CCW: skipped (bit-identical).
//              K3 = LPF (2026-11-05; SpecLpfUpdate / SpecLpf, EXP_SPEC_LPF_*): the SW1 MID tables' 2-pole lowpass type
//                (ExpSVF, one TPT SVF stage, LP output, 12 dB/oct; own instance sp_svf_) on the 48 kHz wet, after the
//                DC blocker and the tables' TabFilter, before PostFilter / Saturate; no saturation. fc_knob = 250
//                (16000 / 250)^k3 Hz (CCW closed, CW open; no bypass), K4 as the tables but on the blended openness o
//                (K4 above): log2 fc = log2 250 + o (log2 fc_knob - log2 250) (EXP_SPEC_LPF_ENV_BOTTOM), Q EXP_SPEC_LPF_Q,
//                coefficients per 8 kHz sample when fc moved. Mix m (TabFilter's slide, 1 ms): target on while SPECTRAL is the entering model. The weights stay the shared W (no per-band factors).
//              FIXED (constants, former knob laws kept reachable): comb shape e = 0.25 16^k at k = EXP_SPEC_K_PEAK 1 ->
//                e = 4 (peaky; b^e = exp(e ln b), b = 0 -> 0); tilt gamma = 3 (1 - 2 k) (k <= 0.5), -(2 k - 1) (k > 0.5)
//                at k = EXP_SPEC_K_TILT 0.5 -> gamma 0 (flat); partial shift delta_k = s f'_k, s = EXP_SPEC_SHIFT_MAX
//                (2 k - 1) at k = EXP_SPEC_K_SHIFT 0.5 -> s = 0 (harmonic): theta_k += s f'_k / 8000 per sample,
//                wrapped [0, 1), held while s = 0 (theta 0 = Re S_k exactly, the fast path); cleared on model entry.
//              BANK SELECT (2026-11-04; on K1 since 2026-11-05): 9 equal stepped zones on the raw K1,
//                b = Quantize(k1, 9), CCW -> CW: FIB STRIDE7 STRIDE6 STRIDE5 STRIDE4 STRIDE3 PRIMES ODD HARM
//                (EXP_SPEC_BANK; HARM = full CW). Member i = 1..16 sits at m_i f'_k (all 16 on); tilt / comb / norm run
//                over the member INDEX i (n above = i), so comb / tilt act the same per bank; the anti-fold edge uses the
//                member's real shifted frequency (m_i + s) f'_k, members past the edge are skipped (not computed).
//                Bank change: a gain g on the normalised weights slides 1 -> 0 over 0.5 ms, the bank swaps at 0, g
//                slides 0 -> 1 over 0.5 ms (1 ms total, EXP_DISC_SLIDE_MS; no crossfade). HARM uses the plain series
//                (SpecSeries); other banks SpecBankSeries (step power(s) once per band, one complex multiply per member).
//   SW1 DOWN = ADDITIVE (step 3, 2026-11-02; model kLADD, item = K1 zone). One harmonic series per band built on the
//              slot's unit phasor v = u_k (as Armitage ADD: harmonics by repeated complex multiplication), FM on the
//              series fundamental at ratio 1, no feedback: q = b e^{j I x} (b = v, x = Im v = sin 2 pi phi; SUB: b = w,
//              x = Im w, see SUB), harmonic m = Re(q^m) (FM index m I on harmonic m).
//              h = "harmonics" macro in [0, 1]: M_eff = 1 + 15 h (EXP_ADD_M_MAX 16), g_m = clamp(M_eff - m + 1, 0, 1),
//              s = 1 - 0.4 h (EXP_ADD_S_MIN 0.6), weight w_m = g_m m^-s / N, N = sqrt(sum_members (g_m m^-s)^2) (RMS of
//              the series = one unit sine at every h; the per-band anti-fold below is NOT renormalised; recomputed when
//              h / the series moved). The fundamental m = 1 is always kept (w_1 = 1 / N); m >= 2 additionally x
//              h_mk = clamp((Fe - m f0) / EXP_HARM_FEDGE 400, 0, 1), Fe = EXP_HARM_FMAX 3800 (harmonics on K2 / fixed) or
//              min(3800, L) (harmonics on K3), f0 = the series fundamental (f'_k; SUB f'_k / 2).
//              FM index I = EXP_ADD_FM_I_MAX 6 k^2 (k = the index knob value), per band capped with the 2OP cap at r 1:
//              I <= L_c / f0 - 2 (floor 0; IndexCap c 1 r 1): L_c = L (K3 = index: the K4 touch env closes it) or the
//              K3 knob's own L without envelope (model B). Band out W = sum_m w_m h_mk Re(q^m).
//              SERIES  SAW m = 1..16 | ODD m = 1, 3, .., 15 (step q^2) | OCT m in {1, 2, 4, 8, 16} (repeated squaring) |
//                      SUB components k / 2 x f' (k = 1..2 M, count g_k = clamp(2 M_eff - k + 1, 0, 1), weight k^-s;
//                      k odd = the sub partials): base w = the half-angle phasor of v (w^2 = v): w0 = (sqrt((1 + Re v) / 2),
//                      sign(Im v) sqrt((1 - Re v) / 2)), sign continuation per slot / band (w = -w0 when w0 . w[n-1] < 0;
//                      own state, the slot's own v - so on the -1 slot SUB = two octaves below the played note).
//              K1 = 12 equal stepped zones (EXP_ADD_ZONES), every change = the 1 ms model crossfade (items share the state):
//                1 SAW-A  2 SAW-B  3 ODD-A  4 ODD-B  5 OCT-A  6 OCT-B  7 SUB-A  8 SUB-B  9 UNI-SAW  10 UNI-ODD
//                11 UNI-SAW-H  12 STRETCH
//              model A: K2 = h (K5 LFO: h = clamp(k2 + 0.3 l, 0, 1)), K3 = index k (L = the index fc incl. K4).
//              model B: K2 = index k = clamp(k2 + 0.3 l, 0, 1) (K5 LFO, knob units), cap at the K3 knob's L (no
//                       envelope); K3 = h_eff = o k3 (the K4 touch env openness o moves h between 0 and the K3 value),
//                       Fe = min(3800, L) (L follows K3 and o as on UP).
//              UNISON (9 UNI-SAW, 10 UNI-ODD: h = 1, K3 = index as A; 11 UNI-SAW-H: K3 = h as B, no FM): copy 2 = the
//                       same series on q2 = q e^{j 2 pi theta} (every harmonic m shifted by m theta, proportional);
//                       W = (S(q) + u S(q2)) / sqrt(1 + u^2) (equal power for uncorrelated copies; at the noon offset
//                       the fundamental is exactly level-matched), u = K2 (u = 0: copy 2 not computed, theta runs on).
//                       K5 = BEAT SPEED (no LFO): t5 = 2 k5 - 1, rate = 0.1 (8 / 0.1)^|t5| Hz (EXP_ADD_BEAT_MIN / MAX),
//                       noon 0 (theta frozen); CCW: theta += rate f0 / EXP_ADD_BEAT_REF_HZ 100 / sr8 (proportional to
//                       the note), CW: theta += rate / sr8 (same Hz on every band). theta starts at EXP_ADD_UNI_PHASE
//                       0.25 cycle (entering a unison zone from another zone / model). State: explorer_add_st (D2 SRAM);
//                       in a crossfade between two unison zones only the entering one advances theta.
//              STRETCH (12): 8 harmonics (EXP_ADD_STR_M, w_m = m^-1 / N), harmonic m at rho_m f', rho_m = m^(1 + delta),
//                       own phase accumulator per harmonic (p_m += rho_m d, d = the wrapped phi step; fresh: rho_m phi),
//                       out cos(2 pi p_m + rho_m I x); h_mk on rho_m f0 (Fe 3800); delta = EXP_ADD_STR_MAX 0.05 x
//                       clamp(k2 + 0.3 l, 0, 1) (K5 LFO), K3 = index as A. Phases: explorer_add_st (D2 SRAM).
//              K4 = touch env (2026-11-05, docs/DESIGN_PHILOSOPHY.md §12; one openness o, full travel: index fc on A /
//              UNI 9, 10 / STRETCH; h and L on B / UNI 11); K5 = the LFO above (no fade-in) or (UNISON) beat speed.
//   (TWIN / DUAL / MODOUT / BELL and their lab operator state removed 2026-11-02: git history 598a4fd.)
//
//  SW1 DOWN — harmonics & shift (K1 = model, 3 equal zones; no dampener)
//   1 HARMONICS  H = (1 - fr) h_n0 cos(2 pi n0 phi) + fr h_n1 cos(2 pi n1 phi), n = 1 + 5 K2 = n0 + fr,
//                n1 = n0 + 1, h_n = clamp((3800 - n f') / 400, 0, 1) (armitage ADD drop);
//                W = (1 - K3) H + K3 cos(2 pi phi).
//   2 FREQ SHIFT W = Re(u_k e^{j 2 pi theta_k}), theta_k += s_k / 8000,
//                s_k = s ((1 - K3) + K3 f'_k / EXP_FS_REF_HZ), s = sign(t) ((801)^|t| - 1) Hz, t = 2 K2 - 1.
//   3 INTERVAL   proportional shift only: theta_k += f'_k (2^((st + c / 100) / 12) - 1) / 8000,
//                st = -12 .. +12 semitones (25 equal K2 zones, noon = 0, slid 1 ms), c = 50 (2 K3 - 1) cents.
//
// PHASE phi_k: computed once per band per slot and sample in Prep (only when a running model needs it:
// FM ratio zones, harmonics, wavetable, bit table): atan of u_k in the played / +1 / -1 slots. A model
// crossfade shares it.
//
// MODEL CHANGE (SW1, K1 zone, or a discrete item that changes the waveform): old and new are both computed
// for EXP_MODEL_XFADE_MS and crossfaded linearly; the entering model starts from cleared state. A change
// during a fade is picked up when that fade ends.
//
// K4 / K5 (constructor flag: Explorer(false) = SW3 MID, Explorer(true) = SW3 DOWN = FM LAB: both ONE-SHOT, same
//   K4 / K5 laws; FM LAB: UP 2-OP = K4 along the K3 character (u_eff = o u), DOWN = the UP index law (K4 on the
//   index fc), MID SPECTRAL: K4 on the density nu (time-scaled travel) and the K3 LPF;
//   EXP_LOOP_ENABLE = true: Explorer(true) = the retired LOOPING variant instead).
//   Onset detector per 48 kHz input sample (EXP_ONSET_*, armitage's OnsetDetect), both. t4 = 2 k4 - 1, t5 = 2 k5 - 1
//   (both smoothed, land exactly).
//  ONE-SHOT (SW3 MID and FM LAB) — K4 = TOUCH ENV (2026-11-05; docs/DESIGN_PHILOSOPHY.md §12; replaces the shared
//   pluck <-> swell g / g_s of PsUpdate, retired, not called). One implementation per instance (TouchEnvUpdate /
//   TouchOpenUpdate, in EnvUpdate per 8 kHz sample), ONE openness o for every K4 target of the running model,
//   FULL travel (target = bottom + o (K3 value - bottom), the law g_s had; no time-scaled depth except SPECTRAL's
//   density, see FM LAB SW1 MID):
//     SW3 MID SW1 UP (FM ratio zones / XBAND): index fc, log2 fc = log2 150 + o (log2 fc_knob - log2 150), clamped
//          [150, 4000] Hz (DampFmUpdate, o3_ = o).
//     SW3 MID SW1 MID (tables): 2-pole LP, ln fc = ln bot + o (ln fc_f_knob - ln bot), bot = EXP_TF_ENV_BOTTOM
//          2^(k3 EXP_TF_ENV_BOTTOM_OCT) (TfUpdate, o3_ = o; not the tables' dampener).
//     SW3 MID SW1 DOWN: K4 inert (o3_ = 1, o not computed while a DOWN model is current; the envelopes run on).
//     FM LAB UP 2-OP: u_eff = o |2 k3 - 1| (LabFmUpdate / LabChar: index fc and feedback b along K3).
//     FM LAB DOWN ADDITIVE: the index fc as SW3 MID UP (DampFmUpdate, o3_ = o; A / UNI 9, 10 / STRETCH), h_eff = o k3
//          and L on B / UNI-SAW-H.
//     FM LAB MID SPECTRAL: the K3 LPF (log2 fc = log2 BOT + o (log2 fc_knob - log2 BOT)) + the density travel.
//   ENVELOPES (TouchEnvUpdate, both always running on the instance, whatever SW1 model runs; both retriggered by
//   every onset; no side state, no LEAVE, no hysteresis: the same knob position = the same result):
//     PLUCK g_d: onset -> 1, then the EXP_PS_DEC_2POLE 2-pole toward 0 (else one-pole / EXP_PS_DEC_LINEAR linear) over
//       T_d, snaps to 0 within 0.02, holds 0.  SWELL g_r: onset -> 0, then the one-pole toward 1, tau T_r /
//       EXP_ENV_TRAVEL_TAUS, snaps to 1 at 98 %, holds 1.  Before the first onset both = 1. g_ds / g_rs = each one-pole
//       slid (EXP_PS_SLIDE_MS 5 ms, lands exactly).
//     t4 -> (smoothed; recomputed when it moved), Z = EXP_SPEC_MORPH_ZONE 0.2 (noon -> ~1:00):
//       T_d = 0.45 (8 / 0.45)^(1 - |t4|) s for t4 < 0, 8 s for t4 >= 0 (EXP_PS_DEC_FAST_S / SLOW_S)
//       T_r = 0.01 s for t4 <= Z, 0.01 (4 / 0.01)^((t4 - Z) / (1 - Z)) s for t4 > Z (EXP_PS_RISE_*)
//       w = 0 for t4 <= 0, clamp(t4 / Z, 0, 1) for t4 > 0 (pluck -> swell blend). All continuous in t4; exact noon =
//       pure pluck at 8 s (continuous with just CCW of noon).
//   LIVE DYNAMICS (TouchOpenUpdate): env = the onset detector's slow baseline on_base_ (|in| one-pole 2.6 ms -> gate
//     meter, up at once / 40 ms fall -> one-pole tau 34.7 ms; raw 48 kHz input, broadband); L = clamp((dB(env) + 60) /
//     40, 0, 1) (EXP_SPEC_DYN_FLOOR_DB -60 / TOP_DB -20 dBFS), G = clamp((dB(env) + 60) / 12, 0, 1) (GATE_DB 12 dB);
//     L_d = L rising at once, falling with tau EXP_SPEC_DYN_REL_FRAC 0.05 T_d (8 s -> 400 ms, 0.45 s -> 22 ms).
//       o_d = min(g_ds, L_d) - the pluck, closed further by the note's own decay / a mute (damped, not cut).
//       o_r = g_rs G - the swell, closed by the soft gate only near silence.
//       o = (1 - w) o_d + w o_r. Per 8 kHz sample over the whole note (never held at the onset); no slide on o.
//     o is computed on FM LAB always and on SW3 MID while the current model is not a DOWN model; L_d restarts at L when
//     it starts again. Looping: o = 1.
//   K5 LFO (LfoUpdate, per 8 kHz sample before the touch env; one shared LFO, free-running phase, never reset by onsets):
//     t5 = 2 k5 - 1 (smoothed, lands exactly), u = |t5|; noon = off (applied 0 exactly -> identical to no LFO).
//     shape: CCW triangle (bipolar +-1) | CW downward staircase EXP_LFO_STEPS +1 / 0 / -1 (100 / 50 / 0 %),
//             equal thirds of the cycle, repeating (2026-10-28, was S&H)
//     depth = min(u / 0.3, 1) (EXP_LFO_DEPTH_FRAC), rate = 0.2 (20 / 0.2)^u Hz (EXP_LFO_HZ_MIN / MAX)
//     applied l = depth shape, one-pole slid 1 ms (EXP_ENV_FM_SLIDE_MS, lands exactly): the steps don't click;
//             a side change passes through depth 0
//     target fixed per family, +- around the knob, clamped (Derive's K2 offset o2, per model via LfoO2):
//       UP FM ratio zones  feedback b = capped b + 0.3 l (EXP_LFO_FB_SWING), [0, EXP_FBFM_FB_MAX]; XBAND: none
//       FM LAB UP          feedback b as UP FM ratio zones (2-OP); depth x the touch env fade-in 1 - w (1 - o_r)
//                          (1 at / CCW of noon, o_r past the morph zone; factor slid 1 ms) - the only K5 fade-in
//       FM LAB DOWN (ADD)  A: h, B: index knob, STRETCH: delta, each clamp(k2 + 0.3 l, 0, 1) (EXP_ADD_LFO_SWING);
//                          UNISON: no LFO (K5 = beat speed)
//       FM LAB MID (SPEC)  none (K5 = SUB level; the shared LFO runs on, unused)
//       MID tables         morph m = m_knob + 0.15 l (EXP_LFO_MORPH_SWING), [0, 1]; parked BITTAB: none
//       DOWN               none
//     K4 acts on fc (DampFmUpdate UP / TfUpdate MID, o3_ = o), K5 on feedback / morph (Derive, per sample): independent.
//  LOOPING (RETIRED 2026-10-28: only with EXP_LOOP_ENABLE, then on SW3 DOWN instead of FM LAB) — generic K4 / K5
//   envelope modulation (discovery comparison):
//   e (0..1), per 8 kHz sample; 0 before the first onset; the envelope runs regardless of K4 noon:
//     LOOPING   each onset e = 0 rising; linear triangle 0 -> 1 -> 0 ..., reflecting at 0 / 1; one ramp
//               T = 4 s (0.03 / 4)^s, s = max(0, (|t4| - 0.08) / 0.92).
//     (ExpEnv also holds the former ONE-SHOT e law - each onset e = 0, one-pole rise to 1 over 36 s -> 450 ms -
//     which SW3 MID ran before the glide / pluck-swell; kept, not run.)
//   applied v = dir e, dir = sign(t4) (CW up, CCW down); v = 0 at |t4| <= EXP_LOOP_PAD 0.08;
//   v one-pole slid (1 ms, lands exactly): onset restarts and pad crossings glide.
//   K5: d2 = max(-t5, 0) (CCW) on the K2 parameter, d3 = max(t5, 0) (CW) on the K3 parameter, noon off.
//   MODULATION, in the parameter's own unit, knob = the start: p = clamp(p_knob + d v span, natural range):
//     UP FM          K2 feedback b: span EXP_FBFM_FB_MAX (1.5), [0, FB_MAX]; XBAND: K2 target does nothing
//     UP             K3 dampener fc: log2 fc + d3 v log2(4000 / 150) octaves, [150, 4000] Hz (the sqrt curve on the
//                    knob side only); per 8 kHz sample (DampFmUpdate)
//     MID            K3 2-pole LP cutoff: log2 fc_f + d3 v log2(16000 / 250) octaves, [250, 16000] Hz; per 8 kHz
//                    sample (TfUpdate). The tables' dampener: not modulated.
//     MID tables     K2 morph m: span 1, [0, 1]                 (parked BITTAB: K2 / K3 shape not modulated)
//     DOWN harm.     K2 n: span 5, [1, 6]; K3 blend: span 1, [0, 1]
//     DOWN f-shift   K2: |t| (= g / log2(801), g = log2(|s| + 1)) + d2 v, [0, 1], sign of the knob t (t = 0: +);
//                    K3 scale blend: span 1, [0, 1]
//     DOWN interval  K2 semitones: round(clamp(st_knob + 24 d2 v, -12, 12)), slid 1 ms (EXP_DISC_SLIDE_MS);
//                    K3 cents: span 100, [-50, 50]
//   Continuous params per 8 kHz sample in Derive (as their knobs); K1 not modulated.
//
// PARKED (compile flags, code kept, not run):
//   EXP_FOLLOW_ENABLE = false    live follower (EnvFollower, 48 kHz raw input, 20 Hz 4-pole, x10); true: e + 0.15 f
//                                (f = min(follower, 1)) before the gate / sign.
//   EXP_POSTFILT_ENABLE = false  K4 post filter. true = as before on SW1 MID / DOWN (not run on UP: mix slides
//                                to 0): EXP_FILT_STAGES TPT SVF (12 dB/oct each, Q 0.707), t = 2 k4 - 1, noon
//                                bypass (fade over |t| < EXP_FILT_FADE), CCW LP 4 kHz -> 180 Hz, CW HP 40 Hz -> 4 kHz
//                                (K4 would then drive both the filter and the envelope).
//
// CONTROL MAP SW3 MID (Explorer(false))
//   K1  selection    SW1 UP: FM ratio zone (14 EXP_FM_RATIOS) | XBAND at full CW; MID: table zone (10, stepped,
//                    1 ms crossfade); DOWN: model (harmonics / freq shift / interval)
//   K2  tone         UP: feedback b 0 -> 1.5 (XBAND: inert); MID: morph m 0..1 (smoothed,
//                    continuous) through the zone's frames; DOWN: per model (see MODELS)
//   K3  UP:          index: the per-band cap law (fc 150 Hz CCW -> 4 kHz CW open, k3^0.5; L = min(4000, fc) caps the
//                    index and the feedback)
//       MID:         dampener as UP (static: knob only, never modulated) + 2-pole SVF lowpass (12 dB/oct, Q 0.707)
//                    250 Hz (CCW) -> 16 kHz (CW, practically open), plain exponential (noon 2 kHz); the K4 touch
//                    env moves the LP only
//       DOWN:        per model (see MODELS)
//   K4  touch env on the K3 fc (UP index, MID 2-pole LP; DOWN inert; docs/DESIGN_PHILOSOPHY.md §12): CCW = pluck
//                    (each onset opens to the K3 fc, decays to the bottom; 0.45 s at the end -> 8 s at noon; a mute /
//                    the volume closes it damped), noon -> ~1:00 = pluck morphs into the fastest swell, CW = swell
//                    (each onset drops to the bottom, rises to the K3 fc; 10 ms at ~1:00 -> 4 s; closed near silence)
//       (retired LOOPING: envelope time + direction: CCW down | pad +-0.08 off | CW up; loop ramp 4 s -> 30 ms)
//   K5  LFO on K2's parameter (UP FM feedback +-0.3, MID morph +-0.15; XBAND / DOWN none): CCW triangle (up to 80 Hz) | noon off | CW
//                    staircase 100/50/0 %; depth fades in over the first 30 % of each side, rate 0.2 Hz -> top,
//                    exponential over the side; phase synced on each onset
//       (retired LOOPING: env target: CCW on K2's parameter, CW on K3's parameter; amount |2 k5 - 1|)
//   K6  dry / wet    (shell)
//   SW1 family       UP = FM, MID = tables, DOWN = harmonics & shift
//   SW2 octave       UP = +1 oct, MID = as played, DOWN = -1 oct (one slot, crossfade EXP_OCT_XFADE_MS)
//   FS1 wet mute     toggle, LED2 on while muted; LED1 on. FS2 unused.
//   DIAG builds: FS1 tap also = numbered "note this setting" line; `auto` line after settling.
// CONTROL MAP SW3 DOWN = FM LAB (Explorer(true); see MODELS / FM LAB)
//   K1  selection    SW1 UP: 2-OP mode (3 equal stepped: HARMONIC / DISHARMONIC / FREE, ratio slid 1 ms); MID: COMB SHAPE e 0.25 (narrow) -> 1 (noon,
//                    cosine) -> 4 (peaky), continuous (smoothed); DOWN: ADDITIVE zone (12 equal stepped, 1 ms crossfade):
//                    SAW-A / SAW-B / ODD-A / ODD-B / OCT-A / OCT-B / SUB-A / SUB-B / UNI-SAW / UNI-ODD / UNI-SAW-H / STRETCH
//   K2  UP: ratio r (HARMONIC / DISHARMONIC: 15 stepped zones, noon 1 / 1.414; FREE: 0.125 -> 8 continuous); MID: comb density nu 0 -> 0.5 (CW = every second partial notched); DOWN: A harmonics h,
//                    B index (0 -> 6 rad, k^2), UNI unison amount u, STRETCH delta 0 -> 0.05
//   K3  index        UP: bipolar character, pitch-dependent index (SW3 MID's law, k3 -> u_eff: index fc 150 -> 4000 Hz,
//                    index 12 capped per band): CCW hollow (no feedback) | noon pure sine | CW + feedback 0 -> 1.5 from
//                    noon (same caps); MID: TILT
//                    gamma 3 (dull) -> 1 (noon, saw) -> 0 (flat); DOWN: A / UNI-SAW / UNI-ODD / STRETCH index (6 k3^2, capped
//                    by L), B / UNI-SAW-H harmonics h (and L on their anti-fold edge)
//   K4  touch env (as SW3 MID, one openness o): UP along K3 (sine <-> the K3 position); DOWN on the index fc (DOWN
//                    B / UNI-SAW-H: also h); MID: on the density nu = 0.5 clamp(k2 + travel) (time-scaled travel) and
//                    the K3 LPF (see FM LAB SW1 MID)
//   K5  UP / DOWN: LFO (shapes / rates / sync as SW3 MID): UP on b +-0.3 after the caps (depth x the touch env
//                    fade-in 1 - w (1 - o_r));
//                    DOWN: A on h,
//                    B on the index knob, STRETCH on delta (+-0.3 knob units); UNI: BEAT SPEED (CCW 8 Hz x f'/100 Hz
//                    proportional | noon 0 | CW 8 Hz constant, 0.1 Hz just off noon, exponential). MID: unused (bank on K1)
//   K6 / SW2 / FS1 as SW3 MID
//   SW1 family       UP = 2-OP FM, MID = SPECTRAL, DOWN = ADDITIVE
//
// K2..K5 are one-pole smoothed per 8 kHz sample (EXP_CTRL_SMOOTH_MS); continuous derived values are computed per
// 8 kHz sample from the smoothed knobs (+ the envelope offset; ONE-SHOT: the K5 LFO offset, the K4 touch env o acts on
// the K4 targets only);
// stepped items from the raw knob.
// DIAG second half (touch env fields `gd<pluck g> Td<s> gr<swell g> Tr<s> w<blend> L<live level> o<openness>`, the
// envelope state at the snapshot): FM LAB (SW3 DOWN) UP `LAB 2OP-HARM|DISH|FREE r<ratio> hol|sine|fb fc<index fc Hz>
// b<feedback> (at the knob, K4 o 1, before the caps) u<u_eff now>` + ` gd.. Td.. gr.. Tr.. w.. L.. o.. lfo tri|stp|off
// d<depth at the knob> ><x the fade-in now> ..Hz`,
// SW1 MID `LAB SPEC bank <n>/9 <NAME> nu<applied density> k2<knob> lpf <Hz|off> sub <level|off> gd<decay g> Td<s>
// gr<rise g> Tr<s> w<blend> tr<travel> L<live level> o<openness>` (no fc / lfo part), SW1 DOWN `LAB ADD-<zone> <K2 name>
// <knob> > <with LFO | beat Hz + prop / hz / off> <K3 name> <value at the knob> fc ..` (K2 / K3 names h / I / u / d);
// + the SW3 MID UP part below (FM LAB DOWN); retired LOOPING `loop e.. up|down|off d.. K2|K3|-`; SW3 MID UP ` gd.. Td..
// gr.. Tr.. w.. L.. o.. fce .. (Hz, applied fc: UP dampener / index fc) lfo tri|stp|off d.. ..Hz` (depth, rate at the
// knob value), MID the same without fce (= eff), `ps - lfo -` on SW1 DOWN (K4 / K5 inert). MID WT lines: `dmp .. lp .. eff ..` = the tables' dampener fc (knob, unmodulated), the LP cutoff at the
// knob, the effective LP cutoff (Hz) instead of `fc` (parked ladder: lp / eff = the ladder's).
//
// TABLES (SW1 MID; COPIED from wavemorph.h, values = wavemorph_constants.h as EXP_TB_*; built at Init into SDRAM,
// int16, packed [frame][version][sample]): per zone F_z frames (frame 1 = the exact base wave), u = frame / (F_z - 1).
// Each frame: defined in the time domain at EXP_TB_TD_SIZE (1024) points and analysed by DFT into harmonics 1..127
// [time], or defined directly by harmonic amplitudes, sine phase [spectral]; mean removed, peak normalised to 1
// (an all-silent frame stays silent); zones 1, 2, 3, 6, 10 re-phased to the saw's phase convention (sine phase,
// positive; magnitudes kept; peak 1 again where not RMS-matched); zones 3 and 6..10 scaled to the RMS of the plain
// saw (zone 3 frame 1, full band), then capped to the int16 storage headroom (32767 / EXP_TB_ZONE_I16_SCALE[zone]);
// then rebuilt as 14 band-limited versions {1,2,3,4,6,8,12,16,24,32,48,64,96,127} at 256 points.
//   1 SINE SYNC  7 frames: sin(2 pi frac(R phi)), R = 1 | 1.5 | 2 | 2.7 | 4 | 4.6 | 6.5             [time]
//   2 TRI FOLD   fold(G tri(phi)), G = 1 | 2 | 4 | 8, fold(v) = |((v - 1) mod 4) - 2| - 1           [time]
//   3 SAW ROUGH  3 frames: saw 1/n | sine + needle (sin + Hann spike width 0.015 at phi 0.25, height 2;
//                re-phased) | needle saw 1/n^0.3; equal RMS (the plain saw's)        [spectral; 2 time]
//   4 PWM        16 frames: +1 (phi < w) else -1, w = 0.5 (1 - u)^2.2: 50 % -> 0 % (silence)          [time]
//   5 VOCAL      16 frames: a -> e -> i -> o -> u, formants interpolated, 110 Hz reference          [spectral]
//   6 PPG CLASSIC 8-step saw (8 levels) | stepped odd-harmonic stack (32 steps, 16 levels) | bit pattern
//                ((s 5) ^ (s >> 2)) & 15, 32 steps | 16-step held sine                             [time]
//   7 SOFT A     tine e-piano | hollow clarinet | drawbar organ | church mixture                   [spectral]
//   8 SOFT B     harmonium | ghost fifths | glass bell | stepped lo-res (sine held in 6 steps) [spectral; 4 time]
//   9 HARD A     PPG resonant | nasal oboe reed | formant brass | bass bell 7/14                  [spectral]
//  10 HARD B     sync saw (3.5x, reset per cycle) | fuzz reed | clipper | PPG feedback (saw 1/n
//                without harmonics 1, 2)                                                    [1 time; spectral]
//   (harmonic amplitude lists in explorer_constants.h, EXP_TB_*)
//
// Header-only; main.cpp includes daisy.h / hothouse.h / control_surface.h / module.h first.
// No dynamic allocation. Tables: 66 frames x 14 x 256 int16 = 462 KB in SDRAM (explorer_tb, own array, built once,
// shared read-only by both instances; the second Init skips the build). FM LAB ADDITIVE state: explorer_add_st,
// 12 x 3 x 84 floats = 12 KB (one copy, the lab instance only; D2 SRAM, EXP_FAST_BSS - 2026-11-02, was SDRAM).
// SPECTRAL has no per-band state (17-float weight table in the instance). (DUAL VOICE's explorer_dual_st, 9 KB,
// removed 2026-11-03.)

#include <cmath>
#include <cstdint>
#include "module.h"
#include "knob_map.h"          // RemapKnob, Quantize
#include "armitage_bands.h"    // band engine (read-only use)
#include "explorer_constants.h"
#include "env_follower.h"      // EnvFollower (live follower on top of the FM envelope)
#include "moog_ladder.h"       // MoogLadder (NitroTron3 Mode A's Huovilainen ladder, used as is): parked SW1 MID K3 lowpass (EXP_LAD_ENABLE)

#ifndef DTCM_MEM_SECTION
#define DTCM_MEM_SECTION       // host builds
#endif
#ifndef DSY_SDRAM_BSS
#define DSY_SDRAM_BSS          // host builds
#endif
// Fast internal RAM for the FM LAB hot per-band state (2026-11-02; was SDRAM): D2 SRAM via the linker script's
// `.heap` input section (STM32H750IB_sram.lds: NOLOAD, region RAM_D2 at 0x30008000, 256 KB; nothing else uses it -
// newlib's _sbrk starts at `end`, which the linker places after it). Outside the 32 KB non-cacheable DMA window
// (0x30000000, libDaisy MPU region 0) = normal cacheable memory; D2 SRAM1..3 clocks are enabled in SystemInit. No
// DMA touches it. NOLOAD = not zeroed at boot: the lab instance's Init (ResetSlotState) initialises it, as it did in
// SDRAM. DTCM (the default .bss) is ~89 % used; AXI SRAM has no NOLOAD section in this linker script. 32-byte aligned
// (cache line). Host builds: plain .bss.
#if defined(__arm__) && defined(__ARM_ARCH_7EM__)
#define EXP_FAST_BSS __attribute__((section(".heap"), aligned(32)))
#else
#define EXP_FAST_BSS
#endif

// Shared read-only sin table (filled in Init; DTCM = zero-wait-state reads).
// One guard entry [EXP_SIN_SIZE] = [0] (2026-11-02, CPU): the interpolation reads i + 1 without the wrap mask (same
// values, bit for bit).
static float DTCM_MEM_SECTION explorer_sin_tab[explorer_k::EXP_SIN_SIZE + 1];

// SW1 MID frame tables (WaveMorph's, copied), packed: [zone offset + frame][level][sample], int16
// (scale EXP_TB_ZONE_I16_SCALE[zone]), SDRAM. Explorer's own array (not WaveMorph's).
static int16_t DSY_SDRAM_BSS explorer_tb[explorer_k::EXP_TB_TOTAL_FRAMES]
                                        [explorer_k::EXP_TB_LEVELS][explorer_k::EXP_TB_SIZE];
// Init-only table build scratch (SDRAM: keeps DTCM free).
struct ExplorerTbScratch {
  float x[explorer_k::EXP_TB_TD_SIZE];                                  // time-domain frame
  float acc[explorer_k::EXP_TB_SIZE];                                   // level accumulator
  float A[explorer_k::EXP_TB_HMAX + 1], B[explorer_k::EXP_TB_HMAX + 1];   // sin / cos coefficients
};
static ExplorerTbScratch DSY_SDRAM_BSS explorer_tb_scratch;
// explorer_sin_tab + explorer_tb are filled by the first Init; further instances (SW3 DOWN) skip the build.
static bool explorer_tables_built = false;
// FM LAB ADDITIVE state (SW3 DOWN instance only; ONE copy, D2 SRAM (EXP_FAST_BSS), 2026-11-02): per SW2 slot and band.
struct ExplorerAddState {
  float th[3][armbands::kArmBands];                                // UNISON: detune phase theta (cycles)
  float wr[3][armbands::kArmBands], wi[3][armbands::kArmBands];   // SUB: previous half phasor w (0, 0 = none)
  float slp[3][armbands::kArmBands];                               // STRETCH: previous phi (-1 = fresh)
  float sp[explorer_k::EXP_ADD_STR_M][3][armbands::kArmBands];     // STRETCH: phase of harmonic m (cycles)
};
static ExplorerAddState EXP_FAST_BSS explorer_add_st;
// FM LAB SPECTRAL state (SW3 DOWN instance only; ONE copy, D2 SRAM, 2026-11-04): K4 partial-shift phase theta (cycles,
// [0, 1)) per slot (up / played / down) and band.
struct ExplorerSpecState {
  float th[3][armbands::kArmBands];
  float ur[3][armbands::kArmBands], ui[3][armbands::kArmBands];   // K5 SUB: previous half phasor u (0, 0 = none)
  bool  subh[3];                                                   // K5 SUB: history present (cleared when K5 off)
};
static ExplorerSpecState EXP_FAST_BSS explorer_spec_st;

// K4 post filter stage: TPT state-variable filter (a local copy of armitage's ArmSVF).
// SPECTRAL bank step table (non-HARM banks, compile time per bank b): the member multiples as floats, the bank's
// structure - constant stride D (m_i = 1 + (i - 1) D: ODD, STRIDE3..7), Fibonacci (m_i = m_{i-1} + m_{i-2}, m_0 = 1:
// FIB) or general (PRIMES: per member the class of its gap m_i - m_{i-1} among the bank's few distinct gaps).
struct ExpSpecBankTab {
  float mf[explorer_k::EXP_SPEC_M + 1] = {};      // m_i as float (edge: m_i f0)
  int   stride = 0;                                 // D > 0: constant stride from m_1 = 1
  bool  fib = false;
  int   ng = 0;                                     // general: distinct gaps
  int   gap[8] = {};
  int   cls[explorer_k::EXP_SPEC_M + 1] = {};       // general: gap class of member i (2..M)
  constexpr ExpSpecBankTab(int b) {
    using namespace explorer_k;
    const int (&m)[EXP_SPEC_M + 1] = EXP_SPEC_BANK[b];
    for (int i = 1; i <= EXP_SPEC_M; i++) mf[i] = (float)m[i];
    const int d = m[2] - m[1];
    bool st = (m[1] == 1);
    for (int i = 3; i <= EXP_SPEC_M; i++) if (m[i] - m[i - 1] != d) st = false;
    if (st) { stride = d; return; }
    bool fb = (m[1] == 1 && m[2] == 2);
    for (int i = 3; i <= EXP_SPEC_M; i++) if (m[i] != m[i - 1] + m[i - 2]) fb = false;
    if (fb) { fib = true; return; }
    for (int i = 2; i <= EXP_SPEC_M; i++) {
      const int g = m[i] - m[i - 1];
      int c = 0;
      while (c < ng && gap[c] != g) c++;
      if (c == ng) gap[ng++] = g;                   // (static_assert below: ng <= 8)
      cls[i] = c;
    }
  }
};
template <int B> constexpr ExpSpecBankTab kExpSpecBankTab = ExpSpecBankTab(B);

struct ExpSVF {
  float ic1 = 0.f, ic2 = 0.f, a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 0.f;
  void Set(float fc, float Q, float sr) {
    const float g = tanf(3.14159265f * fc / sr);
    k = 1.f / Q;
    a1 = 1.f / (1.f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  void CopyCoefFrom(const ExpSVF& o) { a1 = o.a1; a2 = o.a2; a3 = o.a3; k = o.k; }
  inline void Process(float x, float& lp, float& hp) {
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.f * v1 - ic1;
    ic2 = 2.f * v2 - ic2;
    lp = v2; hp = x - k * v1 - v2;
  }
  void Reset() { ic1 = ic2 = 0.f; }
};

class Explorer : public Module {
 public:
  static constexpr int kBands  = armbands::kArmBands;
  static constexpr int kFactor = armbands::kArmFactor;
  enum { kUp = 0, kMid = 1, kDown = 2 };   // = cs.Switch() values
  enum { kSU = 0, kSP = 1, kSD = 2, kSlots = 3 };   // slots: up, played, down (SW2 selects one)
  // Global model ids (family offset + K1 zone).
  enum { kFM = 0, kXFM,                               // SW1 UP: FM ratio zones, XBAND zone
         kWT, kBIT,                                   // SW1 MID
         kHARM, kFSH, kISH,                           // SW1 DOWN   (FM LAB UP / DOWN placeholder: kFM + its own ratio slide)
         kLSPEC,                                      // FM LAB SW1 MID: SPECTRAL (one model, no zones)
         kLADD,                                       // FM LAB SW1 DOWN: ADDITIVE (item = K1 zone 0..11)
         kNumModels };
  enum { kRotFSH = 0, kRotISH = 1, kRotN = 2 };                  // rotation-phase users

  // down = false: SW3 MID (ONE-SHOT); true: SW3 DOWN = FM LAB (ONE-SHOT, own SW1 families), or with
  // EXP_LOOP_ENABLE the retired LOOPING variant. See K4 / K5 above.
  explicit Explorer(bool down = false)
      : loop_(down && explorer_k::EXP_LOOP_ENABLE), lab_(down && !explorer_k::EXP_LOOP_ENABLE) {}
  bool Looping() const { return loop_; }
  bool Lab() const { return lab_; }

  void Init(float sr) override {
    using namespace explorer_k;
    sr8_ = sr / (float)kFactor;
    sr_ = sr;
    const bool build = !explorer_tables_built;  // shared read-only tables: built by the first instance only
    if (build)
      for (int i = 0; i < EXP_SIN_SIZE; i++)
        explorer_sin_tab[i] = sinf(6.283185307f * (float)i / (float)EXP_SIN_SIZE);
    if (build) explorer_sin_tab[EXP_SIN_SIZE] = explorer_sin_tab[0];   // guard entry (wrap without the mask)
    for (int k = 0; k < kBands + 2; k++) mag_[k] = 0.f;
    static const float r[kSlots] = { 2.f, 1.f, 0.5f };          // up / played / down
    for (int k = 0; k < kBands; k++) {
      const float f = armbands::ArmCenterFreq(k);
      bands_[k].Init(f, sr8_, armbands::ArmBandwidth(k));
      {                                                    // armitage ARM_ALIGN fitted weight (played slot)
        float fc = f;
        if (fc < EXP_ALIGN_W_FLO) fc = EXP_ALIGN_W_FLO;
        if (fc > EXP_ALIGN_W_FHI) fc = EXP_ALIGN_W_FHI;
        const float x = log2f(fc);
        wmid_[k] = powf(10.f, (EXP_ALIGN_WA * x * x + EXP_ALIGN_WB * x + EXP_ALIGN_WC) / 20.f);
      }
      for (int o = 0; o < kSlots; o++) {
        const float fp = r[o] * f;                         // f'_k
        fp_[o][k] = fp;
        ifp_[o][k] = 1.f / fp;
        const int hk = (int)(EXP_TB_FMAX / fp);           // open picks (fc = EXP_DAMP_FMAX); DampUpdate re-picks
        int lv = 0;
        for (int l = 0; l < EXP_TB_LEVELS; l++) if (EXP_TB_LEVEL_H[l] <= hk) lv = l;
        int st = EXP_BT_STEPS;                             // BITTAB steps (anti-fold: (S/2) f' <= limit)
        if (EXP_ANTIFOLD)
          while (st > EXP_BT_STEPS_MIN && 0.5f * (float)st * fp > EXP_FOLD_LIMIT_HZ) st >>= 1;
        wtlv_[o][k] = wtlvf_[o][k] = (uint8_t)lv;
        btsc_[o][k] = btsf_[o][k] = (uint8_t)st;
      }
    }
    for (int h = 0; h <= EXP_TB_HMAX; h++) {             // dampener: version for H <= h (largest LEVEL_H <= h)
      int lv = 0;
      for (int l = 0; l < EXP_TB_LEVELS; l++) if (EXP_TB_LEVEL_H[l] <= h) lv = l;
      wt_lv_of_h_[h] = (uint8_t)lv;
    }
    for (int o = 0; o < kSlots; o++)                     // open table blend (fc = EXP_DAMP_FMAX); DampUpdate recomputes
      for (int k = 0; k < kBands; k++) BlendPick(EXP_TB_FMAX * ifp_[o][k], tblo_[o][k], tbx_[o][k]);
    damp_k_ = -1.f;                              // forces DampFmUpdate on the first sample
    damp_o_ = 0.f;
    tpk_k_ = -1.f;                               // forces the tables' (unmodulated) dampener fc on the first block
    tpk_fc_ = EXP_DAMP_FMAX;
    for (int o = 0; o < kSlots; o++) pick_fc_[o] = -1.f;   // every slot re-picks before it first runs
    damp_fc_ = EXP_DAMP_FMAX;
    hc_fold_ = EXP_FOLD_LIMIT_HZ;
    damp_lnr_ = logf(EXP_DAMP_FMAX / EXP_DAMP_FMIN);
    pk_fade_n_ = 0;
    pk_fade_len_ = (int)(EXP_DAMP_SLIDE_MS * 0.001f * sr8_ + 0.5f);
    if (pk_fade_len_ < 1) pk_fade_len_ = 1;
    pk_g_ = 1.f;
    // Parked SW1 MID K3 ladder lowpass (Mode A's MoogLadder; lad_step_ also = the 2-pole LP's m slide): fixed resonance / drive, mix m = 0 (not run), cleared state.
    lad_.Init(sr);
    lad_.SetResonance(EXP_LAD_RES);
    lad_.SetDrive(EXP_LAD_DRIVE);
    lad_.SetCutoff(EXP_LAD_FMAX);
    lad_set_fc_ = EXP_LAD_FMAX;
    lad_fc_ = EXP_LAD_FMAX;
    lad_lnr_ = logf(EXP_LAD_FMAX / EXP_LAD_FMIN);
    lad_m_ = 0.f;
    lad_step_ = 1.f / (EXP_LAD_XFADE_MS * 0.001f * sr);
    for (int j = 0; j < kLadSub; j++) { lad_fcb_[j] = EXP_LAD_FMAX; lad_tgb_[j] = false; }
    // SW1 MID K3 2-pole lowpass (ExpSVF, LP output): mix m = 0 (not run), cleared state, coefficients at TF_FMAX.
    tf_svf_.Set(EXP_TF_FMAX, EXP_TF_Q, sr);
    tf_svf_.Reset();
    tf_set_fc_ = EXP_TF_FMAX;
    tf_fc_ = EXP_TF_FMAX;
    tf_lnr_ = logf(EXP_TF_FMAX / EXP_TF_FMIN);
    tf_lnb_ = logf(EXP_TF_ENV_BOTTOM / EXP_TF_FMIN);
    tf_m_ = 0.f;
    for (int j = 0; j < kLadSub; j++) { tf_fcb_[j] = EXP_TF_FMAX; tf_tgb_[j] = false; }
    sp_svf_.Set(EXP_SPEC_LPF_FHI, EXP_SPEC_LPF_Q, sr);   // SPECTRAL K3 LPF
    sp_svf_.Reset();
    sp_set_fc_ = sp_fc_ = EXP_SPEC_LPF_FHI; sp_m_ = 0.f;
    for (int j = 0; j < kLadSub; j++) { sp_fcb_[j] = EXP_SPEC_LPF_FHI; sp_tgb_[j] = false; }
    BuildBitPatterns();
    {                                            // packed frame offset per zone (BuildTables sets the same values)
      int off = 0;
      for (int z = 0; z < EXP_TB_ZONES; z++) { tb_off_[z] = off; off += EXP_TB_ZONE_FRAMES[z]; }
    }
    if (build) { BuildTables(); explorer_tables_built = true; }
    lab_m_t_ = -1;                               // FM LAB 2-OP: no UP mode until Controls (ratio slide at rest, r 1)
    lab_st_t_ = EXP_LAB_N_STEPS / 2;
    l2r_.Jump(EXP_LAB_REST_R);
    lab_fbf_ = 1.f;                              // K5 LFO fade-in factor (touch env: 1 - w (1 - o_r))
    lab_ue_c_ = -1.f;                            // forces LabFmUpdate on the first sample
    lab_L_ = EXP_FOLD_LIMIT_HZ;
    for (int n = 1; n <= EXP_SPEC_M; n++) {      // ln n (tilt), y(n) = n - 1 (comb; warp fixed 0)
      sp_ln_[n] = logf((float)n);
      sp_y_[n] = (float)(n - 1);
      sp_tilt_[n] = sp_comb_[n] = spec_w_[n] = 0.f;
    }
    spec_w_[1] = sp_tilt_[1] = sp_comb_[1] = 1.f;
    sk_g_ = sk_nu_ = sk_e_ = sk_k1_ = -1.f;      // forces every part on the first SpecWeights
    sk_Td_ = sk_Tr_ = sp_nu_last_ = -1.f; sp_nu_div_ = 0;   // depth / nu recompute state
    sp_tr_ = 0.f;                                // SPECTRAL density travel
    // K4 TOUCH ENV (both instances). TouchOpenUpdate: openness, live level, L_d (restarts at L on the first run)
    te_o_ = te_od_ = te_or_ = 1.f; te_L_ = 0.f; te_Ld_ = 0.f; te_Ld_init_ = true; te_LdT_ = -1.f;
    // TouchEnvUpdate: pluck / swell envelopes, both 1 before the first onset; T / w forced on the 1st sample
    te_gd_ = te_gds_ = te_xd_ = 1.f; te_drun_ = false;
    te_gr_ = te_grs_ = 1.f;
    te_t4_ = 2.f; te_Td_ = te_Tr_ = -1.f; te_w_ = 0.f;
    te_cd_ = te_dgd_ = te_cr_ = 0.f; sp_dd_ = sp_dr_ = 0.f;
    te_lnfl_ = EXP_SPEC_DYN_FLOOR_DB * (logf(10.f) / 20.f);                  // dB -> ln of the linear level
    te_flin_ = expf(te_lnfl_);
    te_iLr_ = 20.f / ((EXP_SPEC_DYN_TOP_DB - EXP_SPEC_DYN_FLOOR_DB) * logf(10.f));
    te_iGr_ = 20.f / (EXP_SPEC_DYN_GATE_DB * logf(10.f));
    sp_redo_ = true;
    spec_bank_t_ = spec_bank_ = EXP_SPEC_BANK_HARM;   // start-up: HARM; Controls sets the K1 bank (slid 1 ms)
    sp_bg_ = 1.f;
    sp_isr8_ = 1.f / sr8_;
    sp_ln_e_ = logf(EXP_SPEC_E_MAX / EXP_SPEC_E_MIN);
    for (int m = 1; m <= EXP_ADD_K_MAX; m++) add_ln_[m] = logf((float)m);   // FM LAB ADDITIVE: m^-s = exp(-s ln m)
    for (int c = 0; c < 2; c++) { add_ks_[c] = -1; add_kh_[c] = -1.f; add_kd_[c] = -1.f; }
    diag_add_z_ = -1;
    diag_lab_z_ = -1;
    for (int o = 0; o < kSlots; o++) { ResetSlotState(o); slot_on_[o] = false; }
    dc_a_ = expf(-6.283185307f * EXP_DC_HZ / sr);
    dc_x1_ = dc_y1_ = 0.f;
    ctrl_a_ = 1.f - expf(-1000.f / (EXP_CTRL_SMOOTH_MS * sr8_));
    mfade_len_ = (int)(EXP_MODEL_XFADE_MS * 0.001f * sr8_ + 0.5f);
    if (mfade_len_ < 1) mfade_len_ = 1;
    zfade_len_ = (int)(EXP_TB_ZONE_XFADE_MS * 0.001f * sr8_ + 0.5f);   // MID table zone change
    if (zfade_len_ < 1) zfade_len_ = 1;
    mfade_cur_ = mfade_len_;
    mfade_n_ = 0;
    disc_len_ = (int)(EXP_DISC_SLIDE_MS * 0.001f * sr8_ + 0.5f);
    if (disc_len_ < 1) disc_len_ = 1;
    sp_pstep_ = 2.f / (float)disc_len_;          // SPECTRAL bank gain slide: 1 -> 0 / 0 -> 1 over EXP_DISC_SLIDE_MS / 2 each
    oct_step_ = 1.f / (EXP_OCT_XFADE_MS * 0.001f * sr8_);
    var_ = var_t_ = vfrom_ = VarKey(kFM, 0);
    k1s_ = k1_t_ = 0.f;                          // smoothed K1 (unused since 2026-11-05: SPECTRAL K1 = raw bank zones)
    k2s_ = k2_t_ = 0.f;
    k3s_ = k3_t_ = 0.f;
    k4s_ = k4_t_ = 0.5f;                         // K4 noon = envelope not applied
    k5s_ = k5_t_ = 0.5f;                         // K5 noon = no target
    // Onset detector (48 kHz, pre-decimator, armitage's) + envelope e (8 kHz): ONE-SHOT or LOOPING (loop_).
    on_env_ = on_gate_ = on_base_ = 0.f;
    on_env_a_  = 1.f - expf(-1000.f / (EXP_ONSET_ENV_MS * sr));
    on_fall_a_ = 1.f - expf(-1000.f / (EXP_ONSET_FALL_MS * sr));
    on_refr_ = 0;
    on_refr_len_ = (int)((float)EXP_ONSET_REFRACTORY_MS * 0.001f * sr + 0.5f);
    on_pending_ = false;
    on_seen_ = false;
    env_.Reset();
    mv_ = 0.f;
    mv_a_ = 1.f - expf(-1000.f / (EXP_ENV_FM_SLIDE_MS * sr8_));
    ps_a_ = 1.f - expf(-1000.f / (EXP_PS_SLIDE_MS * sr8_));
    o2_ = 0.f;
    o3_ = loop_ ? 0.f : 1.f;                     // ONE-SHOT: o3_ = the K4 touch env openness o (1 = the K3 value)
    ln_tau_ratio_  = logf(EXP_ENV_TAU_FAST_S / EXP_ENV_TAU_SLOW_S);
    ln_loop_ratio_ = logf(EXP_LOOP_T_FAST_S / EXP_LOOP_T_SLOW_S);
    // RETIRED ONE-SHOT K4 pluck <-> swell (PsUpdate, not called since 2026-11-05; ps_a_ / ln_ps_* = the touch env's).
    ps_g_ = ps_gs_ = 1.f;
    ps_t4_ = 2.f;                                // forces the coefficient on the first sample
    ps_rise_ = true;
    ps_T_ = ps_Ts_ = EXP_PS_RISE_FAST_S;
    ps_plucked_ = false;
    ps_c_ = ps_c2_ = 0.f;
    ps_x1_ = 1.f;
    ps_dg_ = 0.f;
    ln_ps_dec_  = logf(EXP_PS_DEC_SLOW_S / EXP_PS_DEC_FAST_S);
    ln_ps_rise_ = logf(EXP_PS_RISE_SLOW_S / EXP_PS_RISE_FAST_S);
    // ONE-SHOT K5 LFO (LfoUpdate): off at noon, phase 0.
    ln_lfo_ = logf(EXP_LFO_HZ_MAX / EXP_LFO_HZ_MIN);
    ln_lfo_ccw_ = logf(EXP_LFO_HZ_MAX_CCW / EXP_LFO_HZ_MIN);
    lfo_u_ = 2.f;                                // forces the rate / depth on the first sample
    lfo_inc_ = 0.f;
    lfo_depth_ = 0.f;
    lfo_ph_ = 0.f;
    lfo_rng_ = EXP_LFO_SEED;
    lfo_sh_ = 0.f;
    lfo_s_ = 0.f;
    // Live follower (parked, EXP_FOLLOW_ENABLE; 48 kHz, raw input).
    fol_.Init(sr);
    fol_.SetCutoff(EXP_FOLLOW_HZ);
    fol_.SetPreGain(EXP_FOLLOW_GAIN);
    fol_y_ = fol_f_ = 0.f;
    filt_off_ = false;
    for (int i = 0; i < 3; i++) { og_[i] = og_t_[i] = 0.f; }
    og_[1] = og_t_[1] = 1.f;                     // SW2 MID = played slot
    rfmc_i_t_ = 0;
    rfmc_.Jump(EXP_FM_RATIOS[0]);
    ish_t_ = 0;
    ish_.Jump(0.f);
    // K4 post filter: bypass, cleared state.
    ft_ = ft_t_ = 0.f;
    f_on_ = false;
    f_hp_ = false;
    f_sz_ = 0;
    f_a_ = 0.f;
    f_m_ = 0.f;
    f_step_ = 1.f / (EXP_FILT_SLIDE_MS * 0.001f * sr);
    f_lnhp_ = logf(EXP_FILT_HP_MAX / EXP_FILT_FMIN);
    f_lnlp_ = logf(EXP_FILT_LP_MIN / EXP_FILT_FMAX);
    for (int i = 0; i < 2; i++) svf_[i].Reset();
    mute_ = false;
    mute_g_ = 1.f;
    mute_step_ = 1.f / (EXP_MUTE_SLIDE_MS * 0.001f * sr);
  }

  void Controls(const ControlSurface& cs,
                daisy::Led& led1, daisy::Led& led2) override {
    using namespace explorer_k;
    if (cs.Foot(0).rising) mute_ = !mute_;   // FS1: wet mute toggle
    const float k1 = RemapKnob(cs.Knob(0));
    const float k2 = RemapKnob(cs.Knob(1));
    const float k3 = RemapKnob(cs.Knob(2));
    const float k4 = RemapKnob(cs.Knob(3));      // envelope time + direction (t4)
    const float k5 = RemapKnob(cs.Knob(4));      // envelope target + amount (t5)
    int fam = cs.Switch(0);
    if (fam < kUp || fam > kDown) fam = kMid;
    int oct = cs.Switch(1);
    if (oct < kUp || oct > kDown) oct = kMid;
    int model, zone = 0, labz = -1;             // labz: FM LAB UP mode (0 HARMONIC, 1 DISHARMONIC, 2 FREE)
    int specz = -1, addz = -1;
    if (lab_ && fam == kDown) {                  // FM LAB DOWN: ADDITIVE, K1 = 12 stepped zones (1 ms model crossfade)
      addz = Quantize(k1, EXP_ADD_N_ZONES);
      lab_m_t_ = -1;                             // (2-OP ratio slide rests at r 1)
      model = kLADD;
      zone = addz;
    } else if (lab_ && fam == kMid) {            // FM LAB MID: SPECTRAL, K1 = comb shape (continuous, smoothed)
      specz = 0;
      lab_m_t_ = -1;                             // (2-OP ratio slide parked at r 1 while MID runs, as before)
      model = kLSPEC;
    } else if (lab_) {                           // FM LAB UP: kFM (2-OP law, ratio from l2r_; K3 / K4 / K5 in Derive / ShapeK)
      labz = Quantize(k1, EXP_LAB_N_MODES);      // K1 = 3 modes HARMONIC / DISHARMONIC / FREE (no XBAND)
      lab_st_t_ = Quantize(k2, EXP_LAB_N_STEPS); // K2 = ratio zone (HARMONIC / DISHARMONIC; FREE: the smoothed K2)
      lab_m_t_ = labz;
      model = kFM;
    } else if (fam == kUp) {                     // K1: FM ratio zones, last zone = XBAND
      model = (Quantize(k1, EXP_N_UP) < EXP_FM_N_RATIOS) ? kFM : kXFM;
    } else if (fam == kMid) {                    // K1: table zone (+ bit table zone when enabled)
      const int z = Quantize(k1, EXP_N_MID);
      if (z < EXP_TB_ZONES) { model = kWT; zone = z; }
      else                 { model = kBIT; zone = Quantize(k2, EXP_BT_WAVES); }   // waveform index
    } else {
      model = kHARM + Quantize(k1, EXP_N_DOWN);
    }
    var_t_ = VarKey(model, zone);
    int ri = Quantize(k1, EXP_N_UP);                                  // FM ratio item (held at the last
    if (ri > EXP_FM_N_RATIOS - 1) ri = EXP_FM_N_RATIOS - 1;           // ratio in the XBAND zone)
    rfmc_i_t_ = ri;
    ish_t_ = Quantize(k2, 2 * EXP_IS_SEMI_MAX + 1) - EXP_IS_SEMI_MAX; // interval, semitones
    k1_t_ = k1;
    k2_t_ = k2;
    k3_t_ = k3;
    k4_t_ = k4;
    k5_t_ = k5;
    if (EXP_POSTFILT_ENABLE) {                                        // parked: MID / DOWN K4 post filter
      ft_t_ = 2.f * k4 - 1.f;
      filt_off_ = (fam == kUp);                                       // UP: no post filter
    }
    og_t_[0] = (oct == kUp)   ? 1.f : 0.f;   // SW2 UP   -> +1 slot
    og_t_[1] = (oct == kMid)  ? 1.f : 0.f;   // SW2 MID  -> played slot
    og_t_[2] = (oct == kDown) ? 1.f : 0.f;   // SW2 DOWN -> -1 slot
    if (specz >= 0) spec_bank_t_ = SpecBankSel(k1);   // K1: bank (raw knob, 9 equal zones)

    led1.Set(1.f);
    led2.Set(mute_ ? 1.f : 0.f);

    if (NL_DIAG) {
      const float kv[5] = { k1, k2, k3, k4, k5 };
      const float disc = (model == kFM) ? EXP_FM_RATIOS[rfmc_i_t_] : (float)ish_t_;
      const Params p = Derive(model, zone, k2, k3, disc, 0.f, 0.f);   // knob values (no envelope offset)
      const float t4 = 2.f * k4 - 1.f, t5 = 2.f * k5 - 1.f;
      int dir;                                   // LOOPING: K4 direction; ONE-SHOT: knob side (-1 CCW pluck, +1 noon / CW)
      if (loop_) dir = (fabsf(t4) <= EXP_LOOP_PAD) ? 0 : (t4 > 0.f) ? 1 : -1;
      else       dir = (t4 < 0.f) ? -1 : 1;
      const int tgt = (t5 < 0.f) ? 2 : (t5 > 0.f) ? 3 : 0;
      // K3 fc at the knob value: UP dampener, MID lowpass cutoff (2-pole SVF; parked ladder), DOWN 0; lfe =
      // effective lowpass cutoff (MID, else 0); dfc = the tables' dampener fc (MID: knob only, unmodulated; else 0).
      // FM LAB: every family is the UP 2-OP (index fc, no LP / tables' dampener).
      const bool fmf = (fam == kUp) || lab_, mid = (fam == kMid) && !lab_;
      const float fck = fmf ? DampFc(k3) : mid ? (EXP_LAD_ENABLE ? LadFc(k3) : TfFc(k3)) : 0.f;
      const float lfe = mid ? (EXP_LAD_ENABLE ? lad_fc_ : tf_fc_) : 0.f;
      dfc_diag_ = mid ? DampFc(k3) : 0.f;
      if (loop_)
        DiagControls(cs, led1, kv, fam, oct, model, zone, p.a, p.b, fck, lfe,
                     env_.g, dir, fabsf(t5), tgt, 0.f, 0.f, 0, 0.f, 0.f);
      else {                                     // ONE-SHOT: fc_eff (applied, Hz; the K4 touch env state: DiagControls)
        const float u5 = fabsf(t5);              // + K5 LFO at the knob value: shape, depth, rate (Hz)
        const int lsh = (t5 < 0.f) ? 1 : (t5 > 0.f) ? 2 : 0;
        const float ldp = (u5 >= EXP_LFO_DEPTH_FRAC) ? 1.f : u5 * (1.f / EXP_LFO_DEPTH_FRAC);
        const float lhz = EXP_LFO_HZ_MIN * expf(u5 * logf(EXP_LFO_HZ_MAX / EXP_LFO_HZ_MIN));
        float da = p.a, db = p.b;                // FM LAB: feedback b at the knob / with the LFO
        diag_lab_z_ = labz;
        diag_spec_z_ = specz;
        diag_add_z_ = addz;
        if (labz >= 0) LabDiagParam(labz, k2, k3, ldp, da, db);
        if (specz >= 0) SpecDiagParam(k1, k2, da, db);
        if (addz >= 0) AddDiagParam(addz, k2, k3, k5, da, db);
        DiagControls(cs, led1, kv, fam, oct, model, zone, da, db, fck, lfe,
                     0.f, dir, 0.f, 0, 0.f, fmf ? damp_fc_ : lfe, lsh, ldp, lhz);
      }
    }
  }

  // --- DIAG snapshot (written in Controls(), main loop; read by main.cpp) ---
  struct DiagSnap {
    int   n;          // tap number (#n), 0 = automatic line
    float k[5];       // K1..K5 (remapped 0..1)
    int   sw1, sw2;   // 0 UP, 1 MID, 2 DOWN
    int   model;      // global model id (kFM .. kISH)
    int   zone;       // table zone (0..9), bit-table waveform - 1, else 0
    float a, b;       // derived values per model (FM: ratio r / feedback b; XBAND: partner n / index;
                      // WTABLE: frame position (F_z - 1) m / morph m)
    float fc;         // K3 fc at the knob value (Hz): SW1 UP dampener, MID lowpass cutoff (2-pole SVF; parked:
                      // ladder), 0 on DOWN
    float lfe;        // SW1 MID: effective lowpass cutoff (Hz, incl. K4 / K5 envelope), else 0
    float dfc;        // SW1 MID: the tables' dampener fc (Hz, knob only - not modulated), else 0
    bool  loop;       // variant: false ONE-SHOT (SW3 MID, FM LAB), true retired LOOPING (EXP_LOOP_ENABLE)
    float env;        // LOOPING: envelope e (0..1) at the snapshot (before gate / sign / slide); ONE-SHOT: 0 (touch env: sgd ..)
    int   dir;        // LOOPING K4: +1 CW up, -1 CCW down, 0 off (pad); ONE-SHOT: knob side +1 noon / CW, -1 CCW
    float d;          // K5 amount |2 k5 - 1|
    int   tgt;        // K5 target: 2 = K2 parameter (CCW), 3 = K3 parameter (CW), 0 = none (noon) (LOOPING only)
    float T, fce;     // T: 0 (was the retired PsUpdate's time); fce: ONE-SHOT applied fc (Hz; UP dampener / index fc,
                      // MID lowpass, 0 on SW1 DOWN); LOOPING: 0
    int   lfo;        // ONE-SHOT K5 LFO shape: 0 off (noon), 1 triangle (CCW), 2 staircase (CW); LOOPING: 0
    float lfod, lfohz;// ONE-SHOT K5 LFO depth (0..1) and rate (Hz) at the knob value; LOOPING: 0
    int   labz;       // FM LAB (SW3 DOWN) UP 2-OP: mode 0 HARMONIC / 1 DISHARMONIC / 2 FREE (a = ratio r at the knob, b =
                      // index fc (Hz) at the knob (caps' L = min(4000, fc)), lb = feedback b at the knob (both K4 o = 1,
                      // before the per-band caps), lue =
                      // u_eff with the current touch env o, llfd = K5 depth at the knob x the fade-in 1 - w (1 - o_r) now, lside = K3 side
                      // -1 hollow / 0 sine / +1 feedback); -1 = not the 2-OP family
    float lb, lue, llfd;
    int   lside;
    int   specz;      // FM LAB SW1 MID SPECTRAL: 0 (a = applied density nu, b = K2 knob, sn = K1 bank index; the K4
                      // morph below); else -1
    float sn;         // K1 bank index
    float slp;        // K3 LPF fc (Hz), 0 = off
    float ssub;       // K5 SUB level g_sub, 0 = off
    float sgd, sTd;   // K4 TOUCH ENV (every ONE-SHOT model; names kept from SPECTRAL): pluck envelope g_d (raw), its time T_d (s)
    float sgr, sTr;   //   swell envelope g_r (raw), its time T_r (s)
    float sw, str;    //   blend w (0 pluck .. 1 swell), SPECTRAL density travel (K2 knob units; SPECTRAL lines only)
    float sL, so;     //   live level L (0..1), openness o (0..1, blended) at the snapshot
    int   addz;       // FM LAB SW1 DOWN ADDITIVE: zone 0..11 (a / b = K2 parameter at the knob / with the K5 LFO or, UNISON,
                      // b = beat Hz; c3 = K3 parameter at the knob; bm = beat mode 0 off, 1 prop (CCW), 2 Hz (CW)); else -1
    float c3;
    int   bm;
    bool  mute;
  };
  void Activate() override {
    if (NL_DIAG) { diag_changed_ = true; diag_t_ = daisy::System::GetNow(); }
  }
  // K3 dampener fc (Hz) at k3 (0..1): DAMP_FMIN (DAMP_FMAX / DAMP_FMIN)^(k3^DAMP_CURVE), exactly DAMP_FMAX
  // at k3 >= 1, DAMP_FMIN at k3 <= 0.
  static float DampFc(float k3) {
    using namespace explorer_k;
    return (k3 >= 1.f) ? EXP_DAMP_FMAX : EXP_DAMP_FMIN * expf(DampCurve(k3) * logf(EXP_DAMP_FMAX / EXP_DAMP_FMIN));
  }
  // k3^EXP_DAMP_CURVE (0 at k3 <= 0; sqrtf for the 0.5 default). Per 8 kHz sample while the FM cap inputs move (DampFmUpdate), and DIAG.
  static inline float DampCurve(float k3) {
    using namespace explorer_k;
    if (k3 <= 0.f) return 0.f;
    return (EXP_DAMP_CURVE == 0.5f) ? sqrtf(k3) : (EXP_DAMP_CURVE == 1.f) ? k3 : powf(k3, EXP_DAMP_CURVE);
  }
  bool DiagTake(DiagSnap& out) {
    if (diag_tap_pend_)  { out = diag_tap_;  diag_tap_pend_ = false;  return true; }
    if (diag_auto_pend_) { out = diag_auto_; diag_auto_pend_ = false; return true; }
    return false;
  }
  static const char* ModelName(int m) {
    static const char* const n[kNumModels] = { "FM", "XBAND-FM",
                                               "WTABLE", "BITTAB",
                                               "HARM", "FSHIFT", "INTERVAL", "SPEC", "ADD" };
    return (m >= 0 && m < kNumModels) ? n[m] : "?";
  }
  // FM LAB 2-OP mode name (DIAG): 2OP-HARM / 2OP-DISH / 2OP-FREE.
  static const char* LabZoneName(int z) {
    return (z >= 0 && z < explorer_k::EXP_LAB_N_MODES) ? explorer_k::EXP_LAB_MODE_NAMES[z] : "?";
  }
  // FM LAB 2-OP K3 side name (DIAG): hol (CCW) / sine (noon) / fb (CW).
  static const char* LabSideName(int s) { return (s < 0) ? "hol" : (s > 0) ? "fb" : "sine"; }
  // FM LAB ADDITIVE zone name (DIAG).
  static const char* AddZoneName(int z) {
    return (z >= 0 && z < explorer_k::EXP_ADD_N_ZONES) ? explorer_k::EXP_ADD_ZONES[z].name : "?";
  }
  // FM LAB ADDITIVE K2 / K3 parameter names (DIAG): h harmonics, I index, u unison amount, d stretch delta.
  static const char* AddK2Name(int z) {
    using namespace explorer_k;
    if (z < 0 || z >= EXP_ADD_N_ZONES) return "?";
    const int c = EXP_ADD_ZONES[z].ctl;
    return (c == kAddA) ? "h" : (c == kAddB) ? "I" : (c == kAddStrC) ? "d" : "u";
  }
  static const char* AddK3Name(int z) {
    using namespace explorer_k;
    if (z < 0 || z >= EXP_ADD_N_ZONES) return "?";
    const int c = EXP_ADD_ZONES[z].ctl;
    return (c == kAddB || c == kAddUniH) ? "h" : "I";
  }
  // FM LAB SPECTRAL name (DIAG; no zones since 2026-11-03 continuous K1).
  static const char* SpecZoneName(int z) { return (z >= 0) ? "SPEC" : "?"; }
  // FM LAB SPECTRAL K1 bank name (DIAG / hostsim), index 0..8.
  static const char* SpecBankName(int b) {
    static const char* const nm[explorer_k::EXP_SPEC_BANKS] = {
        "FIB", "STRIDE7", "STRIDE6", "STRIDE5", "STRIDE4", "STRIDE3", "PRIMES", "ODD", "HARM" };
    return (b >= 0 && b < explorer_k::EXP_SPEC_BANKS) ? nm[b] : "?";
  }
  static const char* ZoneName(int m, int z) {
    static const char* const wt[explorer_k::EXP_TB_ZONES] = { "SINE-SYNC", "TRI-FOLD", "SAW-ROUGH", "SQR-PWM",
                                                             "VOCAL", "PPG-CLASSIC", "SOFT-A", "SOFT-B",
                                                             "HARD-A", "HARD-B" };   // = WaveMorph::ZoneName
    if (m == kWT && z >= 0 && z < explorer_k::EXP_TB_ZONES) return wt[z];
    return "";
  }

  void Process(const float* in, float* wet, size_t size) override {
    using namespace explorer_k;
    const float ca = ctrl_a_;
    DampUpdate();                                // K3 dampener: table picks (smoothed K3), per block
    for (size_t i = 0; i + kFactor <= size; i += kFactor) {
      for (size_t j = 0; j < kFactor; j++) {     // onset detector (+ parked live follower), 48 kHz, pre-decimator
        OnsetDetect(in[i + j]);
        if (EXP_FOLLOW_ENABLE) fol_y_ = fol_.Process(in[i + j]);
      }
      const float d = dec_(in + i);
      k1s_ += (k1_t_ - k1s_) * ca;
      if (fabsf(k1_t_ - k1s_) < EXP_CTRL_SNAP) k1s_ = k1_t_;   // land exactly
      k2s_ += (k2_t_ - k2s_) * ca;
      k3s_ += (k3_t_ - k3s_) * ca;
      if (fabsf(k3_t_ - k3s_) < EXP_CTRL_SNAP) k3s_ = k3_t_;   // land exactly (dampener re-picks stop)
      k4s_ += (k4_t_ - k4s_) * ca;
      if (fabsf(k4_t_ - k4s_) < EXP_CTRL_SNAP) k4s_ = k4_t_;   // land exactly (noon: t4 == 0)
      k5s_ += (k5_t_ - k5s_) * ca;
      if (fabsf(k5_t_ - k5s_) < EXP_CTRL_SNAP) k5s_ = k5_t_;   // land exactly (noon: no target)
      EnvUpdate();                               // K5 LFO + K4 touch env -> o3_ = openness o (consumes the onset flag)
      DampFmUpdate();                            // K3 dampener UP caps (smoothed K3 + o3 + LFO), per 8 kHz sample
      if (lab_) LabFmUpdate();                   // FM LAB 2-OP index fc / L from u_eff (smoothed K3 x touch env o)
      for (int j = 0; j < 3; j++) og_[j] = Slide(og_[j], og_t_[j], oct_step_);
      rfmc_.Set(EXP_FM_RATIOS[rfmc_i_t_], disc_len_);   // snapped items slide over EXP_DISC_SLIDE_MS
      rfmc_.Tick();
      {                                          // interval: knob semitones + 24 d2 v, clamped, rounded, then slid
        float st = (float)ish_t_;
        if (o2_ != 0.f) {
          st += (float)(2 * EXP_IS_SEMI_MAX) * o2_;
          st = (st < -(float)EXP_IS_SEMI_MAX) ? -(float)EXP_IS_SEMI_MAX
             : (st > (float)EXP_IS_SEMI_MAX) ? (float)EXP_IS_SEMI_MAX : st;
          st = floorf(st + 0.5f);
        }
        ish_.Set(st, disc_len_);
      }
      ish_.Tick();

      // Model / item change: start a crossfade (picked up when no fade is running).
      if (mfade_n_ == 0 && var_t_ != var_) {
        vfrom_ = var_;
        var_ = var_t_;
        const int mn = var_ >> 6;
        if (mn != (vfrom_ >> 6)) {                       // a new model starts cleared (items of the
          for (int o = 0; o < kSlots; o++) ResetModelState(mn, o);   // same model share its state)
        } else if (mn == kLADD) {                        // ADDITIVE zone -> zone: the entering zone's own state fresh
          AddEnterZone(var_ & 63, vfrom_ & 63);
        }
        // MID table zone -> table zone: EXP_TB_ZONE_XFADE_MS (WaveMorph's); every other change EXP_MODEL_XFADE_MS.
        mfade_cur_ = (mn == kWT && (vfrom_ >> 6) == kWT) ? zfade_len_ : mfade_len_;
        mfade_n_ = mfade_cur_;
      }
      const bool fading = (mfade_n_ > 0);
      if (lab_) {                                // FM LAB: the 2-OP ratio slides (1 ms) to K1 mode / K2's (MID / DOWN: r 1)
        l2r_.Set(LabRatio(lab_m_t_, lab_st_t_, k2s_), disc_len_);   // FREE: target follows the smoothed K2
        l2r_.Tick();
      }
      // K2 / K3 parameters: knob + envelope offset (o2_ / o3_, knob units 0..1 = the full span), per 8 kHz sample.
      // ONE-SHOT: o3_ = the K4 touch env openness o (UP dampener fc / MID 2-pole LP fc only: DampFmUpdate / TfUpdate; FM LAB
      // 2-OP / ADDITIVE read o in LabFmUpdate / Derive) -> no K3 model offsets (K4 inert on SW1 DOWN); the K2
      // offset is the K5 LFO's, per model (LfoO2: FM feedback / table morph, 0 elsewhere), so a model crossfade
      // gives each side its own unit. LFO off (noon): exactly 0 -> Derive identical to the knob value.
      const float d3 = loop_ ? o3_ : 0.f;
      const int mnew = var_ >> 6, mold = vfrom_ >> 6;
      pn_ = Derive(mnew, var_ & 63, k2s_, k3s_, DiscOf(mnew), loop_ ? o2_ : LfoO2(mnew), d3);
      if (fading) pf_ = Derive(mold, vfrom_ & 63, k2s_, k3s_, DiscOf(mold), loop_ ? o2_ : LfoO2(mold), d3);
      if (mnew == kLADD) AddWeights(pn_, 0);    // ADDITIVE: series weights (cached; recomputed when h / series moved)
      if (fading && mold == kLADD) AddWeights(pf_, 1);
      if (mnew == kLSPEC) SpecWeights(pn_);     // SPECTRAL: partial weights (one table; recomputed when an input moved)
      if (fading && mold == kLSPEC) SpecWeights(pf_);
      // ADDITIVE unison theta: advanced by the entering side; by the fading side only when the entering one has none
      pn_.adv = true;
      pf_.adv = !(mnew == kLADD && AddIsUni(var_ & 63));
      mg_ = fading ? 1.f - (float)mfade_n_ / (float)(mfade_cur_ + 1) : 1.f;
      pk_g_ = (pk_fade_n_ > 0) ? 1.f - (float)pk_fade_n_ / (float)(pk_fade_len_ + 1) : 1.f;   // dampener pick slide
      if (EXP_LAD_ENABLE) LadUpdate((int)(i / kFactor));   // parked SW1 MID ladder: mix target + cutoff
      else                TfUpdate((int)(i / kFactor));    // SW1 MID 2-pole LP: mix target + cutoff (this 8 kHz sample)
      SpecLpfUpdate((int)(i / kFactor), mnew);     // FM LAB SPECTRAL K3 LPF: mix target + cutoff

      // Pass 1: clock all bands, magnitudes (|z'| = |z| in every slot).
      float* __restrict mg = mag_ + 1;
      for (int k = 0; k < kBands; k++) {
        bands_[k].Update(d);
        const float yr = bands_[k].Re();
        const float yi = bands_[k].Im();
        const float n  = yr * yr + yi * yi;
        if (n < EXP_SILENCE) {
          mg[k] = 0.f;
          inv_[k] = 0.f;
        } else {
          const float iv = armbands::ArmFastInvSqrt(n);
          inv_[k] = iv;
          mg[k] = n * iv;
        }
      }

      // SW2: one voice (up / played / down); on a change the old and the new slot crossfade
      // (weights slid over EXP_OCT_XFADE_MS); a slot with weight 0 is not computed.
      static constexpr int   oslot[3] = { kSU, kSP, kSD };
      static constexpr float ogain[3] = { EXP_GAIN_UP, 1.f, EXP_GAIN_DOWN };
      float acc = 0.f;
      for (int j = 0; j < 3; j++) {
        const float g = og_[j];
        if (g != 0.f) acc += g * ogain[j] * RunSlot(oslot[j], fading);
        else slot_on_[oslot[j]] = false;
      }
      if (mfade_n_ > 0) --mfade_n_;
      if (pk_fade_n_ > 0) --pk_fade_n_;
      int_(acc * EXP_OUT_GAIN, wet + i);
    }
    DcBlock(wet, size);
    if (EXP_LAD_ENABLE) Ladder(wet, size);       // parked SW1 MID K3 ladder lowpass (mix m; not run while m = 0)
    else                TabFilter(wet, size);    // SW1 MID K3 2-pole SVF lowpass (mix m; not run while m = 0)
    SpecLpf(wet, size);                          // FM LAB SPECTRAL K3 2-pole SVF lowpass (mix m; not run while m = 0)
    if (EXP_POSTFILT_ENABLE) PostFilter(wet, size);   // parked K4: LP | bypass | HP
    Saturate(wet, size);
    ApplyMute(wet, size);
  }

 private:
  // Per-sample model parameters.
  struct Params {
    float a = 0.f, b = 0.f;     // sweep A / B as displayed (DIAG)
    float p0 = 0.f, p1 = 0.f, p2 = 0.f, p3 = 0.f, p4 = 0.f;   // model-specific (see Derive)
    float p5 = 0.f, p6 = 0.f, p7 = 0.f;                      // (FM LAB ADDITIVE: p5 delta, p7 h)
    int   n0 = 0;               // integer item (partner offset, harmonic, packed table frame)
    int   zone = 0;
    const float* bt = nullptr;  // BITTAB: the selected 16-step waveform
    // FM LAB ADDITIVE (kLADD): series / control kind, weight table w_m (AddWeights), highest member, STRETCH rho_m,
    // flags (cap at the K3 knob's L; anti-fold edge Fe = min(3800, L)); adv = this side advances the unison theta
    int   ser = 0, ctl = 0, top = 1;
    const float* aw = nullptr;
    const float* ar = nullptr;
    bool  capk = false, fel = false, adv = true;
  };

  // Linear slide of a snapped value to its new item over a fixed number of 8 kHz samples.
  struct DSlide {
    float v = 0.f, t = 0.f, step = 0.f;
    int   n = 0;
    void Jump(float x) { v = t = x; n = 0; }
    void Set(float target, int len) {
      if (target != t) { t = target; step = (t - v) / (float)len; n = len; }
    }
    void Tick() {
      if (n > 0) { v += step; if (--n == 0) v = t; }
    }
  };

  static inline int VarKey(int model, int zone) { return (model << 6) | (zone & 63); }

  // Onset detector (armitage OnsetDetect, 1:1), once per 48 kHz sample on |in| before the decimator.
  // Fast meter -> gate meter (up at once, slow fall) -> slow baseline; onset = gate > RISE x baseline
  // and > FLOOR, then a refractory. Latches on_pending_ for EnvUpdate() (8 kHz); the refractory is far
  // longer than one 8 kHz step, so an onset is never lost or double-counted.
  inline void OnsetDetect(float in_sample) {
    using namespace explorer_k;
    const float a = fabsf(in_sample);
    on_env_ += on_env_a_ * (a - on_env_);
    if (on_env_ > on_gate_) on_gate_ = on_env_;
    else                    on_gate_ += on_fall_a_ * (on_env_ - on_gate_);
    on_base_ += EXP_ONSET_SLOW_COEF * (on_gate_ - on_base_);
    if (on_refr_ > 0) on_refr_--;
    const bool onset = (on_refr_ == 0 && on_gate_ > EXP_ONSET_FLOOR &&
                        on_gate_ > on_base_ * EXP_ONSET_RISE);
    if (onset) { on_refr_ = on_refr_len_; on_pending_ = true; }
  }

  // Envelope e (0..1), one step per 8 kHz sample. Before the first onset (seen = false): e = 0. Runs regardless of
  // K4 noon (the gate is applied in EnvUpdate). am = |t4|.
  //   ONE-SHOT (loop = false): onset -> e = 0, one-pole rise to 1, travel = DECAY_RATIO TIME_RATIO TAU_SLOW
  //     (FAST / SLOW)^am (armitage EnvGain's rise law), tau = travel / TRAVEL_TAUS, snaps to 1 at 0.98, holds 1.
  //   LOOPING (loop = true): onset -> e = 0 rising; linear ramps of T = LOOP_T_SLOW (FAST / SLOW)^s,
  //     s = max(0, (am - PAD) / (1 - PAD)), reflecting at 0 / 1 (wavemorph fdc9927's LoopEnv).
  struct ExpEnv {
    volatile float g = 0.f;        // e
    int   dir = 0;                 // ONE-SHOT: 1 rising, 0 holding; LOOPING: +1 rising, -1 falling
    float at = -1.f, c = 0.f;      // am the coefficient was computed for; one-pole coefficient / ramp increment
    void Reset() { g = 0.f; dir = 0; at = -1.f; c = 0.f; }
    inline void Step(bool loop, bool onset, bool seen, float am, float sr8, float ln_tau, float ln_loop) {
      using namespace explorer_k;
      if (onset) { g = 0.f; dir = 1; }
      if (!seen) { g = 0.f; dir = 0; return; }
      if (am != at) {
        at = am;
        if (loop) {
          float s = (am - EXP_LOOP_PAD) * (1.f / (1.f - EXP_LOOP_PAD));
          if (s < 0.f) s = 0.f;
          c = 1.f / (EXP_LOOP_T_SLOW_S * expf(s * ln_loop) * sr8);
        } else {
          const float travel = EXP_ENV_FM_DECAY_RATIO * EXP_ENV_FM_TIME_RATIO * EXP_ENV_TAU_SLOW_S * expf(am * ln_tau);
          c = 1.f - expf(-EXP_ENV_TRAVEL_TAUS / (travel * sr8));
        }
      }
      if (loop) {
        float v = g + (float)dir * c;
        if (v >= 1.f)      { v = 2.f - v; dir = -1; }   // reflect at the top
        else if (v <= 0.f) { v = -v;      dir = 1;  }   // reflect at the bottom
        g = v;
      } else if (dir == 1) {
        float v = g + c * (1.f - g);
        if (v >= 0.98f) { v = 1.f; dir = 0; }
        g = v;
      }
    }
  };
  // Per 8 kHz sample: envelope step, gate + sign, 1 ms slide, K5 split into the K2 / K3 offsets.
  //   v_target = dir e (dir = sign(t4)); 0 at t4 == 0 (ONE-SHOT) / |t4| <= EXP_LOOP_PAD (LOOPING).
  //   mv_ = one-pole (tau EXP_ENV_FM_SLIDE_MS) toward v_target, lands exactly (EXP_CTRL_SNAP).
  //   o2_ = max(-t5, 0) mv_ (K2 parameter), o3_ = max(t5, 0) mv_ (K3 parameter); knob units (1 = full span).
  // ONE-SHOT instance (loop_ = false): LfoUpdate (K5), then the K4 TOUCH ENV (docs/DESIGN_PHILOSOPHY.md §12; 2026-11-05,
  // replaces PsUpdate): TouchEnvUpdate (pluck g_d + swell g_r, both always running on the instance, both retriggered by
  // every onset) and TouchOpenUpdate (openness o with the live level) -> o3_ = o, the one openness every K4 target of
  // the running model reads (UP index fc DampFmUpdate, MID 2-pole LP TfUpdate, FM LAB 2-OP u_eff, ADDITIVE index fc / h,
  // SPECTRAL K3 LPF; SPECTRAL density travel: SpecWeights). o computed on FM LAB always (every SW1 family uses K4),
  // on SW3 MID while the current model is not an SW1 DOWN model (there o3_ = 1, K4 inert, as before).
  inline void EnvUpdate() {
    using namespace explorer_k;
    const bool onset = on_pending_;
    on_pending_ = false;
    if (onset) on_seen_ = true;
    const float t4 = 2.f * k4s_ - 1.f;
    const float am = fabsf(t4);
    if (!loop_) {
      LfoUpdate(onset);                          // (PsUpdate retired 2026-11-05: not called, see there)
      TouchEnvUpdate(onset, t4);                 // K4 touch env: pluck / swell envelopes (same onset, smoothed t4)
      const bool down = IsDownModel(var_ >> 6);  // SW3 MID SW1 DOWN: K4 inert (o3_ = 1, o not computed)
      TouchOpenUpdate(lab_ || !down);            // openness o (+ o_d, o_r) with the live level
      o3_ = down ? 1.f : te_o_;
      {
        // K5 FM LFO fade-in (touch env; FM LAB 2-OP, and SW3 MID UP FM since 2026-11-05): fade = 1 - w (1 - o_r) - 1 at /
        // CCW of noon (w 0), the swell's openness o_r past the morph zone (w 1), continuous across it; one-pole slid
        // (mv_a_ = EXP_ENV_FM_SLIDE_MS 1 ms, lands exactly; carried over from the g_s fade-in).
        // SW3 MID UP FM (2026-11-05): swell side fades in with the swell (o_r), pluck side the LFO grows as the pluck
        // closes (1 - o_d): ft = w o_r + (1 - w) (1 - o_d).
        const float ft = lab_ ? 1.f - te_w_ * (1.f - te_or_) : te_w_ * te_or_ + (1.f - te_w_) * (1.f - te_od_);
        lab_fbf_ += mv_a_ * (ft - lab_fbf_);
        if (fabsf(ft - lab_fbf_) < EXP_CTRL_SNAP) lab_fbf_ = ft;
      }
      return;
    }
    env_.Step(loop_, onset, on_seen_, am, sr8_, ln_tau_ratio_, ln_loop_ratio_);
    float e = env_.g;
    if (EXP_FOLLOW_ENABLE) {                     // parked live follower on top: e + DEPTH min(follower, 1)
      fol_f_ = (fol_y_ < 1.f) ? fol_y_ : 1.f;
      e += EXP_FOLLOW_DEPTH * fol_f_;
    }
    const bool gated = loop_ ? (am <= EXP_LOOP_PAD) : (t4 == 0.f);
    const float vt = gated ? 0.f : (t4 > 0.f) ? e : -e;
    mv_ += mv_a_ * (vt - mv_);
    if (fabsf(vt - mv_) < EXP_CTRL_SNAP) mv_ = vt;
    const float t5 = 2.f * k5s_ - 1.f;
    o2_ = (t5 < 0.f) ? -t5 * mv_ : 0.f;
    o3_ = (t5 > 0.f) ?  t5 * mv_ : 0.f;
  }

  // RETIRED 2026-11-05 (not called; code kept): the former shared K4 "pluck <-> swell" g / g_s of both instances,
  // replaced by the touch env (TouchEnvUpdate / TouchOpenUpdate, docs/DESIGN_PHILOSOPHY.md §12). Nothing reads ps_g_ /
  // ps_gs_ any more; ps_a_ (EXP_PS_SLIDE_MS) and ln_ps_dec_ / ln_ps_rise_ are still the touch env's. As it was:
  // ONE-SHOT instance (SW3 MID): K4 "pluck <-> swell" on the K3 fc (UP dampener, MID 2-pole LP), per 8 kHz sample.
  //   Side from the smoothed t4: t4 < 0 DECAY (target 0, T = PS_DEC_FAST (PS_DEC_SLOW / PS_DEC_FAST)^(1 - |t4|)),
  //   t4 >= 0 RISE (target 1, T = PS_RISE_FAST (PS_RISE_SLOW / PS_RISE_FAST)^|t4|); side and T recomputed only
  //   when t4 moved. Onset: DECAY g = 1, RISE with t4 > 0 g = 0, exact noon nothing; such an onset sets
  //   ps_plucked_, a side change clears it. T_eff = T plucked, max(T, EXP_PS_LEAVE_S) unplucked (the leave move).
  //   DECAY: g 2-pole toward 0 (EXP_PS_DEC_2POLE; else one-pole / EXP_PS_DEC_LINEAR linear), snaps to 0 within 0.02. RISE: g one-pole
  //   toward 1. tau = T_eff / EXP_ENV_TRAVEL_TAUS, snaps exactly to the target within 0.02 (98 %), holds.
  //   A K4 turn never resets g: it continues from its value toward the (new) target at the (new) T_eff.
  //   ps_gs_ = g one-pole slid (tau EXP_PS_SLIDE_MS, coefficient ps_a_), lands exactly.
  //   o3_ = ps_gs_ (DampFmUpdate UP / TfUpdate MID: log2 fc = log2 FMIN + g_s (log2 fc_knob - log2 FMIN)); 1 while SW1 DOWN runs
  //   (K4 inert there). o2_ = 0 (the K5 LFO offset goes to Derive via LfoO2).
  inline void PsUpdate(bool onset, float t4) {
    using namespace explorer_k;
    bool redo = false;
    if (t4 != ps_t4_) {
      ps_t4_ = t4;
      const float am = fabsf(t4);
      const bool rise = (t4 >= 0.f);
      if (rise != ps_rise_) { ps_rise_ = rise; ps_plucked_ = false; }   // side changed: leave move until an onset
      ps_Ts_ = ps_rise_ ? EXP_PS_RISE_FAST_S * expf(am * ln_ps_rise_)
                        : EXP_PS_DEC_FAST_S * expf((1.f - am) * ln_ps_dec_);
      redo = true;
    }
    const float tg = ps_rise_ ? 1.f : 0.f;
    if (onset) {
      if (!ps_rise_)      { ps_g_ = ps_x1_ = 1.f; if (!ps_plucked_) { ps_plucked_ = true; redo = true; } }  // DECAY: pluck
      else if (t4 > 0.f)  { ps_g_ = 0.f; if (!ps_plucked_) { ps_plucked_ = true; redo = true; } }  // RISE: swell
    }                                                                   // (exact noon: nothing)
    if (redo) {                                  // T_eff = T_side once plucked, else max(T_side, EXP_PS_LEAVE_S)
      ps_T_ = (ps_plucked_ || ps_Ts_ >= EXP_PS_LEAVE_S) ? ps_Ts_ : EXP_PS_LEAVE_S;
      ps_c_ = 1.f - expf(-EXP_ENV_TRAVEL_TAUS / (ps_T_ * sr8_));     // RISE (and the one-pole decay): one-pole
      ps_c2_ = 1.f - expf(-EXP_PS_DEC_2P_TAUS / (ps_T_ * sr8_));     // DECAY 2-pole: per stage
      ps_dg_ = 1.f / (ps_T_ * sr8_);                                  // DECAY: linear step per 8 kHz sample
    }
    if (ps_g_ != tg) {
      float g;
      if (ps_rise_) {
        g = ps_g_ + ps_c_ * (1.f - ps_g_);
        if (1.f - g <= 0.02f) g = 1.f;
      } else if (explorer_k::EXP_PS_DEC_LINEAR) {
        g = ps_g_ - ps_dg_;                      // linear decay (afc62a9, rejected by ear: not snappy)
        if (g < 0.f) g = 0.f;
      } else if (explorer_k::EXP_PS_DEC_2POLE) {
        ps_x1_ += ps_c2_ * (0.f - ps_x1_);       // DECAY: 2-pole (two equal one-poles in series, critically damped):
        g = ps_g_ + ps_c2_ * (ps_x1_ - ps_g_);   // g = (1 + t / tau) e^(-t / tau) from a pluck - rounded start, steep
        if (g <= 0.02f) { g = 0.f; ps_x1_ = 0.f; }   // fall, the exponential tail; 98 % at T_eff, snaps to 0
      } else {
        g = ps_g_ + ps_c_ * (0.f - ps_g_);       // DECAY: one-pole (snappy), snaps to 0 within 0.02
        if (g <= 0.02f) g = 0.f;
      }
      ps_g_ = g;
    }
    if (ps_rise_ || ps_g_ == 0.f) ps_x1_ = ps_g_;   // 2-pole decay entered from rest starts at the current value
    ps_gs_ += ps_a_ * (ps_g_ - ps_gs_);
    if (fabsf(ps_g_ - ps_gs_) < EXP_CTRL_SNAP) ps_gs_ = ps_g_;
    o2_ = 0.f;
    o3_ = IsDownModel(var_ >> 6) ? 1.f : ps_gs_;
  }

  // ONE-SHOT instances (SW3 MID, FM LAB): K5 LFO, per 8 kHz sample (EnvUpdate, before the touch env). t5 = 2 k5s_ - 1, u = |t5|.
  //   depth = min(u / EXP_LFO_DEPTH_FRAC, 1), rate = EXP_LFO_HZ_MIN (EXP_LFO_HZ_MAX / EXP_LFO_HZ_MIN)^u (recomputed
  //   only when u moved). Free-running phase lfo_ph_ in [0, 1) (never reset). Shape: t5 < 0 triangle
  //   tri = 1 - 4 |frac(ph + 0.25) - 0.5| (0 at ph = 0, rising, +-1), t5 > 0 the downward staircase EXP_LFO_STEPS
  //   (+1 / 0 / -1 = 100 / 50 / 0 %, equal thirds of the cycle, repeating; 2026-10-28, was S&H), t5 == 0 nothing (0).
  //   The steps land through the slide below (1 ms), no click. Applied l = depth shape, one-pole slid
  //   (mv_a_ = tau EXP_ENV_FM_SLIDE_MS), lands exactly (EXP_CTRL_SNAP) -> lfo_s_ (0 exactly at noon once settled).
  inline void LfoUpdate(bool onset) {
    using namespace explorer_k;
    const float t5 = 2.f * k5s_ - 1.f;
    const float u = fabsf(t5);
    if (t5 != lfo_u_) {                          // (signed: the CCW side has its own top rate)
      lfo_u_ = t5;
      lfo_inc_ = EXP_LFO_HZ_MIN * expf(u * ((t5 < 0.f) ? ln_lfo_ccw_ : ln_lfo_)) / sr8_;
      lfo_depth_ = (u >= EXP_LFO_DEPTH_FRAC) ? 1.f : u * (1.f / EXP_LFO_DEPTH_FRAC);
    }
    if (onset) {                                 // phase sync on each attack (2026-10-28)
      if (t5 > 0.f) lfo_ph_ = 0.f;               // CW stepped pattern: step 1
      else if (t5 < 0.f) {                       // CCW triangle: aligned with the K4 envelope's direction
        const float t4 = 2.f * k4s_ - 1.f;       // tri(ph) = 1 - 4 |frac(ph + 0.25) - 0.5|
        lfo_ph_ = (t4 < 0.f) ? 0.25f             // decay: start at the peak (+1), falling
                : (t4 > 0.f) ? 0.75f             // rise: start at the bottom (-1), rising
                : 0.f;                           // noon: 0, rising
      }
    }
    lfo_ph_ += lfo_inc_;
    if (lfo_ph_ >= 1.f) lfo_ph_ -= 1.f;
    float w = 0.f;
    if (t5 < 0.f) {
      float x = lfo_ph_ + 0.25f;
      if (x >= 1.f) x -= 1.f;
      w = 1.f - 4.f * fabsf(x - 0.5f);
    } else if (t5 > 0.f) {                       // CW: downward staircase, EXP_LFO_STEPS equal steps per cycle
      int st = (int)(lfo_ph_ * (float)EXP_LFO_N_STEPS);
      if (st > EXP_LFO_N_STEPS - 1) st = EXP_LFO_N_STEPS - 1;
      w = EXP_LFO_STEPS[st];
    }
    const float lt = lfo_depth_ * w;
    lfo_s_ += mv_a_ * (lt - lfo_s_);
    if (fabsf(lt - lfo_s_) < EXP_CTRL_SNAP) lfo_s_ = lt;
  }
  // K2 offset of the K5 LFO in knob units for model m (Derive: p = clamp(p_knob + o2 span)): FM b swing
  // EXP_LFO_FB_SWING / span EXP_FBFM_FB_MAX, table morph swing EXP_LFO_MORPH_SWING / span 1; XBAND, BITTAB, DOWN: 0.
  inline float LfoO2(int m) const {
    using namespace explorer_k;
    if (m == kFM) return 0.f;                    // FM: the LFO is added after the cap in ShapeK (2026-10-28)
    // (FM LAB SPECTRAL: none - K5 = SUB level)
    if (m == kLADD) return lfo_s_ * EXP_ADD_LFO_SWING;          // FM LAB ADDITIVE: A h / B index knob / STRETCH delta
                                                                 // (Derive; not used by the UNISON zones)
    if (m == kWT) return EXP_TF_Q_ON_K5 ? 0.f : lfo_s_ * EXP_LFO_MORPH_SWING;   // K5 = LP Q on tables (temp)
    return 0.f;
  }

  // --- FM LAB helpers (SW3 DOWN instance) ---
  static inline bool IsDownModel(int m) { return m == kHARM || m == kFSH || m == kISH; }
  // FM LAB 2-OP ratio target: mode m (0 HARMONIC, 1 DISHARMONIC: stepped zone st; 2 FREE: RMIN (RMAX / RMIN)^k2s
  // continuous), m < 0 = SW1 MID / DOWN running: EXP_LAB_REST_R.
  static inline float LabRatio(int m, int st, float k2s) {
    using namespace explorer_k;
    if (m < 0 || m >= EXP_LAB_N_MODES) return EXP_LAB_REST_R;
    if (m == EXP_LAB_FREE)
      return EXP_LAB_FREE_RMIN * expf(Clamp01(k2s) * logf(EXP_LAB_FREE_RMAX / EXP_LAB_FREE_RMIN));
    if (st < 0) st = 0;
    if (st > EXP_LAB_N_STEPS - 1) st = EXP_LAB_N_STEPS - 1;
    return (m == 0) ? EXP_LAB_HARM_R[st] : EXP_LAB_DISH_R[st];
  }
  // FM LAB 2-OP K3 character at the smoothed / knob k3 and the K4 touch env openness o (gs): t3 = 2 k3 - 1, u_eff = o |t3|,
  // I = EXP_FM_INDEX, b = 0 (CCW / noon) or EXP_FBFM_FB_MAX u_eff (CW); before the per-band caps at L (LabFc).
  static inline void LabChar(float k3, float gs, float& I, float& b, float& ue) {
    using namespace explorer_k;
    const float t3 = 2.f * k3 - 1.f;
    ue = gs * fabsf(t3);
    if (ue > 1.f) ue = 1.f;
    I = EXP_FM_INDEX;
    b = (t3 > 0.f) ? EXP_FBFM_FB_MAX * ue : 0.f;
  }
  // FM LAB 2-OP index fc (Hz) at u_eff: SW3 MID's law with k3 -> u_eff (DampFc); L = min(EXP_FOLD_LIMIT_HZ, this).
  static inline float LabFc(float ue) { return DampFc(ue); }
  // DIAG: FM LAB 2-OP at the knob values: a = ratio r (FREE: from the raw knob), b = index fc (Hz, K4 g = 1; the caps'
  // L = min(EXP_FOLD_LIMIT_HZ, this)), diag_lab_b_ = feedback b (o = 1, before the caps), diag_lab_side_ (-1 hollow, 0 sine, +1 feedback side), diag_lab_ue_ = u_eff with the current
  // touch env openness o, diag_lab_lfd_ = the K5 depth at the knob x the fade-in factor now (1 - w (1 - o_r)).
  void LabDiagParam(int m, float k2, float k3, float lfod, float& a, float& b) {
    using namespace explorer_k;
    a = LabRatio(m, Quantize(k2, EXP_LAB_N_STEPS), k2);
    float ue, I;
    LabChar(k3, 1.f, I, diag_lab_b_, ue);
    b = LabFc(ue);
    const float t3 = 2.f * k3 - 1.f;
    diag_lab_side_ = (t3 < 0.f) ? -1 : (t3 > 0.f) ? 1 : 0;
    float Ig, bg;
    LabChar(k3, te_o_, Ig, bg, diag_lab_ue_);
    diag_lab_lfd_ = lfod * (1.f - te_w_ * (1.f - te_or_));
  }

  // --- FM LAB SPECTRAL helpers (SW1 MID, model kLSPEC; see FM LAB SW1 MID in the header) ---
  // DIAG: a = applied density nu (last SpecWeights), b = K2 knob, sn = K1 bank index (the K4 morph state: DiagControls).
  void SpecDiagParam(float k1, float k2, float& a, float& b) {
    a = diag_spec_nu_;
    b = Clamp01(k2);
    diag_spec_n_ = (float)SpecBankSel(k1);
  }
  // K1 bank at the raw k1 (0..1): Quantize(k1, EXP_SPEC_BANKS) = 0..8 (CCW FIB .. CW HARM).
  static inline int SpecBankSel(float k1) {
    using namespace explorer_k;
    return Quantize(Clamp01(k1), EXP_SPEC_BANKS);
  }
  // Comb exponent e at k1 (0..1): E_MIN (E_MAX / E_MIN)^k1 = 0.25 16^k1 (CCW 0.25 narrow, noon 1, CW 4 peaky).
  inline float SpecExp(float k1) const {
    using namespace explorer_k;
    return EXP_SPEC_E_MIN * expf(Clamp01(k1) * sp_ln_e_);
  }
  // Tilt exponent gamma at k (0..1), bipolar, flat at noon (2026-11-04): GAMMA_MAX 3 -> 0 over [0, 0.5], 0 -> GAMMA_MIN
  // -1 over [0.5, 1] (linear; negative = upper partials louder).
  static inline float SpecGamma(float k) {
    using namespace explorer_k;
    k = Clamp01(k);
    return (k <= 0.5f) ? EXP_SPEC_GAMMA_MAX * (1.f - 2.f * k)
                       : EXP_SPEC_GAMMA_MIN * (2.f * k - 1.f);
  }
  // Partial shift s at k4 (0..1): SHIFT_MAX (2 k4 - 1), noon 0 (harmonic), +-0.5 at the ends.
  static inline float SpecShift(float k4) {
    using namespace explorer_k;
    return EXP_SPEC_SHIFT_MAX * (2.f * Clamp01(k4) - 1.f);
  }
  // --- K4 TOUCH ENV (docs/DESIGN_PHILOSOPHY.md §12; one implementation per instance, both instances) ---
  // Built 2026-11-05 as FM LAB SPECTRAL's K4 morph (reference implementation), shared since 2026-11-05 by every K4 mode
  // (SW3 MID SW1 UP FM / XBAND index fc, SW1 MID tables 2-pole LP; FM LAB 2-OP u_eff, SPECTRAL, ADDITIVE index fc / h).
  // Constants: EXP_PS_* (times, shapes, slide), EXP_SPEC_MORPH_ZONE, EXP_SPEC_DYN_* (names kept from SPECTRAL).
  // TouchEnvUpdate: the two envelopes, per 8 kHz sample on the instance (EnvUpdate; whatever SW1 model runs, so a K4
  // mode finds them current), one onset / smoothed t4.
  //   t4 moved -> T_d = DEC_FAST (DEC_SLOW / DEC_FAST)^(1 - |t4|) (t4 < 0) | DEC_SLOW (t4 >= 0);
  //              T_r = RISE_FAST (t4 <= Z) | RISE_FAST (RISE_SLOW / RISE_FAST)^((t4 - Z) / (1 - Z)) (t4 > Z);
  //              w = 0 (t4 <= 0) | clamp(t4 / Z, 0, 1) (t4 > 0), Z = EXP_SPEC_MORPH_ZONE; each coefficient only when
  //              its T changed.
  //   onset -> g_d = 1 (decay starts), g_r = 0 (rise starts); before the first onset both = 1 (held).
  //   DECAY g_d: PsUpdate's decay law (EXP_PS_DEC_LINEAR linear | EXP_PS_DEC_2POLE 2-pole | one-pole) toward 0, snaps
  //              to 0 within 0.02, holds. RISE g_r: one-pole toward 1 (tau T_r / EXP_ENV_TRAVEL_TAUS), snaps at 98 %.
  //   g_ds / g_rs = one-pole slid (ps_a_, tau EXP_PS_SLIDE_MS), land exactly. No LEAVE, no side state.
  inline void TouchEnvUpdate(bool onset, float t4) {
    using namespace explorer_k;
    constexpr float Z = EXP_SPEC_MORPH_ZONE;
    if (t4 != te_t4_) {
      te_t4_ = t4;
      const float Td = (t4 < 0.f) ? EXP_PS_DEC_FAST_S * expf((1.f - fabsf(t4)) * ln_ps_dec_) : EXP_PS_DEC_SLOW_S;
      const float Tr = (t4 <= Z) ? EXP_PS_RISE_FAST_S
                                 : EXP_PS_RISE_FAST_S * expf((t4 - Z) * (1.f / (1.f - Z)) * ln_ps_rise_);
      te_w_ = (t4 <= 0.f) ? 0.f : (t4 >= Z) ? 1.f : t4 / Z;
      if (Td != te_Td_) {
        te_Td_ = Td;
        if (EXP_PS_DEC_LINEAR)     te_dgd_ = 1.f / (Td * sr8_);                               // linear step
        else if (EXP_PS_DEC_2POLE) te_cd_ = 1.f - expf(-EXP_PS_DEC_2P_TAUS / (Td * sr8_));    // per stage
        else                       te_cd_ = 1.f - expf(-EXP_ENV_TRAVEL_TAUS / (Td * sr8_));   // one-pole
      }
      if (Tr != te_Tr_) { te_Tr_ = Tr; te_cr_ = 1.f - expf(-EXP_ENV_TRAVEL_TAUS / (Tr * sr8_)); }
    }
    if (onset) { te_gd_ = te_xd_ = 1.f; te_drun_ = true; te_gr_ = 0.f; }
    if (te_drun_) {                              // DECAY (PsUpdate's decay branch, own state)
      float g;
      if (EXP_PS_DEC_LINEAR) {
        g = te_gd_ - te_dgd_;
        if (g < 0.f) g = 0.f;
      } else if (EXP_PS_DEC_2POLE) {
        te_xd_ += te_cd_ * (0.f - te_xd_);
        g = te_gd_ + te_cd_ * (te_xd_ - te_gd_);
        if (g <= 0.02f) { g = 0.f; te_xd_ = 0.f; }
      } else {
        g = te_gd_ + te_cd_ * (0.f - te_gd_);
        if (g <= 0.02f) g = 0.f;
      }
      te_gd_ = g;
      if (g == 0.f) te_drun_ = false;            // holds at 0 until the next onset
    }
    if (te_gr_ != 1.f) {                         // RISE (PsUpdate's rise branch, own state)
      float g = te_gr_ + te_cr_ * (1.f - te_gr_);
      if (1.f - g <= 0.02f) g = 1.f;
      te_gr_ = g;
    }
    te_gds_ += ps_a_ * (te_gd_ - te_gds_);
    if (fabsf(te_gd_ - te_gds_) < EXP_CTRL_SNAP) te_gds_ = te_gd_;
    te_grs_ += ps_a_ * (te_gr_ - te_grs_);
    if (fabsf(te_gr_ - te_grs_) < EXP_CTRL_SNAP) te_grs_ = te_gr_;
  }
  // TouchOpenUpdate (K4 DYNAMICS, 2026-11-05): openness o, per 8 kHz sample after TouchEnvUpdate (EnvUpdate), while a
  // K4 mode runs (run: FM LAB always; SW3 MID while the current model is not an SW1 DOWN model); not run -> nothing
  // computed, L_d restarts at L on the next run (the envelopes run on regardless). env = on_base_, the onset detector's
  // slow baseline (latest 48 kHz value: |in| one-pole 2.6 ms -> gate meter up at once / 40 ms fall -> one-pole tau
  // 34.7 ms; raw input):
  //   x = ln(env) - ln(floor) (one logf, only when env > floor; else L = G = 0)
  //   L = clamp((dB(env) - FLOOR_DB) / (TOP_DB - FLOOR_DB), 0, 1), G = clamp((dB(env) - FLOOR_DB) / GATE_DB, 0, 1)
  //   L_d = L rising at once, falling with tau EXP_SPEC_DYN_REL_FRAC T_d (coefficient only when T_d changed);
  //   o_d = min(g_ds, L_d), o_r = g_rs G; o = (1 - w) o_d + w o_r. No slide, no law switch.
  // Every K4 target of the running model reads the same o (o3_ / te_o_; FULL travel: target = bottom + o (K3 value -
  // bottom), as g_s before); SPECTRAL's density travel (time-scaled depth) is SPECTRAL's own (SpecWeights, from o_d /
  // o_r / w). Looping (retired LOOPING variant): not called; readers use o = 1.
  inline void TouchOpenUpdate(bool run) {
    using namespace explorer_k;
    if (!run) { te_Ld_init_ = true; return; }
    const float env = on_base_;
    float L = 0.f, G = 0.f;
    if (env > te_flin_) {
      const float x = logf(env) - te_lnfl_;      // nepers above the floor (> 0)
      L = x * te_iLr_; if (L > 1.f) L = 1.f;
      G = x * te_iGr_; if (G > 1.f) G = 1.f;
    }
    te_L_ = L;
    if (te_Td_ != te_LdT_) {                     // L_d release: tau = EXP_SPEC_DYN_REL_FRAC T_d, one expf when T_d changed
      te_LdT_ = te_Td_;
      te_Ld_a_ = 1.f - expf(-1.f / (EXP_SPEC_DYN_REL_FRAC * te_Td_ * sr8_));
    }
    // L_d: the decay side's live level - rises with L at once, falls with tau = EXP_SPEC_DYN_REL_FRAC T_d (a mute
    // on a long pluck closes damped, not cut; short plucks stay as fast as the follower itself).
    if (te_Ld_init_ || L >= te_Ld_) { te_Ld_ = L; te_Ld_init_ = false; }
    else                            te_Ld_ += te_Ld_a_ * (L - te_Ld_);
    const float Ld = te_Ld_;
    const float gds = te_gds_;
    const float od = (Ld < gds) ? Ld : gds;      // decay openness (damped live level)
    const float orr = te_grs_ * G;               // rise openness
    const float w = te_w_;
    te_od_ = od;
    te_or_ = orr;
    te_o_ = (1.f - w) * od + w * orr;
  }
  // SPECTRAL density travel (K2 knob units), per 8 kHz sample while kLSPEC runs (SpecWeights), from this sample's touch
  // env (TouchOpenUpdate): depth_d / depth_r = DEPTH_MIN + (DEPTH_MAX - DEPTH_MIN) clamp(ln(T / FAST) / ln(SLOW / FAST),
  // 0, 1) over each side's own EXP_PS_* range (only when T_d / T_r changed); travel = (1 - w) depth_d (1 - o_d) +
  // w depth_r (1 - o_r). SPECTRAL only (the other K4 modes travel the full range, no time-scaled depth).
  inline float SpecTravel() {
    using namespace explorer_k;
    if (te_Td_ != sk_Td_) {                      // depth_d: log over the decay range 0.45 .. 8 s
      sk_Td_ = te_Td_;
      sp_dd_ = EXP_SPEC_ENV_DEPTH_MIN + (EXP_SPEC_ENV_DEPTH_MAX - EXP_SPEC_ENV_DEPTH_MIN) *
                                        Clamp01(logf(te_Td_ / EXP_PS_DEC_FAST_S) / ln_ps_dec_);
    }
    if (te_Tr_ != sk_Tr_) {                      // depth_r: log over the rise range 0.01 .. 4 s
      sk_Tr_ = te_Tr_;
      sp_dr_ = EXP_SPEC_ENV_DEPTH_MIN + (EXP_SPEC_ENV_DEPTH_MAX - EXP_SPEC_ENV_DEPTH_MIN) *
                                        Clamp01(logf(te_Tr_ / EXP_PS_RISE_FAST_S) / ln_ps_rise_);
    }
    const float od = te_od_, orr = te_or_;
    const float w = te_w_;
    sp_tr_ = (1.f - w) * sp_dd_ * (1.f - od) + w * sp_dr_ * (1.f - orr);
    return sp_tr_;
  }
  // Partial weights spec_w_[1..M] (shared by every band / slot), per 8 kHz sample while kLSPEC runs (either side of a
  // model crossfade), each part only when its inputs moved: tilt when gamma moved (M - 1 expf), comb when nu / e moved
  // (M - 1 table cos + the exponent; y(n) = n - 1, phi 0), partials p(n) while sliding to the stepped N, then the
  // product + RMS norm. Also p.p0 = shift s.
  //   nu / gamma / e / s: see the 2026-11-05 note below;
  //   n = member index i of the active bank (all 16 on). Bank change (spec_bank_t_ != spec_bank_): gain g (sp_bg_) slides
  //   1 -> 0 over EXP_DISC_SLIDE_MS / 2, spec_bank_ = target at g = 0, then 0 -> 1 over EXP_DISC_SLIDE_MS / 2; g multiplies
  //   the normalised W (g = 1: bit-identical to no gain). W(1) = 1 (tilt(1) = comb(1) = 1).
  //   (2026-11-05: e / gamma / s at the fixed EXP_SPEC_K_PEAK / K_TILT / K_SHIFT; nu = NU_MAX clamp(k2s_ + travel,
  //   0, 1), travel = the K4 morph's density share (SpecTravel from this sample's touch env: both envelopes with the live level,
  //   blended by w; 0 while looping); comb redo while nu moves at most every EXP_SPEC_NU_DIV.)
  void SpecWeights(Params& p) {
    using namespace explorer_k;
    const float tr = loop_ ? 0.f : SpecTravel();   // K4 travel in K2 knob units (touch env; 0 while looping)
    // EXP_SPEC_K2_REVERSE (2026-11-05 test): the whole density mapping mirrored - knob and envelope travel flipped.
    const float kt = k2s_ + tr;
    const float nu = EXP_SPEC_NU_MAX * Clamp01(EXP_SPEC_K2_REVERSE ? 1.f - kt : kt);
    diag_spec_nu_ = nu;
    const float ga = SpecGamma(EXP_SPEC_K_TILT);
    if (sk_k1_ < 0.f) { sk_k1_ = EXP_SPEC_K_PEAK; sp_e_ = SpecExp(EXP_SPEC_K_PEAK); }   // e fixed (once)
    const float e = sp_e_;
    bool redo = sp_redo_;
    sp_redo_ = false;
    if (ga != sk_g_) {                           // tilt(n) = n^-gamma
      sk_g_ = ga;
      for (int n = 2; n <= EXP_SPEC_M; n++) sp_tilt_[n] = expf(-ga * sp_ln_[n]);
      redo = true;
    }
    {                                            // bank change: g out, swap at 0, g in (1 ms total)
      const int bt = spec_bank_t_;
      const float gt = (bt != spec_bank_) ? 0.f : 1.f;
      if (sp_bg_ != gt) { sp_bg_ = Slide(sp_bg_, gt, sp_pstep_); redo = true; }
      if (bt != spec_bank_ && sp_bg_ == 0.f) spec_bank_ = bt;
    }
    const bool nu_stop = (nu == sp_nu_last_);   // nu stopped moving this sample: land on it now
    sp_nu_last_ = nu;
    if (sp_nu_div_ > 0) sp_nu_div_--;
    if ((nu != sk_nu_ && (sp_nu_div_ == 0 || nu_stop)) || e != sk_e_) {   // comb(n) = (0.5 + 0.5 cos(2 pi nu (n - 1)))^e
      sk_nu_ = nu; sk_e_ = e;
      sp_nu_div_ = EXP_SPEC_NU_DIV;
      for (int n = 2; n <= EXP_SPEC_M; n++) {
        float b = 0.5f + 0.5f * Cos01(nu * sp_y_[n]);   // argument >= 0
        b = fminf(fmaxf(b, 0.f), 1.f);
        sp_comb_[n] = (b > 0.f) ? expf(e * logf(b)) : 0.f;   // b^e, general path (b = 0 -> 0)
      }
      redo = true;
    }
    if (redo) {                                  // W = tilt comb p (W(1) = 1), / sqrt(sum W^2)
      const float w1 = 1.f;
      float sum = w1 * w1;
      for (int n = 2; n <= EXP_SPEC_M; n++) {
        const float v = sp_tilt_[n] * sp_comb_[n];
        spec_w_[n] = v;
        sum += v * v;
      }
      const float in = sp_bg_ / sqrtf(sum);     // sum >= 1 (W(1) = 1); g = 1 -> 1 / sqrt(sum) exactly
      spec_w_[1] = w1 * in;
      for (int n = 2; n <= EXP_SPEC_M; n++) spec_w_[n] *= in;
    }
    p.aw = spec_w_;
    p.top = EXP_SPEC_M;
    p.p0 = SpecShift(EXP_SPEC_K_SHIFT);
  }
  // SPECTRAL series, analytic: S = sum_n w_n h_n v^n (complex), the SAW loop of SeriesSum with the anti-fold edge on
  // the SHIFTED partial frequency (n + s) f0: the caller passes Fe = 3800 - s f0 (s = 0: SeriesSum<kAddSaw>'s Re, bit
  // for bit). IM = false: the Im sum is not accumulated (outi = 0; the caller only needs Re when theta_k = 0) - the
  // running power and Re are the same operations in the same order, so Re is bit-identical (CPU, 2026-11-05).
  template <bool IM>
  __attribute__((always_inline)) static inline void SpecSeries(float qr, float qi, float f0, float if0, float Fe,
                                                               const float* __restrict w, int top, float& outr,
                                                               float& outi) {
    using namespace explorer_k;
    constexpr float ie = 1.f / EXP_HARM_FEDGE;
    float sr = w[1] * qr, si = IM ? w[1] * qi : 0.f;
    float pr = qr, pi = qi;
    float mf = f0;
    int m = 2;
    const int ms = SafeTop(Fe, if0, top);
    for (; m <= ms; m++) {                       // h = 1 exactly
      const float t = pr * qr - pi * qi;
      pi = pr * qi + pi * qr;
      pr = t;
      mf += f0;
      sr += w[m] * pr;
      if (IM) si += w[m] * pi;
    }
    for (; m <= top; m++) {
      const float t = pr * qr - pi * qi;
      pi = pr * qi + pi * qr;
      pr = t;
      mf += f0;
      float h = (Fe - mf) * ie;
      if (h <= 0.f) break;
      h = fminf(h, 1.f);
      sr += w[m] * h * pr;
      if (IM) si += w[m] * h * pi;
    }
    outr = sr; outi = si;
  }
  // Complex power v^E (E constant >= 1, binary: squares + multiplies, unrolled at compile time).
  template <int E>
  __attribute__((always_inline)) static inline void SpecPow(float qr, float qi, float& rr, float& ri) {
    float br = qr, bi = qi, ar = 1.f, ai = 0.f;
    bool first = true;
#pragma GCC unroll 8
    for (int e = E; e; e >>= 1) {
      if (e & 1) {
        if (first) { ar = br; ai = bi; first = false; }
        else { const float t = ar * br - ai * bi; ai = ar * bi + ai * br; ar = t; }
      }
      if (e >> 1) { const float t = br * br - bi * bi; bi = 2.f * br * bi; br = t; }
    }
    rr = ar; ri = ai;
  }
  // SPECTRAL bank series (non-HARM banks): S = sum_i w_i h_i v^{m_i}, m = EXP_SPEC_BANK[b] ascending, m_1 = 1 (no edge).
  // Edge as SpecSeries on the member's real frequency m_i f0 against Fe; the first member with m_i f0 >= Fe ends the
  // loop (later members are higher - skipped, not computed). CPU (2026-11-05, HARM structure; rounding differs from
  // before, < 1e-5 of peak): the step power(s) once per band, then ONE complex multiply per member:
  //   constant stride D (ODD, STRIDE3..7): step v^D, p_i = p_{i-1} v^D;
  //   FIB: p_i = p_{i-1} p_{i-2} (p_0 = p_1 = v);
  //   general (PRIMES): v^g for the bank's few distinct gaps g once, p_i = p_{i-1} v^{gap_i}.
  // As SafeTop: members with m_i <= (Fe - FEDGE - 1 Hz) / f0 run without the edge maths (h = 1 exactly); the rest
  // get the clamp, stopping at the first member past the edge. IM as SpecSeries.
  template <bool IM, int BK>
  __attribute__((always_inline)) static inline void SpecBankSeries(float qr, float qi, float f0, float if0, float Fe,
                                                                   const float* __restrict w, float& outr,
                                                                   float& outi) {
    using namespace explorer_k;
    constexpr ExpSpecBankTab T = ExpSpecBankTab(BK);
    static_assert(T.ng <= 8, "SPECTRAL bank: too many distinct gaps");
    const float* __restrict mfl = kExpSpecBankTab<BK>.mf;
    constexpr float ie = 1.f / EXP_HARM_FEDGE;
    constexpr int M = EXP_SPEC_M;
    float sr = w[1] * qr, si = IM ? w[1] * qi : 0.f;
    // safe members: m_i <= x (h = 1 exactly; 1 Hz margin as SafeTop)
    const float x = fminf((Fe - (EXP_HARM_FEDGE + 1.f)) * if0, mfl[M]);
    int ms = 1;
    if (T.stride > 0) {
      if (x >= 1.f + (float)T.stride) ms = 1 + (int)((x - 1.f) * (1.f / (float)(T.stride > 0 ? T.stride : 1)));
      if (ms > M) ms = M;
    } else {                                     // largest i with m_i <= x (m ascending): 4-step binary search
#pragma GCC unroll 4
      for (int st = 8; st; st >>= 1)
        if (ms + st <= M && mfl[ms + st] <= x) ms += st;
    }
    int i = 2;
    if (T.stride > 0) {
      float dr, di;
      SpecPow<(T.stride > 0 ? T.stride : 1)>(qr, qi, dr, di);
      float pr = qr, pi = qi;
      for (; i <= ms; i++) {                     // h = 1 exactly
        const float t = pr * dr - pi * di;
        pi = pr * di + pi * dr;
        pr = t;
        sr += w[i] * pr;
        if (IM) si += w[i] * pi;
      }
      for (; i <= M; i++) {
        float h = (Fe - mfl[i] * f0) * ie;
        if (h <= 0.f) break;
        h = fminf(h, 1.f);
        const float t = pr * dr - pi * di;
        pi = pr * di + pi * dr;
        pr = t;
        sr += w[i] * h * pr;
        if (IM) si += w[i] * h * pi;
      }
    } else if (T.fib) {
      float ar = qr, ai = qi, pr = qr, pi = qi;  // p_{i-2}, p_{i-1}
      for (; i <= ms; i++) {
        const float t = pr * ar - pi * ai;
        const float u = pr * ai + pi * ar;
        ar = pr; ai = pi; pr = t; pi = u;
        sr += w[i] * pr;
        if (IM) si += w[i] * pi;
      }
      for (; i <= M; i++) {
        float h = (Fe - mfl[i] * f0) * ie;
        if (h <= 0.f) break;
        h = fminf(h, 1.f);
        const float t = pr * ar - pi * ai;
        const float u = pr * ai + pi * ar;
        ar = pr; ai = pi; pr = t; pi = u;
        sr += w[i] * h * pr;
        if (IM) si += w[i] * h * pi;
      }
    } else {
      constexpr int NG = T.ng > 0 ? T.ng : 1;
      float gr[NG], gi[NG];
#pragma GCC unroll 8
      for (int c = 0; c < NG; c++) SpecPowRt(qr, qi, T.gap[c], gr[c], gi[c]);   // gaps constant after unrolling
      const int* __restrict cl = kExpSpecBankTab<BK>.cls;
      float pr = qr, pi = qi;
      // safe members unrolled (the gap class per member is a constant: v^g stays in registers, no table loads)
#pragma GCC unroll 16
      for (int j = 2; j <= M; j++) {
        if (j > ms) break;
        const int c = T.cls[j];
        const float t = pr * gr[c] - pi * gi[c];
        pi = pr * gi[c] + pi * gr[c];
        pr = t;
        sr += w[j] * pr;
        if (IM) si += w[j] * pi;
        i = j + 1;
      }
      for (; i <= M; i++) {
        float h = (Fe - mfl[i] * f0) * ie;
        if (h <= 0.f) break;
        h = fminf(h, 1.f);
        const int c = cl[i];
        const float t = pr * gr[c] - pi * gi[c];
        pi = pr * gi[c] + pi * gr[c];
        pr = t;
        sr += w[i] * h * pr;
        if (IM) si += w[i] * h * pi;
      }
    }
    outr = sr; outi = si;
  }
  // v^g, g >= 1 known at compile time after unrolling (binary).
  __attribute__((always_inline)) static inline void SpecPowRt(float qr, float qi, int g, float& rr, float& ri) {
    float br = qr, bi = qi, ar = 1.f, ai = 0.f;
    bool first = true;
#pragma GCC unroll 8
    for (int e = g; e; e >>= 1) {
      if (e & 1) {
        if (first) { ar = br; ai = bi; first = false; }
        else { const float t = ar * br - ai * bi; ai = ar * bi + ai * br; ar = t; }
      }
      if (e >> 1) { const float t = br * br - bi * bi; bi = 2.f * br * bi; br = t; }
    }
    rr = ar; ri = ai;
  }
  // FM LAB SPECTRAL: sum_k w_k a_k Re(S_k e^{j 2 pi theta_k}), S_k = SpecSeries on the slot's unit phasor (no FM),
  // theta_k (per slot / band, explorer_spec_st) advancing s f'_k / sr8 per sample (K4 shift, wrapped to [0, 1)).
  // CPU (2026-11-05): one band loop per bank (template; the bank switch is once per sample), Im only for bands with
  // theta_k != 0 (theta_k = 0 -> W = Re S_k, as before).
  float ShapeSpec(int o, const Params& p) {
    using namespace explorer_k;
    switch (spec_bank_) {
#define EXP_SPEC_CASE(B) case B: return ShapeSpecB<B>(o, p);
      EXP_SPEC_CASE(0) EXP_SPEC_CASE(1) EXP_SPEC_CASE(2) EXP_SPEC_CASE(3) EXP_SPEC_CASE(4) EXP_SPEC_CASE(5)
      EXP_SPEC_CASE(6) EXP_SPEC_CASE(7) EXP_SPEC_CASE(8)
#undef EXP_SPEC_CASE
      default: return 0.f;
    }
  }
  template <int BK>
  float ShapeSpecB(int o, const Params& p) {
    using namespace explorer_k;
    static_assert(EXP_SPEC_BANKS == 9, "ShapeSpec dispatch lists 9 banks");
    constexpr bool harm = (BK == EXP_SPEC_BANK_HARM);
    const float* __restrict mg  = mag_ + 1;
    const float* __restrict ur  = ur_;
    const float* __restrict ui  = ui_;
    const float* __restrict fp  = fp_[o];
    const float* __restrict ifp = ifp_[o];
    const float* __restrict w   = p.aw ? p.aw : spec_w_;
    float* __restrict th = explorer_spec_st.th[o];
    const float s = p.p0;
    const float ds = s * sp_isr8_;
    const bool played = (o == kSP);
    const bool sub = (k5s_ > EXP_SPEC_SUB_ON);    // K5 SUB; full CCW = skipped entirely (bit-identical to no sub)
    const float gsub = EXP_SPEC_SUB_MAX * k5s_;
    if (!sub && explorer_spec_st.subh[o]) SpecResetSub(o);   // history cleared once when it goes off
    if (sub) explorer_spec_st.subh[o] = true;
    float acc = 0.f;
    for (int k = 0; k < kBands; k++) {
      const float t0 = th[k];
      if (s != 0.f) {                            // advance (cycles), clean wrap to [0, 1)
        float t = t0 + ds * fp[k];
        t -= floorf(t);
        th[k] = t;
      }
      const float a = mg[k];
      if (a == 0.f) {
        if (sub) explorer_spec_st.ur[o][k] = explorer_spec_st.ui[o][k] = 0.f;   // silent band: sub history cleared
        continue;
      }
      const float Fe = EXP_HARM_FMAX - s * fp[k];
      float W, Sr, Si;
      if (t0 == 0.f) {
        if (harm) SpecSeries<false>(ur[k], ui[k], fp[k], ifp[k], Fe, w, EXP_SPEC_M, Sr, Si);
        else      SpecBankSeries<false, BK>(ur[k], ui[k], fp[k], ifp[k], Fe, w, Sr, Si);
        W = Sr;
      } else {
        if (harm) SpecSeries<true>(ur[k], ui[k], fp[k], ifp[k], Fe, w, EXP_SPEC_M, Sr, Si);
        else      SpecBankSeries<true, BK>(ur[k], ui[k], fp[k], ifp[k], Fe, w, Sr, Si);
        W = Sr * Cos01(t0) - Si * Sin01(t0);
      }
      if (sub) {                                 // K5 SUB: octave divider u (u^2 = v), sign continued = flip per wrap of v
        const float vr = ur[k];
        float c = sqrtf(fmaxf(0.5f + 0.5f * vr, 0.f));
        float sn = sqrtf(fmaxf(0.5f - 0.5f * vr, 0.f));
        if (ui[k] < 0.f) sn = -sn;
        float& pr = explorer_spec_st.ur[o][k];
        float& pi = explorer_spec_st.ui[o][k];
        if (c * pr + sn * pi < 0.f) { c = -c; sn = -sn; }
        pr = c; pi = sn;
        W += gsub * c;                           // Re(u) x g_sub, after the norm, before the band amplitude
      }
      acc += (played ? wmid_[k] : 1.f) * a * W;
    }
    return acc;
  }
  void SpecResetSub(int o) {
    for (int k = 0; k < kBands; k++) explorer_spec_st.ur[o][k] = explorer_spec_st.ui[o][k] = 0.f;
    explorer_spec_st.subh[o] = false;
  }
  void SpecResetTheta(int o) {
    for (int k = 0; k < kBands; k++) explorer_spec_st.th[o][k] = 0.f;
  }
  // Index cap, 2OP Carson form with the carrier at c f' and the modulator at r f' (c f' + r f' (I + 1) <= L):
  // I <= (L / f' - c) / r - 1, floor 0 (ifp = 1 / f', ir = 1 / r; c = 1 = kFM's cap). With EXP_FM_ENV_INDEX_FLOOR > 0
  // the envelope closes it only down to that floor (or the K3 knob's own cap, Lk), as kFM.
  static inline float IndexCap(float L, float Lk, float ifp, float ir, float c) {
    using namespace explorer_k;
    float ic = fmaxf((L * ifp - c) * ir - 1.f, 0.f);         // floor 0 (vmaxnm: same value, no flag stall)
    if (EXP_FM_ENV_INDEX_FLOOR > 0.f) {
      float ick = (Lk * ifp - c) * ir - 1.f;
      if (ick < 0.f) ick = 0.f;
      const float fl = (EXP_FM_ENV_INDEX_FLOOR < ick) ? EXP_FM_ENV_INDEX_FLOOR : ick;
      if (ic < fl) ic = fl;
    }
    return ic;
  }
  // --- FM LAB ADDITIVE helpers (SW1 DOWN, model kLADD; see FM LAB SW1 DOWN in the header) ---
  static inline bool AddIsUni(int z) {
    using namespace explorer_k;
    if (z < 0 || z >= EXP_ADD_N_ZONES) return false;
    const int c = EXP_ADD_ZONES[z].ctl;
    return c == kAddUniI || c == kAddUniH;
  }
  // Zone zo -> zone zn inside kLADD (the items share explorer_add_st): the entering zone's own state starts fresh -
  // unison theta at EXP_ADD_UNI_PHASE (from a non-unison zone), SUB half phasor without history, STRETCH phases fresh.
  void AddEnterZone(int zn, int zo) {
    using namespace explorer_k;
    if (!lab_ || zn < 0 || zn >= EXP_ADD_N_ZONES || zo < 0 || zo >= EXP_ADD_N_ZONES) return;
    const int sn = EXP_ADD_ZONES[zn].series, so = EXP_ADD_ZONES[zo].series;
    const bool th = AddIsUni(zn) && !AddIsUni(zo);
    const bool wh = (sn == kAddSub) && (so != kAddSub);
    const bool st = (sn == kAddStr) && (so != kAddStr);
    ExplorerAddState& A = explorer_add_st;
    for (int o = 0; o < kSlots; o++)
      for (int k = 0; k < kBands; k++) {
        if (th) A.th[o][k] = EXP_ADD_UNI_PHASE;
        if (wh) A.wr[o][k] = A.wi[o][k] = 0.f;
        if (st) A.slp[o][k] = -1.f;
      }
  }
  // Series weights into cache c (0 = the current side pn_, 1 = the fading side pf_), recomputed only when the series,
  // h (p7) or delta (p5) moved:
  //   SAW / ODD / OCT: members m <= 16 (ODD odd m, OCT powers of 2), g_m = clamp(M_eff - m + 1, 0, 1),
  //     M_eff = 1 + h (M_MAX - 1); SUB: k <= 32, g_k = clamp(2 M_eff - k + 1, 0, 1); w = g m^-s, s = 1 - h (1 - S_MIN)
  //   STRETCH: w_m = 1 / m (m = 1..STR_M), rho_m = m^(1 + delta)
  //   then w /= sqrt(sum w^2) (the series' RMS = one unit sine). p.aw / p.ar / p.top point at the cache.
  void AddWeights(Params& p, int c) {
    using namespace explorer_k;
    const float h = p.p7, d = p.p5;
    const int ser = p.ser;
    if (ser != add_ks_[c] || h != add_kh_[c] || d != add_kd_[c]) {
      add_ks_[c] = ser; add_kh_[c] = h; add_kd_[c] = d;
      float* w = add_w_[c];
      for (int m = 0; m <= EXP_ADD_K_MAX; m++) w[m] = 0.f;
      int top = 1;
      if (ser == kAddStr) {
        float* r = add_rho_[c];
        r[0] = 0.f;
        for (int m = 1; m <= EXP_ADD_STR_M; m++) {
          w[m] = 1.f / (float)m;
          r[m] = (float)m * expf(d * add_ln_[m]);   // m^(1 + delta)
        }
        top = EXP_ADD_STR_M;
      } else {
        const float me = 1.f + h * (float)(EXP_ADD_M_MAX - 1);
        const float sx = 1.f - h * (1.f - EXP_ADD_S_MIN);
        const bool sub = (ser == kAddSub);
        const int kmax = sub ? EXP_ADD_K_MAX : EXP_ADD_M_MAX;
        const float cnt = sub ? 2.f * me : me;
        for (int m = 1; m <= kmax; m++) {
          if (ser == kAddOdd && !(m & 1)) continue;
          if (ser == kAddOct && (m & (m - 1))) continue;
          float g = cnt - (float)m + 1.f;
          if (g <= 0.f) break;
          if (g > 1.f) g = 1.f;
          w[m] = g * expf(-sx * add_ln_[m]);
          top = m;
        }
      }
      float e = 0.f;
      for (int m = 1; m <= top; m++) e += w[m] * w[m];
      const float in = (e > 0.f) ? 1.f / sqrtf(e) : 0.f;
      for (int m = 1; m <= top; m++) w[m] *= in;
      add_top_[c] = top;
    }
    p.aw = add_w_[c];
    p.ar = add_rho_[c];
    p.top = add_top_[c];
  }
  // DIAG: K2 parameter at the knob (a) and with the K5 LFO (b; UNISON: the beat rate in Hz, mode in diag_add_bm_),
  // K3 parameter at the knob (diag_add_c3_). h / u / delta / index in their own units (index rad, before the cap).
  void AddDiagParam(int z, float k2, float k3, float k5, float& a, float& b) {
    using namespace explorer_k;
    const int c = EXP_ADD_ZONES[(z >= 0 && z < EXP_ADD_N_ZONES) ? z : 0].ctl;
    const float kl = Clamp01(k2 + lfo_s_ * EXP_ADD_LFO_SWING);
    const float i3 = EXP_ADD_FM_I_MAX * k3 * k3;
    diag_add_bm_ = 0;
    switch (c) {
      case kAddA: a = k2; b = kl; diag_add_c3_ = i3; break;
      case kAddB: a = EXP_ADD_FM_I_MAX * k2 * k2; b = EXP_ADD_FM_I_MAX * kl * kl; diag_add_c3_ = k3; break;
      case kAddStrC: a = EXP_ADD_STR_MAX * k2; b = EXP_ADD_STR_MAX * kl; diag_add_c3_ = i3; break;
      default: {                                 // UNISON: u, beat rate (Hz) + mode
        const float t5 = 2.f * k5 - 1.f;
        a = k2;
        b = (t5 == 0.f) ? 0.f : EXP_ADD_BEAT_MIN * expf(fabsf(t5) * logf(EXP_ADD_BEAT_MAX / EXP_ADD_BEAT_MIN));
        diag_add_bm_ = (t5 < 0.f) ? 1 : (t5 > 0.f) ? 2 : 0;
        diag_add_c3_ = (c == kAddUniH) ? k3 : i3;
        break;
      }
    }
  }
  // Series sum of one band: sum_m w_m h_mk Re(q^m) over the series' members (m = 1 always kept, no edge), h_mk =
  // clamp((Fe - m f0) / FEDGE, 0, 1) (monotone in m: the loop stops at the first 0). SAW / SUB step q (SUB: w indexes
  // k, f0 = f' / 2), ODD steps q^2, OCT squares.
  //
  // 2026-11-02 (CPU, same output bit for bit): members with m f0 <= Fe - FEDGE - 1 Hz (m <= SafeTop) have h_mk >= 1.002
  // whatever the float rounding of the running m f0 (mf: error < 0.01 Hz; SafeTop's (Fe - 401) / f0: < 0.1 Hz), so the
  // clamp gives exactly 1 and w_m 1 = w_m: those run without the edge arithmetic / compares (mf still advances, so
  // the edge members see the same mf as before). Edge members: the original loop, unchanged.
  static inline int SafeTop(float Fe, float if0, int top) {
    const float x = (Fe - (explorer_k::EXP_HARM_FEDGE + 1.f)) * if0;
    if (!(x >= 1.f)) return 1;                   // no member above the fundamental is safe
    return (x >= (float)top) ? top : (int)x;
  }
  template <int S>
  static inline float SeriesSum(float qr, float qi, float f0, float if0, float Fe, const float* __restrict w,
                                int top) {
    using namespace explorer_k;
    constexpr float ie = 1.f / EXP_HARM_FEDGE;
    float sum = w[1] * qr;
    float pr = qr, pi = qi;
    if (S == kAddOct) {
      for (int m = 2; m <= top; m <<= 1) {
        const float t = pr * pr - pi * pi;
        pi = 2.f * pr * pi;
        pr = t;
        float h = (Fe - (float)m * f0) * ie;
        if (h <= 0.f) break;
        if (h > 1.f) h = 1.f;
        sum += w[m] * h * pr;
      }
      return sum;
    }
    constexpr int dm = (S == kAddOdd) ? 2 : 1;
    float sr = qr, si = qi;
    if (S == kAddOdd) { sr = qr * qr - qi * qi; si = 2.f * qr * qi; }
    const float df = (float)dm * f0;
    float mf = f0;
    int m = 1 + dm;
    const int ms = SafeTop(Fe, if0, top);
    for (; m <= ms; m += dm) {                   // h_mk = 1 exactly
      const float t = pr * sr - pi * si;
      pi = pr * si + pi * sr;
      pr = t;
      mf += df;
      sum += w[m] * pr;
    }
    for (; m <= top; m += dm) {
      const float t = pr * sr - pi * si;
      pi = pr * si + pi * sr;
      pr = t;
      mf += df;
      float h = (Fe - mf) * ie;
      if (h <= 0.f) break;
      h = fminf(h, 1.f);
      sum += w[m] * h * pr;
    }
    return sum;
  }
  // UNISON (SAW / ODD): both copies' series sums in ONE pass (2026-11-02, CPU): s1 = SeriesSum(q), s2 = SeriesSum(q2)
  // with the same per-copy operations and order as two separate calls (bit for bit), sharing m f0, the edge h_mk and
  // the loop; two independent phasor chains interleave in the pipeline.
  template <int S>
  static inline void SeriesSum2(float qr, float qi, float q2r, float q2i, float f0, float if0, float Fe,
                                const float* __restrict w, int top, float& s1, float& s2) {
    using namespace explorer_k;
    constexpr float ie = 1.f / EXP_HARM_FEDGE;
    float sum = w[1] * qr, sum2 = w[1] * q2r;
    float pr = qr, pi = qi, p2r = q2r, p2i = q2i;
    constexpr int dm = (S == kAddOdd) ? 2 : 1;
    float sr = qr, si = qi, s2r = q2r, s2i = q2i;
    if (S == kAddOdd) {
      sr = qr * qr - qi * qi; si = 2.f * qr * qi;
      s2r = q2r * q2r - q2i * q2i; s2i = 2.f * q2r * q2i;
    }
    const float df = (float)dm * f0;
    float mf = f0;
    int m = 1 + dm;
    const int ms = SafeTop(Fe, if0, top);
    for (; m <= ms; m += dm) {                   // h_mk = 1 exactly
      const float t = pr * sr - pi * si;
      pi = pr * si + pi * sr;
      pr = t;
      const float t2 = p2r * s2r - p2i * s2i;
      p2i = p2r * s2i + p2i * s2r;
      p2r = t2;
      mf += df;
      sum += w[m] * pr;
      sum2 += w[m] * p2r;
    }
    for (; m <= top; m += dm) {
      const float t = pr * sr - pi * si;
      pi = pr * si + pi * sr;
      pr = t;
      const float t2 = p2r * s2r - p2i * s2i;
      p2i = p2r * s2i + p2i * s2r;
      p2r = t2;
      mf += df;
      float h = (Fe - mf) * ie;
      if (h <= 0.f) break;
      h = fminf(h, 1.f);
      sum += w[m] * h * pr;
      sum2 += w[m] * h * p2r;
    }
    s1 = sum;
    s2 = sum2;
  }
  // STRETCH band sum: harmonic m at rho_m f0, own phase p_m (fresh: rho_m phi, else + rho_m d, d = the wrapped phi
  // step; every accumulator advances, also above the edge), out sum_m w_m h_mk cos(2 pi p_m + rho_m I x) (ix = I x),
  // h_mk on rho_m f0 (m >= 2).
  inline float StretchSum(int o, int k, float ph, float f0, float ix, float Fe, const float* __restrict w,
                          const float* __restrict rho) {
    using namespace explorer_k;
    constexpr float ie = 1.f / EXP_HARM_FEDGE;
    ExplorerAddState& A = explorer_add_st;
    float& lp = A.slp[o][k];
    const bool fresh = (lp < 0.f);
    float d = 0.f;
    if (!fresh) {
      d = ph - lp;
      if (d < -0.5f) d += 1.f;
      else if (d >= 0.5f) d -= 1.f;
    }
    lp = ph;
    float sum = 0.f;
    for (int m = 1; m <= EXP_ADD_STR_M; m++) {
      const float r = rho[m];
      float pm = fresh ? r * ph : A.sp[m - 1][o][k] + r * d;
      pm -= floorf(pm);
      A.sp[m - 1][o][k] = pm;
      float h = 1.f;
      if (m > 1) {
        h = (Fe - r * f0) * ie;
        if (h <= 0.f) continue;
        if (h > 1.f) h = 1.f;
      }
      float arg = pm;                            // cos(2 pi p_m + rho_m I x) = Cos01(frac(p_m + rho_m I x / 2 pi))
      if (ix != 0.f) {
        arg += r * ix * 0.159154943f;
        arg -= floorf(arg);
      }
      sum += w[m] * h * Cos01(arg);
    }
    return sum;
  }
  // FM LAB ADDITIVE: sum_k w_k a_k W_k (series S; U = unison). Per band: base b = v (SUB: the continued half phasor
  // w, f0 = f' / 2), I = min(p0, IndexCap at r 1 with L_c), q = b e^{j I Im b}, W = SeriesSum (UNISON: + u x the
  // same on q e^{j 2 pi theta}, x 1 / sqrt(1 + u^2); STRETCH: StretchSum).
  float ShapeAdd(int o, const Params& p) {
    using namespace explorer_k;
    const bool uni = (p.ctl == kAddUniI || p.ctl == kAddUniH);
    switch (p.ser) {
      case kAddSaw: return uni ? ShapeAddT<kAddSaw, true>(o, p) : ShapeAddT<kAddSaw, false>(o, p);
      case kAddOdd: return uni ? ShapeAddT<kAddOdd, true>(o, p) : ShapeAddT<kAddOdd, false>(o, p);
      case kAddOct: return ShapeAddT<kAddOct, false>(o, p);
      case kAddSub: return ShapeAddT<kAddSub, false>(o, p);
      default:      return ShapeAddT<kAddStr, false>(o, p);
    }
  }
  template <int S, bool U>
  float ShapeAddT(int o, const Params& p) {
    using namespace explorer_k;
    static_assert(EXP_ADD_FM_I_MAX < 90.f, "SinCos offset covers the index");
    const float* __restrict mg  = mag_ + 1;
    const float* __restrict ur  = ur_;
    const float* __restrict ui  = ui_;
    const float* __restrict fp  = fp_[o];
    const float* __restrict ifp = ifp_[o];
    const float* __restrict phs = ph_;
    const bool played = (o == kSP);
    const float L  = hc_fold_;
    const float Lc = p.capk ? hc_fold_k_ : L;                 // index cap: K3 knob alone (B) or incl. the envelope
    const float Fe = (p.fel && L < EXP_HARM_FMAX) ? L : EXP_HARM_FMAX;   // anti-fold edge
    const float I0 = p.p0;
    const float* __restrict w = p.aw;
    const int top = p.top;
    ExplorerAddState& A = explorer_add_st;
    float acc = 0.f;
    for (int k = 0; k < kBands; k++) {
      const float a = mg[k];
      if (a == 0.f) {                            // silent band: SUB / STRETCH history cleared (theta runs on later)
        if (S == kAddSub) { A.wr[o][k] = 0.f; A.wi[o][k] = 0.f; }
        if (S == kAddStr) A.slp[o][k] = -1.f;
        continue;
      }
      float br = ur[k], bi = ui[k];
      float f0 = fp[k], if0 = ifp[k];
      if (S == kAddSub) {                        // half-angle phasor w (w^2 = v), sign continued
        const float vr = br;
        float c = sqrtf(fmaxf(0.5f + 0.5f * vr, 0.f));
        float s = sqrtf(fmaxf(0.5f - 0.5f * vr, 0.f));
        if (bi < 0.f) s = -s;
        float& pr = A.wr[o][k];
        float& pi = A.wi[o][k];
        if (c * pr + s * pi < 0.f) { c = -c; s = -s; }
        pr = c; pi = s;
        br = c; bi = s;
        f0 *= 0.5f; if0 *= 2.f;
      }
      float I = I0;
      if (EXP_ANTIFOLD && I > 0.f) {
        const float ic = IndexCap(Lc, Lc, if0, 1.f, 1.f);   // I <= L_c / f0 - 2
        if (I > ic) I = ic;
      }
      const float ix = I * bi;                   // modulator at ratio 1: sin of the series fundamental
      float W;
      if (S == kAddStr) {
        W = StretchSum(o, k, phs[k], f0, ix, Fe, w, p.ar);
      } else {
        float qr = br, qi = bi;
        if (ix != 0.f) {
          float sn, cs;
          SinCos(ix, sn, cs);
          qr = br * cs - bi * sn;
          qi = br * sn + bi * cs;
        }
        if (U) {
          float& th = A.th[o][k];
          if (p.adv) {
            float t = th + p.p3 + p.p4 * f0;
            t -= floorf(t);
            th = t;
          }
          if (p.p1 > 0.f) {                      // both copies in one pass (SeriesSum2 = the two former calls)
            float c, s;
            CosSin01(th, c, s);
            const float q2r = qr * c - qi * s, q2i = qr * s + qi * c;
            float s1, s2;
            SeriesSum2<S>(qr, qi, q2r, q2i, f0, if0, Fe, w, top, s1, s2);
            W = (s1 + p.p1 * s2) * p.p2;
          } else {
            W = SeriesSum<S>(qr, qi, f0, if0, Fe, w, top);
          }
        } else {
          W = SeriesSum<S>(qr, qi, f0, if0, Fe, w, top);
        }
      }
      acc += (played ? wmid_[k] : 1.f) * a * W;
    }
    return acc;
  }

  // K3 dampener UP (FM caps), per 8 kHz sample after EnvUpdate (nothing when its inputs did not move; 2026-10-28:
  //   was per audio block from the block's first o3_ -> the index stepped at 1 ms block edges), from the smoothed K3,
  //   the K3 envelope value o3_ and (ONE-SHOT) the K5 LFO lfo_s_:
  //     LOOPING:  ln(fc / DAMP_FMIN) = k3^DAMP_CURVE ln(DAMP_FMAX / DAMP_FMIN) + o3_ ln(DAMP_FMAX / DAMP_FMIN) (octave
  //               offset); ONE-SHOT: ln(fc / DAMP_FMIN) = o3_ k3^DAMP_CURVE ln(DAMP_FMAX / DAMP_FMIN) (o3_ = the K4
  //               touch env openness o: full travel, bottom 150 Hz .. the K3 value);
  //     clamped to [0, ln(FMAX / FMIN)] (exactly DAMP_FMAX / DAMP_FMIN at the ends); hc_fold_ = min(FOLD_LIMIT_HZ, fc)
  //     is the L the UP models' caps read (ShapeK). hc_fold_k_ (K3 knob alone) is recomputed only when k3 moved.
  void DampFmUpdate() {
    using namespace explorer_k;
    const float k = k3s_, om = o3_;
    // SW3 MID only (not FM LAB): the K5 LFO also swings the FM cutoff, UPWARD ONLY on top of the touch env
    // (2026-11-05): + depth (shape + 1) / 2 in [0, depth] x EXP_LFO_FM_FC_OCT octaves; LFO off (depth 0) -> exactly 0.
    // SW3 MID only (2026-11-05, octaves on the FM cutoff): swell side +- EXP_LFO_FM_FC_OCT around the envelope (l),
    // faded in with the swell (x o_r); pluck side UPWARD 0 .. EXP_LFO_FM_FC_OCT_DEC x depth, growing as the pluck
    // closes (x (1 - o_d)); blended by w.
    //   2026-11-05: the pluck side's amount also scales with the K3 cutoff's position in its range (log, 0 at
    //   DAMP_FMIN .. 1 at DAMP_FMAX = DampCurve(k3)): dark K3 -> subtle, bright K3 -> up to EXP_LFO_FM_FC_OCT_DEC.
    float lo = 0.f, ld = 0.f;                    // swell part (octaves) / pluck part before the K3 scale
    if (!loop_ && !lab_) {
      float up = 0.5f * (lfo_depth_ + lfo_s_); if (up < 0.f) up = 0.f;
      lo = te_w_ * te_or_ * lfo_s_ * EXP_LFO_FM_FC_OCT;
      ld = (1.f - te_w_) * up * EXP_LFO_FM_FC_OCT_DEC * (1.f - te_od_);
    }
    if (k != damp_k_ || om != damp_o_ || lo != damp_l_ || ld != damp_ld_) {
      const bool kmoved = (k != damp_k_);
      damp_k_ = k;
      damp_o_ = om;
      damp_l_ = lo;
      damp_ld_ = ld;
      const float kc = (k >= 1.f) ? 1.f : DampCurve(k);   // K3 position in its range (log)
      const float lk = kc * damp_lnr_;
      float lf = lk;
      if (loop_) lf += om * damp_lnr_;
      else       lf = lf * om + (lo + ld * kc) * 0.6931472f;   // + the LFO octaves (swell: +-, pluck: upward x K3 position)
      damp_fc_ = (lf >= damp_lnr_) ? EXP_DAMP_FMAX : (lf <= 0.f) ? EXP_DAMP_FMIN : EXP_DAMP_FMIN * expf(lf);
      hc_fold_ = (damp_fc_ < EXP_FOLD_LIMIT_HZ) ? damp_fc_ : EXP_FOLD_LIMIT_HZ;
      // K3 knob value alone (no envelope): the envelope's index floor uses it (MID)
      if (loop_) hc_fold_k_ = hc_fold_;
      else if (kmoved) {
        const float fk = (lk >= damp_lnr_) ? EXP_DAMP_FMAX : (lk <= 0.f) ? EXP_DAMP_FMIN : EXP_DAMP_FMIN * expf(lk);
        hc_fold_k_ = (fk < EXP_FOLD_LIMIT_HZ) ? fk : EXP_FOLD_LIMIT_HZ;
      }
    }
  }
  // FM LAB 2-OP (lab_ only) caps' L, per 8 kHz sample after EnvUpdate (nothing when u_eff did not move): u_eff =
  //   o |2 k3s - 1| (o = the K4 touch env openness; LOOPING: 1), fc = LabFc(u_eff) = SW3 MID's law with k3 -> u_eff, lab_L_ = min(FOLD_LIMIT_HZ, fc).
  //   No envelope floor, no LFO on L (K5 acts on b only). SW3 MID's DampFmUpdate state is not touched.
  void LabFmUpdate() {
    using namespace explorer_k;
    float ue = (loop_ ? 1.f : te_o_) * fabsf(2.f * k3s_ - 1.f);
    if (ue > 1.f) ue = 1.f;
    if (ue != lab_ue_c_) {
      lab_ue_c_ = ue;
      const float fc = LabFc(ue);
      lab_L_ = (fc < EXP_FOLD_LIMIT_HZ) ? fc : EXP_FOLD_LIMIT_HZ;
    }
  }
  // K3 dampener, table part, once per audio block:
  //   MID (table / bit-table picks), from the smoothed K3 ONLY (not modulated; 2026-10-28 late, back as before the
  //     ladder): tpk_fc_ = DampFc(k3) (sqrt law); the picks follow (Repick) only for the SW2 slots running or about
  //     to run (og_ / og_t_ != 0); a slot re-picks when it starts (pick_fc_). Runs on every family (the picks are
  //     read only on MID).
  void DampUpdate() {
    const float k = k3s_;
    if (k != tpk_k_) {                           // tables: the knob value only
      tpk_k_ = k;
      tpk_fc_ = explorer_k::EXP_TB_DAMP_ENABLE ? DampFc(k) : explorer_k::EXP_DAMP_FMAX;   // off: static open picks
    }
    bool any = false, run[kSlots];
    for (int o = 0; o < kSlots; o++) {           // og index = slot index (kSU, kSP, kSD)
      run[o] = (og_[o] != 0.f || og_t_[o] != 0.f) && pick_fc_[o] != tpk_fc_;
      any = any || run[o];
    }
    if (!any) return;                            // unchanged: picks hold
    bool moved = false;
    for (int o = 0; o < kSlots; o++) {
      if (run[o]) { moved = Repick(o) || moved; pick_fc_[o] = tpk_fc_; }
      else {                                     // idle slot: old = new (no stale crossfade in the new slide)
        for (int b = 0; b < kBands; b++) { wtlvf_[o][b] = wtlv_[o][b]; btsf_[o][b] = btsc_[o][b]; }
      }
    }
    if (moved) pk_fade_n_ = pk_fade_len_;
    else       pk_fade_n_ = 0;
  }

  // SW1 MID family (tables; parked bit table): the K3 2-pole LP (parked: ladder) runs there.
  static inline bool IsTab(int m) { return m == kWT || m == kBIT; }
  // Ladder cutoff (Hz) at k3 (0..1), knob value: LAD_FMIN (LAD_FMAX / LAD_FMIN)^k3, exact ends. DIAG only.
  static float LadFc(float k3) {
    using namespace explorer_k;
    return (k3 >= 1.f) ? EXP_LAD_FMAX : (k3 <= 0.f) ? EXP_LAD_FMIN
                                       : EXP_LAD_FMIN * expf(k3 * logf(EXP_LAD_FMAX / EXP_LAD_FMIN));
  }
  // Per 8 kHz sample (sub = its index in the block): ladder mix target (1 while the current model is a MID table
  // model, else 0) and cutoff, in octaves of log2(LAD_FMAX / LAD_FMIN) above LAD_FMIN (lf, clamped [0, 1]):
  //   ONE-SHOT: lf = o k3 (o3_ = the K4 touch env openness o: between LAD_FMIN and the knob value; 1 = the knob)
  //   LOOPING:  lf = k3 + o3_ (o3_ = the K5-CW envelope offset, knob units = the full span)
  // k3 = smoothed K3. Computed only while the ladder targets on or still fades (lad_m_ > 0); else the last fc holds.
  inline void LadUpdate(int sub) {
    using namespace explorer_k;
    if (sub >= kLadSub) sub = kLadSub - 1;
    const bool tab = IsTab(var_ >> 6);
    lad_tgb_[sub] = tab;
    if (!tab && lad_m_ <= 0.f) { lad_fcb_[sub] = lad_set_fc_; return; }
    const float lf = loop_ ? (k3s_ + o3_) : o3_ * k3s_;
    const float fc = (lf >= 1.f) ? EXP_LAD_FMAX : (lf <= 0.f) ? EXP_LAD_FMIN : EXP_LAD_FMIN * expf(lf * lad_lnr_);
    lad_fcb_[sub] = fc;
    lad_fc_ = fc;
  }
  // The ladder on the 48 kHz wet (after the DC blocker, before the soft clipper): wet = x + m (ladder(x) - x).
  // m slides linearly toward the 8 kHz sample's target (lad_tgb_) by lad_step_ per 48 kHz sample (EXP_LAD_XFADE_MS
  // for 0 <-> 1); starting from m = 0 clears the ladder state; m = 0: not run. Cutoff set once per 8 kHz sample
  // (kFactor 48 kHz samples) from lad_fcb_, only when it changed.
  void Ladder(float* wet, size_t size) {
    float m = lad_m_;
    const int nsub = (int)(size / kFactor);
    bool any = (m > 0.f);
    for (int j = 0; j < nsub && j < kLadSub; j++) any = any || lad_tgb_[j];
    if (!any) return;                            // SW1 UP / DOWN settled: ladder not run
    const float st = lad_step_;
    size_t n = 0;
    for (int j = 0; j < nsub; j++) {
      const int jj = (j < kLadSub) ? j : kLadSub - 1;
      const bool tg = lad_tgb_[jj];
      const float fc = lad_fcb_[jj];
      if (fc != lad_set_fc_) { lad_.SetCutoff(fc); lad_set_fc_ = fc; }
      for (int q = 0; q < kFactor; q++, n++) {
        if (tg) {
          if (m <= 0.f) lad_.Init(sr_);          // starts from m = 0: cleared state (cutoff / drive / res kept)
          m += st; if (m > 1.f) m = 1.f;
        } else if (m > 0.f) {
          m -= st; if (m < 0.f) m = 0.f;
        }
        if (m > 0.f) {
          const float x = wet[n];
          wet[n] = x + m * (explorer_k::EXP_LAD_TRIM * lad_.Process(x) - x);   // fixed post trim: level-matched to the unfiltered wet
        }
      }
    }
    lad_m_ = m;
  }

  // 2-pole lowpass cutoff (Hz) at k3 (0..1), knob value: TF_FMIN (TF_FMAX / TF_FMIN)^k3, exact ends. DIAG only.
  static float TfFc(float k3) {
    using namespace explorer_k;
    return (k3 >= 1.f) ? EXP_TF_FMAX : (k3 <= 0.f) ? EXP_TF_FMIN
                                      : EXP_TF_FMIN * expf(k3 * logf(EXP_TF_FMAX / EXP_TF_FMIN));
  }
  // Per 8 kHz sample (sub = its index in the block): 2-pole LP mix target (1 while the current model is a MID table
  // model, else 0) and cutoff, in fractions of log2(TF_FMAX / TF_FMIN) above TF_FMIN (lf, clamped [0, 1]):
  //   ONE-SHOT: ln fc = ln ENV_BOTTOM + o (ln fc_knob - ln ENV_BOTTOM) (o3_ = the K4 touch env openness o: full travel
  //             between the bottom EXP_TF_ENV_BOTTOM 2^(k3 EXP_TF_ENV_BOTTOM_OCT) and the knob value; below the knob's
  //             own range, so a pluck closes further)
  //   LOOPING:  lf = k3 + o3_ (o3_ = the K5-CW envelope offset, knob units = the full span)
  // k3 = smoothed K3. Computed only while the LP targets on or still fades (tf_m_ > 0); else the last fc holds.
  inline void TfUpdate(int sub) {
    using namespace explorer_k;
    if (sub >= kLadSub) sub = kLadSub - 1;
    const bool tab = IsTab(var_ >> 6);
    tf_tgb_[sub] = tab;
    if (!tab && tf_m_ <= 0.f) { tf_fcb_[sub] = tf_set_fc_; return; }
    float fc;
    if (loop_) {
      const float lf = k3s_ + o3_;
      fc = (lf >= 1.f) ? EXP_TF_FMAX : (lf <= 0.f) ? EXP_TF_FMIN : EXP_TF_FMIN * expf(lf * tf_lnr_);
    } else {                                     // ONE-SHOT: touch env o between the bottom and the knob value (octaves)
      const float lk = (k3s_ >= 1.f) ? tf_lnr_ : (k3s_ <= 0.f) ? 0.f : k3s_ * tf_lnr_;   // ln(fc_knob / TF_FMIN)
      const float lb = tf_lnb_ + k3s_ * (EXP_TF_ENV_BOTTOM_OCT * 0.6931472f);          // bottom rises with K3
      fc = EXP_TF_FMIN * expf(lb + o3_ * (lk - lb));
    }
    tf_fcb_[sub] = fc;
    tf_fc_ = fc;
  }
  // The 2-pole lowpass on the 48 kHz wet (after the DC blocker, before the soft clipper; the ladder's position):
  // one TPT SVF stage (ExpSVF, Q EXP_TF_Q, LP output), no drive / trim: wet = x + m (lp(x) - x). m slides linearly
  // toward the 8 kHz sample's target (tf_tgb_) by lad_step_ per 48 kHz sample (EXP_LAD_XFADE_MS for 0 <-> 1);
  // starting from m = 0 clears the SVF state; m = 0: not run. Coefficients set once per 8 kHz sample (kFactor
  // 48 kHz samples) from tf_fcb_, only when the cutoff changed.
  void TabFilter(float* wet, size_t size) {
    using namespace explorer_k;
    float m = tf_m_;
    const int nsub = (int)(size / kFactor);
    bool any = (m > 0.f);
    for (int j = 0; j < nsub && j < kLadSub; j++) any = any || tf_tgb_[j];
    if (!any) return;                            // SW1 UP / DOWN settled: LP not run
    const float st = lad_step_;
    size_t n = 0;
    for (int j = 0; j < nsub; j++) {
      const int jj = (j < kLadSub) ? j : kLadSub - 1;
      const bool tg = tf_tgb_[jj];
      const float fc = tf_fcb_[jj];
      // Q: SW3 MID with EXP_TF_Q_ON_K5 = K5 (smoothed): EXP_TF_Q_MIN (EXP_TF_Q_MAX / EXP_TF_Q_MIN)^k5, else EXP_TF_Q.
      const float qv = (!loop_ && EXP_TF_Q_ON_K5) ? EXP_TF_Q_MIN * expf(k5s_ * logf(EXP_TF_Q_MAX / EXP_TF_Q_MIN))
                                                  : EXP_TF_Q;
      if (fc != tf_set_fc_ || qv != tf_set_q_) { tf_svf_.Set(fc, qv, sr_); tf_set_fc_ = fc; tf_set_q_ = qv; }
      for (int q = 0; q < kFactor; q++, n++) {
        if (tg) {
          if (m <= 0.f) tf_svf_.Reset();         // starts from m = 0: cleared state (coefficients kept)
          m += st; if (m > 1.f) m = 1.f;
        } else if (m > 0.f) {
          m -= st; if (m < 0.f) m = 0.f;
        }
        if (m > 0.f) {
          const float x = wet[n];
          float lp, hp;
          tf_svf_.Process(x, lp, hp);
          // tables saturation after the LP (2026-10-28): drive into tanh, FIXED trim (not 1/drive) -> evens out the
          // filtered level (quiet / closed up ~+5 dB, loud squashed); part of the tables path (faded with m)
          const float ls = explorer_k::EXP_TB_SAT_TRIM * tanhf(explorer_k::EXP_TB_SAT_DRIVE * lp);
          wet[n] = x + m * (ls - x);
        }
      }
    }
    tf_m_ = m;
  }

  // FM LAB SPECTRAL K3 LPF, per 8 kHz sample (2026-11-05: no bypass, K4 envelope as the SW1 MID tables): target =
  // SPECTRAL entering model; fc_knob = FLO (FHI / FLO)^k3 (CCW closed, CW open); log2 fc = log2 BOT + o (log2 fc_knob
  // - log2 BOT) with o the K4 openness (TouchOpenUpdate: the decay / rise envelopes with the live level, blended by w;
  // 1 = the K3 cutoff, 0 = EXP_SPEC_LPF_ENV_BOTTOM; 1 while looping).
  inline void SpecLpfUpdate(int sub, int mnew) {
    using namespace explorer_k;
    if (sub >= kLadSub) sub = kLadSub - 1;
    const bool on = lab_ && mnew == kLSPEC;
    sp_tgb_[sub] = on;
    if (!on && sp_m_ <= 0.f) { sp_fcb_[sub] = sp_set_fc_; sp_fc_ = 0.f; return; }
    const float fk = EXP_SPEC_LPF_FLO * expf(k3s_ * logf(EXP_SPEC_LPF_FHI / EXP_SPEC_LPF_FLO));
    const float g = loop_ ? 1.f : te_o_;       // K4 openness o
    const float fc = EXP_SPEC_LPF_ENV_BOTTOM * expf(g * logf(fk / EXP_SPEC_LPF_ENV_BOTTOM));
    sp_fcb_[sub] = fc;
    sp_fc_ = on ? fc : 0.f;
  }
  // TabFilter's structure without the tables saturation: wet = x + m (lp(x) - x), m slid by lad_step_ per 48 kHz
  // sample toward the 8 kHz sample's target (sp_tgb_); starting from m = 0 clears the state; coefficients set once
  // per 8 kHz sample, only when fc changed (Q fixed EXP_SPEC_LPF_Q).
  void SpecLpf(float* wet, size_t size) {
    using namespace explorer_k;
    float m = sp_m_;
    const int nsub = (int)(size / kFactor);
    bool any = (m > 0.f);
    for (int j = 0; j < nsub && j < kLadSub; j++) any = any || sp_tgb_[j];
    if (!any) return;                            // off: not run (bit-identical)
    const float st = lad_step_;
    size_t n = 0;
    for (int j = 0; j < nsub; j++) {
      const int jj = (j < kLadSub) ? j : kLadSub - 1;
      const bool tg = sp_tgb_[jj];
      const float fc = sp_fcb_[jj];
      if (fc != sp_set_fc_) { sp_svf_.Set(fc, EXP_SPEC_LPF_Q, sr_); sp_set_fc_ = fc; }
      for (int q = 0; q < kFactor; q++, n++) {
        if (tg) {
          if (m <= 0.f) sp_svf_.Reset();
          m += st; if (m > 1.f) m = 1.f;
        } else if (m > 0.f) {
          m -= st; if (m < 0.f) m = 0.f;
        }
        if (m > 0.f) {
          const float x = wet[n];
          float lp, hp;
          sp_svf_.Process(x, lp, hp);
          wet[n] = x + m * (lp - x);
        }
      }
    }
    sp_m_ = m;
  }

  // Dampener (tables: fc = tpk_fc_, the knob value only): per band of slot o, hf = min(TB_FMAX, fc) / f'_k -> the table blend lo / x (BlendPick, read with
  // EXP_TB_BLEND) and the discrete table version (largest H <= hf, floor H = 1; read with EXP_TB_BLEND = false),
  // and the bit-table step count (largest S of 16 / 8 / 4 / 2 with (S/2) f'_k <= min(FOLD_LIMIT_HZ, fc), floor 2;
  // EXP_ANTIFOLD only). A band whose discrete pick moves keeps its old pick in wtlvf_ / btsf_ and crossfades
  // old -> new linearly over pk_fade_len_ samples (EXP_DAMP_SLIDE_MS); all other bands' old = new. One shared
  // fade counter (DampUpdate): with 48-frame (1 ms) blocks a fade ends inside its block; a re-pick during a running
  // fade restarts it from the band's last target. With EXP_TB_BLEND a moved table version does not start the slide.
  // Returns true when a pick that slides moved.
  bool Repick(int o) {
    using namespace explorer_k;
    const float cw = (tpk_fc_ < EXP_TB_FMAX) ? tpk_fc_ : EXP_TB_FMAX;      // tables: unmodulated dampener fc
    const float lb = (tpk_fc_ < EXP_FOLD_LIMIT_HZ) ? tpk_fc_ : EXP_FOLD_LIMIT_HZ;   // bit-table limit, same fc
    bool moved = false;
    for (int k = 0; k < kBands; k++) {
      const float hf = cw * ifp_[o][k];
      if (EXP_TB_BLEND) BlendPick(hf, tblo_[o][k], tbx_[o][k]);
      int hk = (int)hf;
      if (hk > EXP_TB_HMAX) hk = EXP_TB_HMAX;
      const int lv = wt_lv_of_h_[hk];
      int st = EXP_BT_STEPS;
      if (EXP_ANTIFOLD) {
        const float fp = fp_[o][k];
        while (st > EXP_BT_STEPS_MIN && 0.5f * (float)st * fp > lb) st >>= 1;
      }
      wtlvf_[o][k] = wtlv_[o][k];
      btsf_[o][k] = btsc_[o][k];
      if (lv != wtlv_[o][k]) { wtlv_[o][k] = (uint8_t)lv; if (!EXP_TB_BLEND) moved = true; }
      if (st != btsc_[o][k]) { btsc_[o][k] = (uint8_t)st; moved = true; }
    }
    return moved;
  }

  // Table version blend at fractional harmonic count hf: lo = largest version with LEVEL_H[lo] <= hf, x =
  // (hf - H_lo) / (H_lo+1 - H_lo) toward hi = lo + 1; hf < 1 -> lo = 0, x = 0 (fundamental floor); hf >= the
  // last H -> lo = last, x = 0. LEVEL_H is integer, so lo = the version of floor(hf) (wt_lv_of_h_).
  void BlendPick(float hf, uint8_t& lo, float& x) const {
    using namespace explorer_k;
    if (!(hf >= 1.f)) { lo = 0; x = 0.f; return; }
    const int hk = (hf >= (float)EXP_TB_HMAX) ? EXP_TB_HMAX : (int)hf;
    const int l = wt_lv_of_h_[hk];
    lo = (uint8_t)l;
    if (l >= EXP_TB_LEVELS - 1) { x = 0.f; return; }
    const float h0 = (float)EXP_TB_LEVEL_H[l], h1 = (float)EXP_TB_LEVEL_H[l + 1];
    x = (hf - h0) / (h1 - h0);
  }

  float DiscOf(int m) const { return (m == kFM) ? (lab_ ? l2r_.v : rfmc_.v) : (m == kISH) ? ish_.v : 0.f; }   // FM LAB 2-OP: its own ratio slide (l2r_)

  // Derived values per model from K2 / K3 (0..1), the slid discrete value (FM ratio / interval semitones) and
  // the envelope offsets o2 / o3 (knob units: 1 = the parameter's full span; 0 = knob value only). Every span is
  // the parameter's full natural range, so p = clamp(p_knob + o span, range) = the parameter at clamp(k + o, 0, 1)
  // on the knob's own law; the frequency shift keeps the sign of the knob t and modulates |t| (= g / log2(801),
  // g = log2(|s| / F0 + 1)); the interval semitones are modulated in Process (ish_ target), the K3 dampener in
  // DampFmUpdate (fc, per 8 kHz sample). XBAND: K2 inert, o2 unused.
  static inline float Clamp01(float x) { return (x < 0.f) ? 0.f : (x > 1.f) ? 1.f : x; }
  Params Derive(int m, int zone, float k2, float k3, float disc, float o2, float o3) const {
    using namespace explorer_k;
    Params p;
    p.zone = zone;
    switch (m) {
      case kFM: {                          // p0 = r, p1 = 1/r, p2 = I, p3 = feedback b
        const float r = disc;
        p.p0 = r; p.p1 = 1.f / r;
        if (lab_) {                        // FM LAB 2-OP: K3 bipolar character x K4 touch env o (LabChar); K2 = the ratio
          float ue;
          LabChar(k3, loop_ ? 1.f : te_o_, p.p2, p.p3, ue);
        } else {
          p.p2 = EXP_FM_INDEX; p.p3 = EXP_FBFM_FB_MAX * Clamp01(k2 + o2);   // b in [0, FB_MAX]
        }
        p.a = r; p.b = p.p3; break;
      }
      case kLSPEC:                         // FM LAB SPECTRAL: weights in SpecWeights (Process); a / b = nu / gamma
        p.a = explorer_k::EXP_SPEC_NU_MAX * Clamp01(k2);   // (knob; the applied nu is SpecWeights')
        p.b = SpecGamma(explorer_k::EXP_SPEC_K_TILT);
        break;
      case kLADD: {                        // FM LAB ADDITIVE: p0 = I (before the per-band cap), p1 = u, p2 = 1 / sqrt(1 + u^2),
                                           // p3 / p4 = theta step constant / per Hz of f0 (cycles per 8 kHz sample),
                                           // p5 = delta, p7 = h; o2 = the K5 LFO in knob units (LfoO2)
        const ExpAddZone& Z = EXP_ADD_ZONES[(zone >= 0 && zone < EXP_ADD_N_ZONES) ? zone : 0];
        p.ser = Z.series; p.ctl = Z.ctl;
        const float gs = loop_ ? 1.f : te_o_;      // K4 touch env openness o (1 = the K3 value)
        float h = 1.f, ki = 0.f;
        switch (Z.ctl) {
          case kAddA:    h = Clamp01(k2 + o2); ki = k3; break;
          case kAddB:    ki = Clamp01(k2 + o2); h = gs * k3; p.capk = true; p.fel = true; break;
          case kAddUniI: h = 1.f; ki = k3; break;
          case kAddUniH: h = gs * k3; ki = 0.f; p.fel = true; break;
          default:       h = 1.f; ki = k3; p.p5 = EXP_ADD_STR_MAX * Clamp01(k2 + o2); break;   // STRETCH
        }
        p.p7 = h;
        p.p0 = EXP_ADD_FM_I_MAX * ki * ki;
        if (Z.ctl == kAddUniI || Z.ctl == kAddUniH) {
          const float u = k2;
          p.p1 = u;
          p.p2 = 1.f / sqrtf(1.f + u * u);
          const float t5 = 2.f * k5s_ - 1.f;
          if (t5 != 0.f) {                         // beat rate: MIN (MAX / MIN)^|t5|; noon 0 (theta frozen)
            const float rate = EXP_ADD_BEAT_MIN * expf(fabsf(t5) * logf(EXP_ADD_BEAT_MAX / EXP_ADD_BEAT_MIN));
            if (t5 < 0.f) p.p4 = rate / (EXP_ADD_BEAT_REF_HZ * sr8_);   // CCW: proportional to f0
            else          p.p3 = rate / sr8_;                           // CW: the same Hz on every band
          }
        } else {
          p.p2 = 1.f;
        }
        p.a = h; p.b = p.p0; break;
      }
      case kXFM:                           // n0 = partner offset, p1 = I
        p.n0 = EXP_XB_PARTNER;
        p.p1 = EXP_FM_INDEX;
        p.a = (float)p.n0; p.b = p.p1; break;
      case kFSH: {                         // p0 = fixed increment / sample, p1 = proportional increment per Hz of f'
        const float t = 2.f * k2 - 1.f;
        const float at = Clamp01(fabsf(t) + o2);           // |t| + d2 v: g = at log2(MAX / F0 + 1), [0, that]
        float s = EXP_FS_CURVE_HZ * (expf(at * logf(EXP_FS_MAX_HZ / EXP_FS_CURVE_HZ + 1.f)) - 1.f);
        if (t < 0.f) s = -s;                               // sign of the knob (t = 0: +)
        if (at == 0.f) s = 0.f;
        const float kb = Clamp01(k3 + o3);                 // scale blend
        p.p0 = s * (1.f - kb) / sr8_;
        p.p1 = s * kb / (EXP_FS_REF_HZ * sr8_);
        p.a = s; p.b = kb; break;
      }
      case kISH: {                         // p0 = 0, p1 = proportional increment per Hz of f'
        const float cents = EXP_IS_DETUNE_CENTS * (2.f * Clamp01(k3 + o3) - 1.f);   // [-50, 50]
        const float ratio = exp2f((disc + cents * 0.01f) * (1.f / 12.f));
        p.p0 = 0.f;
        p.p1 = (ratio - 1.f) / sr8_;
        p.a = disc; p.b = cents; break;
      }
      case kHARM: {                        // n0 + p0 = harmonic n, p1 = fundamental share
        const float n = 1.f + (EXP_HARM_MAX - 1.f) * Clamp01(k2 + o2);   // [1, MAX]
        const float kb = Clamp01(k3 + o3);                                // fundamental share
        p.n0 = (int)n;
        if (p.n0 > (int)EXP_HARM_MAX) p.n0 = (int)EXP_HARM_MAX;
        p.p0 = n - (float)p.n0; p.p1 = kb;
        p.a = n; p.b = kb; break;
      }
      case kWT: {                          // morph m = K2: n0 = packed frame below, p0 = fraction toward n0 + 1,
                                           // p1 = 1 / zone storage scale (WaveMorph MorphPos / Shape)
        const int nf = EXP_TB_ZONE_FRAMES[zone];
        const float mm = Clamp01(k2 + o2);                // morph m in [0, 1]
        const float pos = (float)(nf - 1) * mm;
        int f0 = (int)pos;
        if (f0 > nf - 2) f0 = nf - 2;
        if (f0 < 0) f0 = 0;
        p.n0 = tb_off_[zone] + f0;
        p.p0 = pos - (float)f0;
        p.p1 = 1.f / EXP_TB_ZONE_I16_SCALE[zone];
        p.a = pos; p.b = mm; break;
      }
      default: {                           // kBIT: bt = waveform zone (0..31); a = 1..32;
                                           // p0 / p1 / p2 = triangle / saw / pulse weights from K3
        p.bt = bt_all_[zone];
        const float x = (k3 < 0.5f) ? 2.f * k3 : 2.f * k3 - 1.f;
        if (k3 < 0.5f) { p.p0 = 1.f - x; p.p1 = x;       p.p2 = 0.f; }   // tri -> saw
        else           { p.p0 = 0.f;     p.p1 = 1.f - x; p.p2 = x;   }   // saw -> pulse
        p.a = (float)(zone + 1); p.b = k3; break;
      }
    }
    return p;
  }

  // One 8 kHz sample of slot o: derive u_k, return the model's band sum sum_k w_k a_k W_k.
  float RunSlot(int o, bool fading) {
    if (!slot_on_[o]) { ResetSlotState(o); slot_on_[o] = true; }
    Prep(o, NeedsPhaseV(var_) || (fading && NeedsPhaseV(vfrom_)));
    float sh = Shape(var_ >> 6, o, pn_);
    if (fading) sh = (1.f - mg_) * Shape(vfrom_ >> 6, o, pf_) + mg_ * sh;
    return sh;
  }

  // Models that read the band phase phi_k (ph_); ADDITIVE: only the STRETCH zone (the others work on the phasor).
  static inline bool NeedsPhase(int m) {
    return m == kFM || m == kHARM || m == kWT || m == kBIT;
  }
  static inline bool NeedsPhaseV(int var) {
    const int m = var >> 6;
    if (m == kLADD) {
      const int z = var & 63;
      return z >= 0 && z < explorer_k::EXP_ADD_N_ZONES && explorer_k::EXP_ADD_ZONES[z].series == explorer_k::kAddStr;
    }
    return NeedsPhase(m);
  }

  // u_k = z'_k / |z'_k| for slot o into ur_ / ui_ (0 for silent bands); with need_ph also the phase
  // phi_k in cycles into ph_ (once per band, shared by the models and a model crossfade).
  void Prep(int o, bool need_ph) {
    const float* __restrict mg = mag_ + 1;
    for (int k = 0; k < kBands; k++) {
      if (mg[k] == 0.f) {
        ur_[k] = ui_[k] = 0.f;
        continue;
      }
      const float iv = inv_[k];
      float zr, zi;
      if (o == kSP) {
        zr = bands_[k].Re(); zi = bands_[k].Im();
        if (k & 1) { zr = -zr; zi = -zi; }        // phase alignment (armitage ARM_ALIGN)
      } else if (o == kSU) {
        bands_[k].ArmUp1(iv, zr, zi);
      } else {                                    // kSD
        bands_[k].ArmDown1(iv, zr, zi);
      }
      ur_[k] = zr * iv;
      ui_[k] = zi * iv;
      if (need_ph) ph_[k] = FastAtan2Cycles(ui_[k], ur_[k]);   // the models' former call, same values
    }
  }

  float Shape(int m, int o, const Params& p) {
    switch (m) {
      case kFM:    return ShapeK<kFM>(o, p);
      case kXFM:   return ShapeK<kXFM>(o, p);
      case kFSH:   return ShapeK<kFSH>(o, p);
      case kISH:   return ShapeK<kISH>(o, p);
      case kHARM:  return ShapeK<kHARM>(o, p);
      case kWT:    return ShapeK<kWT>(o, p);
      case kLSPEC: return ShapeSpec(o, p);
      case kLADD:  return ShapeAdd(o, p);
      default:     return ShapeK<kBIT>(o, p);
    }
  }

  // Modulator phase that runs at r x the band's own phase (FM ratio zones).
  inline float AccPhase(int o, int k, float ph, float r) {
    float pm = pm_[o][k];
    const float pp = pp_[o][k];
    if (pp < 0.f) {
      pm = r * ph;
    } else {
      float dph = ph - pp;
      if (dph < -0.5f) dph += 1.f;
      else if (dph >= 0.5f) dph -= 1.f;
      pm += r * dph;
    }
    pm -= floorf(pm);
    pm_[o][k] = pm;
    pp_[o][k] = ph;
    return pm;
  }

  // sum_k w_k a_k W_k of model M in slot o.
  template <int M>
  float ShapeK(int o, const Params& p) {
    using namespace explorer_k;
    const float* __restrict mg  = mag_ + 1;
    const float* __restrict ur  = ur_;
    const float* __restrict ui  = ui_;
    const float* __restrict fp  = fp_[o];
    const float* __restrict ifp = ifp_[o];
    const float* __restrict phs = ph_;
    const bool played = (o == kSP);
    // FM LAB (lab_, only kFM reads these): L = lab_L_ (u_eff law, LabFmUpdate) for both (no envelope floor: Lk = L ->
    // floor 0); K5 LFO x the fade-in.
    const float L   = lab_ ? lab_L_ : hc_fold_;               // K3 dampener: min(FOLD_LIMIT_HZ, fc) (UP caps)
    const float Lk  = lab_ ? lab_L_ : hc_fold_k_;             // K3 knob alone (no envelope)
    const float lfb = lfo_s_ * EXP_LFO_FB_SWING * lab_fbf_;   // K5 LFO on the feedback (x the swell fade-in), AFTER the cap
    const int   pkf = pk_fade_n_;                             // dampener discrete pick slide running
    const float pkg = pk_g_;
    const uint8_t* __restrict wlv  = wtlv_[o];
    const uint8_t* __restrict wlvf = wtlvf_[o];
    const uint8_t* __restrict bsc  = btsc_[o];
    const uint8_t* __restrict bsf  = btsf_[o];
    const uint8_t* __restrict tlo  = tblo_[o];                // EXP_TB_BLEND: lower version, weight 1 - x
    const float*   __restrict tx   = tbx_[o];                 //               blend weight x toward lo + 1
    constexpr int ru = (M == kISH) ? kRotISH : kRotFSH;
    float acc = 0.f;
    for (int k = 0; k < kBands; k++) {
      const float a = mg[k];
      if (a == 0.f) {                            // silent band: state cleared
        if (M == kFM) { pp_[o][k] = -1.f; m1_[o][k] = 0.f; m2_[o][k] = 0.f; }
        continue;
      }
      const float cr = ur[k], ci = ui[k];
      float W;
      if (M == kFM) {
        // m = sin(2 pi pm + b (m1 + m2) / 2), W = cos(2 pi phi + I m); pm at r x the band's phase.
        const float pm = AccPhase(o, k, phs[k], p.p0);
        float I = p.p2, b = p.p3;
        if (EXP_ANTIFOLD && !EXP_FBFM_PUSHED) {  // pushed variant: no caps
          const float x = L * ifp[k] * p.p1;     // L / (r f')
          float ic = (x - p.p1) - 1.f;           // (L / f' - 1) / r - 1
          if (ic < 0.f) ic = 0.f;
          // envelope floor (2026-10-28): the envelope closes the index only down to EXP_FM_ENV_INDEX_FLOOR (or the
          // K3 knob's own cap if that is lower)
          const float xk = Lk * ifp[k] * p.p1;
          float ick = (xk - p.p1) - 1.f;
          if (ick < 0.f) ick = 0.f;
          float fl = EXP_FM_ENV_INDEX_FLOOR;
          if (fl > ick) fl = ick;
          if (ic < fl) ic = fl;
          if (I > ic) I = ic;
          float bc = (x - 1.f) * (1.f / EXP_FBFM_FB_SPAN);
          if (bc < 0.f) bc = 0.f;
          if (b > bc) b = bc;
        }
        I += lfo_s_ * EXP_LFO_INDEX_SWING;       // K5 LFO on top of the (envelope-capped) index as well
        if (I < 0.f) I = 0.f;
        if (I > EXP_FM_INDEX) I = EXP_FM_INDEX;
        b += lfb;                                // K5 LFO on top of the (envelope-capped) feedback, modular-style sum
        if (b < 0.f) b = 0.f;
        if (b > EXP_FBFM_FB_MAX) b = EXP_FBFM_FB_MAX;
        float cm, sm;
        CosSin01(pm, cm, sm);
        float s, c;
        SinCos(b * 0.5f * (m1_[o][k] + m2_[o][k]), s, c);
        float mod = sm * c + cm * s;             // sin(2 pi pm + b avg)
        if (fabsf(mod) < EXP_DENORM) mod = 0.f;
        m2_[o][k] = m1_[o][k];
        m1_[o][k] = mod;
        SinCos(I * mod, s, c);
        W = cr * c - ci * s;
      } else if (M == kXFM) {
        const int q = k + p.n0;
        float W0 = cr;
        if (q < kBands) {
          float I = p.p1;
          if (EXP_ANTIFOLD) {
            float cap = (L - fp[k]) * ifp[q] - 1.f;
            if (cap < 0.f) cap = 0.f;
            if (I > cap) I = cap;
          }
          float s, c;
          SinCos(I * ui[q], s, c);
          W0 = cr * c - ci * s;
        }
        W = W0;
      } else if (M == kFSH || M == kISH) {
        float th = th_[ru][o][k] + p.p0 + p.p1 * fp[k];
        th -= floorf(th);
        th_[ru][o][k] = th;
        float c, s;
        CosSin01(th, c, s);
        W = cr * c - ci * s;
      } else if (M == kHARM) {
        const float ph = phs[k];
        const int n0 = p.n0, n1 = n0 + 1;
        float h0 = 1.f, h1 = 1.f;
        if (EXP_ANTIFOLD) {
          constexpr float ie = 1.f / EXP_HARM_FEDGE;
          h0 = (EXP_HARM_FMAX - (float)n0 * fp[k]) * ie;
          h0 = (h0 < 0.f) ? 0.f : (h0 > 1.f) ? 1.f : h0;
          h1 = (EXP_HARM_FMAX - (float)n1 * fp[k]) * ie;
          h1 = (h1 < 0.f) ? 0.f : (h1 > 1.f) ? 1.f : h1;
        }
        const float H = (1.f - p.p0) * h0 * Cos01((float)n0 * ph) + p.p0 * h1 * Cos01((float)n1 * ph);
        W = H + p.p1 * (cr - H);
      } else if (M == kWT && EXP_TB_BLEND) {     // WaveMorph's Shape, versions lo / lo + 1 blended (x)
        const float ph = phs[k];
        const float xi = ph * (float)EXP_TB_SIZE;
        const int   i0 = (int)xi;
        const float fr = xi - (float)i0;
        const int   j0 = i0 & (EXP_TB_SIZE - 1), j1 = (i0 + 1) & (EXP_TB_SIZE - 1);
        const int   lo = tlo[k];
        const float x  = tx[k];
        const int16_t* t0 = explorer_tb[p.n0][lo];
        const int16_t* t1 = explorer_tb[p.n0 + 1][lo];
        const float v0 = (float)t0[j0] + fr * (float)(t0[j1] - t0[j0]);
        const float v1 = (float)t1[j0] + fr * (float)(t1[j1] - t1[j0]);
        float Wv = v0 + p.p0 * (v1 - v0);                // read(lo), unscaled
        if (x != 0.f) {                                  // read(hi = lo + 1), linear blend
          const int16_t* u0 = explorer_tb[p.n0][lo + 1];
          const int16_t* u1 = explorer_tb[p.n0 + 1][lo + 1];
          const float w0 = (float)u0[j0] + fr * (float)(u0[j1] - u0[j0]);
          const float w1 = (float)u1[j0] + fr * (float)(u1[j1] - u1[j0]);
          Wv += x * ((w0 + p.p0 * (w1 - w0)) - Wv);
        }
        W = Wv * p.p1;
      } else if (M == kWT) {                     // WaveMorph's Shape: frames n0 / n0 + 1 of the packed tables
        const float ph = phs[k];
        const int lv = wlv[k];
        const int16_t* t0 = explorer_tb[p.n0][lv];
        const int16_t* t1 = explorer_tb[p.n0 + 1][lv];
        const float xi = ph * (float)EXP_TB_SIZE;
        const int   i0 = (int)xi;
        const float fr = xi - (float)i0;
        const int   j0 = i0 & (EXP_TB_SIZE - 1), j1 = (i0 + 1) & (EXP_TB_SIZE - 1);
        const float v0 = (float)t0[j0] + fr * (float)(t0[j1] - t0[j0]);
        const float v1 = (float)t1[j0] + fr * (float)(t1[j1] - t1[j0]);
        W = (v0 + p.p0 * (v1 - v0)) * p.p1;
        if (pkf > 0 && wlvf[k] != lv) {          // dampener: version moved -> linear crossfade old -> new
          const int16_t* u0 = explorer_tb[p.n0][wlvf[k]];
          const int16_t* u1 = explorer_tb[p.n0 + 1][wlvf[k]];
          const float w0 = (float)u0[j0] + fr * (float)(u0[j1] - u0[j0]);
          const float w1 = (float)u1[j0] + fr * (float)(u1[j1] - u1[j0]);
          const float Wf = (w0 + p.p0 * (w1 - w0)) * p.p1;
          W = Wf + pkg * (W - Wf);
        }
      } else {                                   // kBIT: step shape tri / saw / pulse (K3)
        const float ph = phs[k];
        const int S = bsc[k];
        W = BitStep(p, ph, S);
        if (pkf > 0 && bsf[k] != S) {            // dampener: step count moved -> linear crossfade old -> new
          const float Wf = BitStep(p, ph, bsf[k]);
          W = Wf + pkg * (W - Wf);
        }
      }
      acc += (played ? wmid_[k] : 1.f) * a * W;
    }
    return acc;
  }

  // BITTAB band output at phase ph with S steps (pattern subsampled every 16 / S-th step).
  static inline float BitStep(const Params& p, float ph, int S) {
    using namespace explorer_k;
    const int stride = EXP_BT_STEPS / S;
    const float xs = ph * (float)S;
    int j = (int)xs;
    if (j > S - 1) j = S - 1;
    const float f = xs - (float)j;
    const float v0 = p.bt[j * stride];
    const float v1 = p.bt[((j + 1 == S) ? 0 : j + 1) * stride];
    const float tri = v0 + f * (v1 - v0);    // linear to the next step
    const float saw = (v1 > v0) ? tri : v0;    // ramps on rising steps, hard edge on falling ones
    return p.p0 * tri + p.p1 * saw + p.p2 * v0;   // v0 = pulse (hold, hard steps)
  }

  // Clear one model's state in slot o (on model entry).
  void ResetModelState(int m, int o) {
    const int ru = (m == kFSH) ? kRotFSH : (m == kISH) ? kRotISH : -1;
    for (int k = 0; k < kBands; k++) {
      if (m == kFM) { pm_[o][k] = 0.f; pp_[o][k] = -1.f; m1_[o][k] = m2_[o][k] = 0.f; }
      if (ru >= 0)  th_[ru][o][k] = 0.f;
      if (m == kLSPEC && lab_) explorer_spec_st.th[o][k] = 0.f;   // FM LAB SPECTRAL shift phase (D2 SRAM)
      if (m == kLADD && lab_) {                  // FM LAB ADDITIVE (D2 SRAM, the lab instance only)
        ExplorerAddState& A = explorer_add_st;
        A.th[o][k] = explorer_k::EXP_ADD_UNI_PHASE;
        A.wr[o][k] = A.wi[o][k] = 0.f;
        A.slp[o][k] = -1.f;
      }
    }
  }
  void ResetSlotState(int o) {
    ResetModelState(kFM, o);
    ResetModelState(kFSH, o);
    ResetModelState(kISH, o);
    if (lab_) ResetModelState(kLADD, o);
    if (lab_) ResetModelState(kLSPEC, o);        // SPECTRAL: K4 shift phase
  }

  static inline float Slide(float v, float t, float st) {
    if (v < t) { v += st; if (v > t) v = t; }
    else if (v > t) { v -= st; if (v < t) v = t; }
    return v;
  }

  // BITTAB waveforms: 0..15 = LFSR patterns (16-bit Fibonacci LFSR per tap set, fixed seed, first 16
  // new bits, +-1); 16..31 = SQR n = 1..16 (square x harmonic n, sampled at the step starts).
  void BuildBitPatterns() {
    using namespace explorer_k;
    for (int t = 0; t < EXP_BT_PATTERNS; t++) {
      uint16_t st = EXP_BT_SEED;
      for (int j = 0; j < EXP_BT_STEPS; j++) {
        const unsigned bit = (unsigned)__builtin_popcount((unsigned)(st & EXP_BT_TAPS[t])) & 1u;
        st = (uint16_t)((st >> 1) | (bit << 15));
        bt_all_[t][j] = bit ? 1.f : -1.f;
      }
    }
    for (int n = 1; n <= EXP_BT_SQR_N_MAX; n++)
      for (int j = 0; j < EXP_BT_STEPS; j++) {
        const float sq = (j < EXP_BT_STEPS / 2) ? 1.f : -1.f;
        bt_all_[EXP_BT_PATTERNS + n - 1][j] = sq * Cos01((float)(n * j) * (1.f / (float)EXP_BT_STEPS));
      }
  }

  // K4 post filter on the final 48 kHz wet (armitage's PostFilter without the envelope; Q fixed).
  void PostFilter(float* wet, size_t size) {
    using namespace explorer_k;
    if ((int)size != f_sz_) {
      f_sz_ = (int)size;
      f_a_ = 1.f - expf(-(float)size / (EXP_FILT_SMOOTH_MS * 0.001f * sr_));
    }
    const bool off = filt_off_;                  // SW1 UP: no post filter
    if (off && f_m_ <= 0.f) {                    // faded out: bypass (state kept cleared on re-entry)
      f_on_ = false;
      return;
    }
    float ts = ft_;                              // UP: t held (cutoff fixed) while the mix slides to 0
    if (!off) {
      const float tt = ft_t_;
      ts = ft_ + (tt - ft_) * f_a_;
      if (tt == 0.f && fabsf(ts) < EXP_FILT_SNAP) ts = 0.f;
      ft_ = ts;
    }
    if (ts == 0.f && f_m_ <= 0.f) {              // noon: true bypass
      f_on_ = false;
      return;
    }
    const bool want_hp = (ts > 0.f);
    if (!f_on_) {
      f_on_ = true;
      f_hp_ = want_hp;
      svf_[0].Reset();
      svf_[1].Reset();
    }
    const float at = fabsf(ts);
    float mt = at * (1.f / EXP_FILT_FADE);
    if (mt > 1.f) mt = 1.f;
    if (ts == 0.f || want_hp != f_hp_ || off) mt = 0.f;
    {
      const float tmag = expf(EXP_FILT_CURVE * logf((at > 1e-6f) ? at : 1e-6f));   // |t|^curve
      float fc = f_hp_ ? EXP_FILT_FMIN * expf(tmag * f_lnhp_)
                       : EXP_FILT_FMAX * expf(tmag * f_lnlp_);
      fc = (fc < EXP_FILT_FC_MIN) ? EXP_FILT_FC_MIN : (fc > EXP_FILT_FC_MAX) ? EXP_FILT_FC_MAX : fc;
      svf_[0].Set(fc, EXP_FILT_Q, sr_);
      svf_[1].CopyCoefFrom(svf_[0]);
    }
    float m = f_m_;
    const float st = f_step_;
    ExpSVF& s0 = svf_[0];
    ExpSVF& s1 = svf_[1];
    if (f_hp_) {
      for (size_t n = 0; n < size; n++) {
        if (m < mt)      { m += st; if (m > mt) m = mt; }
        else if (m > mt) { m -= st; if (m < mt) m = mt; }
        const float x = wet[n];
        float lp, hp, y;
        s0.Process(x, lp, hp);
        if (EXP_FILT_STAGES == 2) s1.Process(hp, lp, y);   // second stage (24 dB/oct)
        else                      y = hp;                  // one stage (12 dB/oct)
        wet[n] = x + m * (y - x);
      }
    } else {
      for (size_t n = 0; n < size; n++) {
        if (m < mt)      { m += st; if (m > mt) m = mt; }
        else if (m > mt) { m -= st; if (m < mt) m = mt; }
        const float x = wet[n];
        float lp, hp, y;
        s0.Process(x, lp, hp);
        if (EXP_FILT_STAGES == 2) s1.Process(lp, y, hp);   // second stage (24 dB/oct)
        else                      y = lp;                  // one stage (12 dB/oct)
        wet[n] = x + m * (y - x);
      }
    }
    f_m_ = m;
    if (m <= 0.f && want_hp != f_hp_ && ts != 0.f) {
      f_hp_ = want_hp;
      s0.Reset();
      s1.Reset();
    }
    if (m <= 0.f && ts == 0.f) f_on_ = false;
  }

  // --- SW1 MID table build (COPIED from wavemorph.h: TimeDomain / IsTimeDomain / PwmWidth / Spectral / SynthPeak /
  // Rms / BuildTables; WM_* -> EXP_TB_*, wavemorph_sin_tab -> explorer_sin_tab (same size and values), output into
  // explorer_tb) --------------------------------------------------------------------------------------------------
  // Time-domain definition of zone z, frame c (u = c / (F_z - 1)), phi = i / EXP_TB_TD_SIZE.
  static float TbTimeDomain(int z, int c, float u, float ph) {
    using namespace explorer_k;
    switch (z) {
      case 0: {                                    // sine | sync
        float ps = EXP_TB_SYNC_R[c] * ph;
        ps -= floorf(ps);
        return sinf(6.283185307f * ps);
      }
      case 1: {                                    // triangle | fold
        const float tri = (ph < 0.25f) ? 4.f * ph : (ph < 0.75f) ? 2.f - 4.f * ph : 4.f * ph - 4.f;
        const float v = EXP_TB_FOLD_G[c] * tri;
        float m = fmodf(v - 1.f, 4.f);
        if (m < 0.f) m += 4.f;
        return fabsf(m - 2.f) - 1.f;
      }
      case 2: {                                    // saw rough 2: sine + needle (Hann spike on the peak)
        float x = sinf(6.283185307f * ph);
        const float d = ph - EXP_TB_SNDL_POS;
        if (fabsf(d) < 0.5f * EXP_TB_SNDL_W) x += EXP_TB_SNDL_H * 0.5f * (1.f + cosf(6.283185307f * d / EXP_TB_SNDL_W));
        return x;
      }
      case 3:                                      // PWM 50 % -> silence (curve: narrow end)
        return (ph < TbPwmWidth(u)) ? 1.f : -1.f;
      case 5:                                      // PPG classic
        switch (c) {
          case 0: {                                // saw: 8 steps, 8 levels
            const int st = (int)(ph * (float)EXP_TB_PPG_SAW_STEPS);
            return 1.f - 2.f * (float)st / (float)(EXP_TB_PPG_SAW_STEPS - 1);
          }
          case 1: {                                // odd-harmonic stack, 32 steps, 16 levels
            const float xs = floorf(ph * (float)EXP_TB_PPG_ODD_STEPS) / (float)EXP_TB_PPG_ODD_STEPS;
            const float y = sinf(6.283185307f * xs) + EXP_TB_PPG_ODD_A3 * sinf(6.283185307f * 3.f * xs)
                          + EXP_TB_PPG_ODD_A5 * sinf(6.283185307f * 5.f * xs);
            return roundf(y * EXP_TB_PPG_ODD_LEVELS) / EXP_TB_PPG_ODD_LEVELS;
          }
          case 2: {                                // bit pattern ((s 5) ^ (s >> 2)) & 15 over 32 steps
            const int st = (int)(ph * (float)EXP_TB_PPG_BIT_STEPS);
            return (float)(((st * 5) ^ (st >> 2)) & 15) / 7.5f - 1.f;
          }
          default:                                 // sine held in 16 steps
            return sinf(6.283185307f * floorf(ph * (float)EXP_TB_PPG_SINE_STEPS) / (float)EXP_TB_PPG_SINE_STEPS);
        }
      case 7:                                      // soft B 4: stepped lo-res sine (held per step)
        return sinf(6.283185307f * floorf((float)EXP_TB_LORES_STEPS * ph) / (float)EXP_TB_LORES_STEPS);
      default: {                                   // hard B 1: sync saw, reset each cycle
        float ps = EXP_TB_SYNCSAW_R * ph;
        ps -= floorf(ps);
        return 2.f * ps - 1.f;
      }
    }
  }
  static inline bool TbIsTimeDomain(int z, int c) {
    return z == 0 || z == 1 || z == 3 || z == 5 || (z == 2 && c == 1) || (z == 7 && c == 3) || (z == 9 && c == 0);
  }

  // PWM width at u: 0.5 (1 - u)^EXP_TB_PWM_CURVE (u = 1: 0, silence).
  static inline float TbPwmWidth(float u) {
    using namespace explorer_k;
    return (u >= 1.f) ? 0.f : 0.5f * powf(1.f - u, EXP_TB_PWM_CURVE);
  }

  // Spectral definition of zone z, frame c (u = c / (F_z - 1)): A_h (sin), B_h (cos) for h = 1..EXP_TB_HMAX.
  // Sine phase throughout (B = 0).
  static void TbSpectral(int z, int c, float u, float* A, float* B) {
    using namespace explorer_k;
    for (int h = 0; h <= EXP_TB_HMAX; h++) { A[h] = 0.f; B[h] = 0.f; }
    auto put = [&](int h, float amp) { if (h >= 1 && h <= EXP_TB_HMAX) A[h] += amp; };
    auto list = [&](const ExpTbHarm* w, int n) { for (int i = 0; i < n; i++) put(w[i].h, w[i].a); };
#define EXP_TB_LIST(arr) list(arr, (int)(sizeof(arr) / sizeof(arr[0])))
    switch (z) {
      case 2:                                      // saw rough: 1 saw | 3 needle saw (2 = sine + needle, time domain)
        for (int h = 1; h <= EXP_TB_HMAX; h++) put(h, (c == 0) ? 1.f / (float)h : powf((float)h, -EXP_TB_NSAW_EXP));
        break;
      case 4: {                                    // vocal a -> e -> i -> o -> u
        const float v = u * (float)(EXP_TB_VOX_N - 1);
        int v0 = (int)v;
        if (v0 > EXP_TB_VOX_N - 2) v0 = EXP_TB_VOX_N - 2;
        const float vf = v - (float)v0;
        float F[3];
        for (int f = 0; f < 3; f++) F[f] = EXP_TB_VOX_F[v0][f] + vf * (EXP_TB_VOX_F[v0 + 1][f] - EXP_TB_VOX_F[v0][f]);
        for (int h = 1; h <= EXP_TB_HMAX; h++) {
          const float fh = (float)h * EXP_TB_VOX_F0;
          float amp = 0.f;
          for (int f = 0; f < 3; f++) {
            const float e = (fh - F[f]) / (0.5f * EXP_TB_VOX_BW[f]);
            amp += EXP_TB_VOX_A[f] / (1.f + e * e);
          }
          put(h, amp);
        }
        break;
      }
      case 6:                                      // soft A
        switch (c) {
          case 0: EXP_TB_LIST(EXP_TB_W_TINE); break;
          case 1: EXP_TB_LIST(EXP_TB_W_CLARINET); break;
          case 2: EXP_TB_LIST(EXP_TB_W_DRAWBAR); break;
          default: EXP_TB_LIST(EXP_TB_W_MIXTURE); break;
        }
        break;
      case 7:                                      // soft B (4 = lo-res is time domain)
        switch (c) {
          case 0:                                  // harmonium
            put(1, EXP_TB_HARMONIUM_H1); put(2, EXP_TB_HARMONIUM_H2); put(3, EXP_TB_HARMONIUM_H3);
            for (int h = 4; h <= EXP_TB_HMAX; h++) put(h, EXP_TB_HARMONIUM_HN / (float)h);
            break;
          case 1: EXP_TB_LIST(EXP_TB_W_GHOST5); break;
          default: EXP_TB_LIST(EXP_TB_W_BELL); break;
        }
        break;
      case 8:                                      // hard A
        switch (c) {
          case 0:                                  // PPG resonant
            for (int h = 1; h <= EXP_TB_PPGRES_N; h++) put(h, 1.f - EXP_TB_PPGRES_SLOPE * (float)(h - 1));
            break;
          case 1:                                  // nasal oboe reed
            EXP_TB_LIST(EXP_TB_W_OBOE);
            for (int h = 9; h <= EXP_TB_HMAX; h++) put(h, EXP_TB_OBOE_TAIL * powf(8.f / (float)h, EXP_TB_OBOE_TAIL_EXP));
            break;
          case 2:                                  // formant brass
            for (int h = 1; h <= EXP_TB_HMAX; h++) {
              const float e = ((float)h - EXP_TB_BRASS_CENTER) / EXP_TB_BRASS_WIDTH;
              put(h, (1.f / (float)h) * (1.f + EXP_TB_BRASS_GAIN * expf(-e * e)));
            }
            break;
          default: EXP_TB_LIST(EXP_TB_W_BASSBELL); break;   // bass bell 7/14
        }
        break;
      default:                                     // hard B (zone 10; 1 = sync saw is time domain)
        switch (c) {
          case 1:                                  // fuzz reed
            for (int h = 1; h <= EXP_TB_HMAX; h++) put(h, powf((float)h, -EXP_TB_FUZZ_EXP));
            break;
          case 2:                                  // clipper
            put(1, EXP_TB_CLIP_H1);
            for (int h = 2; h <= EXP_TB_HMAX; h++) {
              const float kh = (float)EXP_TB_CLIP_KNEE / (float)h;
              put(h, (h <= EXP_TB_CLIP_KNEE) ? EXP_TB_CLIP_FLAT : EXP_TB_CLIP_FLAT * kh * kh);
            }
            break;
          default:                                 // PPG feedback: saw 1/n without harmonics 1 .. SKIP
            for (int h = EXP_TB_PPGFB_SKIP + 1; h <= EXP_TB_HMAX; h++) put(h, 1.f / (float)h);
            break;
        }
        break;
    }
#undef EXP_TB_LIST
  }

  // Peak of the full-band (harmonics 1..EXP_TB_HMAX) frame, synthesised at EXP_TB_TD_SIZE points.
  static float TbSynthPeak(const float* A, const float* B) {
    using namespace explorer_k;
    constexpr int N = EXP_TB_TD_SIZE, M = N - 1, Q = N / 4;
    const float* tab = explorer_sin_tab;
    float pk = 0.f;
    for (int i = 0; i < N; i++) {
      float v = 0.f;
      int idx = 0;
      for (int h = 1; h <= EXP_TB_HMAX; h++) {
        idx = (idx + i) & M;                     // h i mod N
        v += A[h] * tab[idx] + B[h] * tab[(idx + Q) & M];
      }
      if (fabsf(v) > pk) pk = fabsf(v);
    }
    return pk;
  }
  static float TbRms(const float* A, const float* B) {   // full-band RMS from the coefficients
    float e = 0.f;
    for (int h = 1; h <= explorer_k::EXP_TB_HMAX; h++) e += A[h] * A[h] + B[h] * B[h];
    return sqrtf(0.5f * e);
  }

  // All zones and frames: definition -> (DFT) -> mean removed, peak 1 -> (re-phase) -> (RMS match + cap)
  // -> 14 band-limited int16 levels. Needs explorer_sin_tab filled (Init does it first).
  void BuildTables() {
    using namespace explorer_k;
    static_assert(EXP_SIN_SIZE == EXP_TB_TD_SIZE, "table build indexes explorer_sin_tab exactly (as wavemorph)");
    constexpr int N = EXP_TB_TD_SIZE;
    constexpr int M = N - 1;
    constexpr int Q = N / 4;                     // quarter period (cos offset)
    constexpr int S = N / EXP_TB_SIZE;           // table step in the sin table
    float* x   = explorer_tb_scratch.x;
    float* acc = explorer_tb_scratch.acc;
    float* A   = explorer_tb_scratch.A;
    float* B   = explorer_tb_scratch.B;
    const float* tab = explorer_sin_tab;
    float rms_saw = 1.f;                         // RMS of the plain saw (zone 3 frame 1), set when built
    int off = 0;
    for (int z = 0; z < EXP_TB_ZONES; z++) {
      tb_off_[z] = off;
      const int nf = EXP_TB_ZONE_FRAMES[z];
      off += nf;
      for (int f = 0; f < nf; f++) {
        const float u = (float)f / (float)(nf - 1);
        if (TbIsTimeDomain(z, f)) {
          // 1) time domain, mean removed, peak 1
          float mean = 0.f;
          for (int i = 0; i < N; i++) { x[i] = TbTimeDomain(z, f, u, (float)i / (float)N); mean += x[i]; }
          mean /= (float)N;
          float pk = 0.f;
          for (int i = 0; i < N; i++) { x[i] -= mean; if (fabsf(x[i]) > pk) pk = fabsf(x[i]); }
          const float g = (pk > 0.f) ? 1.f / pk : 0.f;
          // 2) DFT: x = sum_h A_h sin + B_h cos (peak scale folded in)
          for (int h = 1; h <= EXP_TB_HMAX; h++) {
            float a = 0.f, b = 0.f;
            int idx = 0;
            for (int i = 0; i < N; i++) {
              a += x[i] * tab[idx];
              b += x[i] * tab[(idx + Q) & M];
              idx = (idx + h) & M;
            }
            A[h] = a * g * (2.f / (float)N);
            B[h] = b * g * (2.f / (float)N);
          }
        } else {
          // 1) harmonic definition; peak of the full-band frame (no DC by construction)
          TbSpectral(z, f, u, A, B);
          const float pk = TbSynthPeak(A, B);
          const float g = (pk > 0.f) ? 1.f / pk : 0.f;
          for (int h = 1; h <= EXP_TB_HMAX; h++) { A[h] *= g; B[h] *= g; }
        }
        if (EXP_TB_ZONE_REPHASE[z]) {                 // zones 1, 2, 3, 6, 10: one phase convention (the saw's:
          for (int h = 1; h <= EXP_TB_HMAX; h++) {    // sine phase, positive), magnitudes kept exactly
            if (B[h] == 0.f && A[h] >= 0.f) continue; // already in the convention: left bit-exact
            A[h] = sqrtf(A[h] * A[h] + B[h] * B[h]);
            B[h] = 0.f;
          }
          if (!EXP_TB_ZONE_RMS_MATCH[z]) {            // peak-normalised zones: peak 1 again after re-phasing
            const float pk = TbSynthPeak(A, B);
            const float g = (pk > 0.f) ? 1.f / pk : 0.f;
            for (int h = 1; h <= EXP_TB_HMAX; h++) A[h] *= g;
          }
        }
        if (z == 2 && f == 0) rms_saw = TbRms(A, B);  // the plain saw: RMS target of zones 3, 6..10
        if (EXP_TB_ZONE_RMS_MATCH[z]) {               // zones 3, 6..10: equal RMS, then the storage cap
          const float r = TbRms(A, B);
          float g = (r > 0.f) ? rms_saw / r : 0.f;
          for (int h = 1; h <= EXP_TB_HMAX; h++) { A[h] *= g; B[h] *= g; }
          const float pk = TbSynthPeak(A, B);
          const float cap = 32767.f / EXP_TB_ZONE_I16_SCALE[z];   // the zone's storage headroom
          g = (pk > cap) ? cap / pk : 1.f;
          for (int h = 1; h <= EXP_TB_HMAX; h++) { A[h] *= g; B[h] *= g; }
        }
        // 3) band-limited levels (cumulative), 256 points
        for (int i = 0; i < EXP_TB_SIZE; i++) acc[i] = 0.f;
        int hdone = 0;
        for (int l = 0; l < EXP_TB_LEVELS; l++) {
          const int ht = EXP_TB_LEVEL_H[l];
          for (int h = hdone + 1; h <= ht; h++) {
            const float a = A[h], b = B[h];
            if (a == 0.f && b == 0.f) continue;
            int idx = 0;
            const int step = (h * S) & M;
            for (int i = 0; i < EXP_TB_SIZE; i++) {
              acc[i] += a * tab[idx] + b * tab[(idx + Q) & M];
              idx = (idx + step) & M;
            }
          }
          hdone = ht;
          int16_t* dst = explorer_tb[tb_off_[z] + f][l];
          for (int i = 0; i < EXP_TB_SIZE; i++) {
            float v = acc[i] * EXP_TB_ZONE_I16_SCALE[z];
            v = (v > 32767.f) ? 32767.f : (v < -32768.f) ? -32768.f : v;   // int16 range (storage)
            dst[i] = (int16_t)lrintf(v);
          }
        }
      }
    }
  }

  // Wet DC blocker (copied from armitage).
  void DcBlock(float* wet, size_t size) {
    float x1 = dc_x1_, y1 = dc_y1_;
    const float a = dc_a_;
    for (size_t n = 0; n < size; n++) {
      const float x = wet[n];
      const float y = x - x1 + a * y1;
      wet[n] = y;
      x1 = x;
      y1 = y;
    }
    dc_x1_ = x1;
    dc_y1_ = y1;
  }

  // Wet soft clipper (copied from armitage): EXP_SAT_TRIM * EXP_SAT_T * tanh(x / EXP_SAT_T).
  void Saturate(float* wet, size_t size) {
    using namespace explorer_k;
    constexpr float kIn  = 1.f / EXP_SAT_T;
    constexpr float kOut = EXP_SAT_T * EXP_SAT_TRIM;
    for (size_t n = 0; n < size; n++) wet[n] = kOut * PadeTanh(wet[n] * kIn);
  }
  static inline float PadeTanh(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
  }

  // FS1 wet mute (copied from armitage).
  void ApplyMute(float* wet, size_t size) {
    const float target = mute_ ? 0.f : 1.f;
    const float step = mute_step_;
    float g = mute_g_;
    for (size_t n = 0; n < size; n++) {
      if (g < target)      { g += step; if (g > target) g = target; }
      else if (g > target) { g -= step; if (g < target) g = target; }
      wet[n] *= g;
    }
    mute_g_ = g;
  }

  void DiagControls(const ControlSurface& cs, daisy::Led& led1, const float* kv,
                    int sw1, int oct, int model, int zone, float a, float b, float fc, float lfe, float env,
                    int dir, float d, int tgt, float gT, float fce, int lfo, float lfod, float lfohz) {
    using namespace explorer_k;
    const uint32_t now = daisy::System::GetNow();
    DiagSnap s;
    s.n = 0;
    for (int i = 0; i < 5; i++) s.k[i] = kv[i];
    s.sw1 = sw1; s.sw2 = oct;
    s.model = model; s.zone = zone;
    s.a = a; s.b = b;
    s.fc = fc;
    s.lfe = lfe;
    s.dfc = dfc_diag_;
    s.loop = loop_;
    s.env = env;
    s.dir = dir;
    s.d = d;
    s.tgt = tgt;
    s.T = gT; s.fce = fce;
    s.lfo = lfo; s.lfod = lfod; s.lfohz = lfohz;
    s.labz = diag_lab_z_;
    s.lb = diag_lab_b_; s.lue = diag_lab_ue_; s.llfd = diag_lab_lfd_; s.lside = diag_lab_side_;
    s.specz = diag_spec_z_;
    s.sn = diag_spec_n_;
    s.sgd = te_gd_; s.sTd = te_Td_; s.sgr = te_gr_; s.sTr = te_Tr_; s.sw = te_w_; s.str = sp_tr_;   // touch env
    s.slp = sp_fc_;
    s.ssub = (k5s_ > explorer_k::EXP_SPEC_SUB_ON) ? explorer_k::EXP_SPEC_SUB_MAX * k5s_ : 0.f;
    s.sL = te_L_; s.so = te_o_;
    s.addz = diag_add_z_;
    s.c3 = diag_add_c3_;
    s.bm = diag_add_bm_;
    s.mute = mute_;
    if (cs.Foot(0).rising) {
      s.n = ++diag_n_;
      diag_tap_ = s;
      diag_tap_pend_ = true;
      diag_blink_t_ = now;
      diag_blink_ = true;
    }
    if (diag_blink_) {
      if (now - diag_blink_t_ < EXP_DIAG_BLINK_MS) led1.Set(0.f);
      else diag_blink_ = false;
    }
    bool moved = (sw1 != diag_ref_sw1_) || (oct != diag_ref_sw2_);
    for (int i = 0; i < 5; i++) if (fabsf(kv[i] - diag_ref_k_[i]) > EXP_DIAG_KNOB_EPS) moved = true;
    if (moved) {
      for (int i = 0; i < 5; i++) diag_ref_k_[i] = kv[i];
      diag_ref_sw1_ = sw1; diag_ref_sw2_ = oct;
      diag_changed_ = true;
      diag_t_ = now;
    } else if (diag_changed_ && now - diag_t_ >= EXP_DIAG_SETTLE_MS) {
      diag_changed_ = false;
      s.n = 0;
      diag_auto_ = s;
      diag_auto_pend_ = true;
    }
  }

  // --- fast trig (copied from armitage, own table) ---
  // SinCosI = the same, always inlined (was the FM LAB DUAL VOICE hot path, 2026-11-02; DUAL removed 2026-11-03).
  static inline void SinCos(float theta, float& s, float& c) { SinCosI(theta, s, c); }
  __attribute__((always_inline)) static inline void SinCosI(float theta, float& s, float& c) {
    using namespace explorer_k;
    static_assert((EXP_SIN_SIZE & (EXP_SIN_SIZE - 1)) == 0, "power of two");
    static_assert(EXP_FM_INDEX + EXP_FBFM_FB_MAX < 90.f,
                  "SinCos offset covers |theta| < ~100 rad");
    constexpr int   kMask = EXP_SIN_SIZE - 1;
    constexpr float kScale = (float)EXP_SIN_SIZE / 6.283185307f;
    const float x  = theta * kScale + 16384.f;
    const int   ix = (int)x;
    const float fr = x - (float)ix;
    const int   i0 = ix & kMask;
    const int   q  = (ix + EXP_SIN_SIZE / 4) & kMask;
    const float s0 = explorer_sin_tab[i0], s1 = explorer_sin_tab[i0 + 1];
    const float c0 = explorer_sin_tab[q],  c1 = explorer_sin_tab[q + 1];
    s = s0 + fr * (s1 - s0);
    c = c0 + fr * (c1 - c0);
  }
  // cos(2 pi x), x >= 0.
  static inline float Cos01(float x) {
    using namespace explorer_k;
    constexpr int kMask = EXP_SIN_SIZE - 1;
    const float xi = x * (float)EXP_SIN_SIZE;
    const int   ix = (int)xi;
    const float fr = xi - (float)ix;
    const int   q  = (ix + EXP_SIN_SIZE / 4) & kMask;
    const float c0 = explorer_sin_tab[q], c1 = explorer_sin_tab[q + 1];
    return c0 + fr * (c1 - c0);
  }
  // sin(2 pi x), x >= 0.
  static inline float Sin01(float x) {
    using namespace explorer_k;
    constexpr int kMask = EXP_SIN_SIZE - 1;
    const float xi = x * (float)EXP_SIN_SIZE;
    const int   ix = (int)xi;
    const float fr = xi - (float)ix;
    const int   i0 = ix & kMask;
    const float s0 = explorer_sin_tab[i0], s1 = explorer_sin_tab[i0 + 1];
    return s0 + fr * (s1 - s0);
  }
  // cos and sin of 2 pi x, x >= 0.
  static inline void CosSin01(float x, float& c, float& s) {
    using namespace explorer_k;
    constexpr int kMask = EXP_SIN_SIZE - 1;
    const float xi = x * (float)EXP_SIN_SIZE;
    const int   ix = (int)xi;
    const float fr = xi - (float)ix;
    const int   i0 = ix & kMask;
    const int   q  = (ix + EXP_SIN_SIZE / 4) & kMask;
    const float c0 = explorer_sin_tab[q],  c1 = explorer_sin_tab[q + 1];
    const float s0 = explorer_sin_tab[i0], s1 = explorer_sin_tab[i0 + 1];
    c = c0 + fr * (c1 - c0);
    s = s0 + fr * (s1 - s0);
  }
  // atan2(y, x) / 2 pi in [0, 1) (armitage FastAtan2Cycles, max error ~0.005 rad).
  static inline float FastAtan2Cycles(float y, float x) {
    const float ax = fabsf(x), ay = fabsf(y);
    const float mx = (ax > ay) ? ax : ay;
    const float mn = (ax > ay) ? ay : ax;
    if (mx <= 0.f) return 0.f;
    const float a  = mn / mx;
    constexpr float k1 = 0.97239411f / 6.283185307f;
    constexpr float k3 = 0.19194795f / 6.283185307f;
    float r = a * (k1 - k3 * a * a);
    if (ay > ax) r = 0.25f - r;
    if (x < 0.f) r = 0.5f - r;
    if (y < 0.f) r = 1.f - r;
    return (r >= 1.f) ? 0.f : r;
  }

  armbands::ArmBand         bands_[kBands];
  armbands::ArmDecimator    dec_;
  armbands::ArmInterpolator int_;

  float mag_[kBands + 2];          // |z_k| at [k + 1] (0 = silent); guards at both ends
  float inv_[kBands];              // 1 / |z_k| (0 = silent)
  float ur_[kBands], ui_[kBands];  // unit phasor of the slot being run
  float ph_[kBands];               // its phase phi_k in cycles [0, 1) (when a running model needs it)
  float wmid_[kBands];             // played-slot fitted output weight (armitage ARM_ALIGN)
  float fp_[kSlots][kBands];       // f'_k per slot (up / played / down)
  float ifp_[kSlots][kBands];      // 1 / f'_k
  uint8_t wtlv_[kSlots][kBands], wtlvf_[kSlots][kBands];   // dampener: wavetable level in use / before the last re-pick
  uint8_t btsc_[kSlots][kBands], btsf_[kSlots][kBands];    // dampener: BITTAB steps in use / before the last re-pick
  uint8_t wt_lv_of_h_[explorer_k::EXP_TB_HMAX + 1];        // largest version index with LEVEL_H <= h
  uint8_t tblo_[kSlots][kBands];   // EXP_TB_BLEND: lower table version lo per slot / band (hi = lo + 1)
  float   tbx_[kSlots][kBands];    // EXP_TB_BLEND: blend weight x toward hi (0 = lo only), per block (Repick)
  int     tb_off_[explorer_k::EXP_TB_ZONES] = {};          // MID tables: packed frame offset per zone (BuildTables)
  float bt_all_[explorer_k::EXP_BT_WAVES][explorer_k::EXP_BT_STEPS] = {};   // BITTAB waveforms

  // Model state per slot and band
  float pm_[kSlots][kBands], pp_[kSlots][kBands];   // FM: modulator phase, previous band phase (-1 = fresh)
  float m1_[kSlots][kBands], m2_[kSlots][kBands];   // FM: last two modulator outputs (self-feedback)
  float th_[kRotN][kSlots][kBands];                  // frequency / interval shift: rotation phase (cycles)
  bool  slot_on_[kSlots] = { false, false, false };   // slot computed last sample

  Params pn_, pf_;                 // current / fading-out model parameters (this sample)
  float dc_a_ = 0.f, dc_x1_ = 0.f, dc_y1_ = 0.f;
  float sr8_ = 8000.f, sr_ = 48000.f;
  float ctrl_a_ = 0.f;
  int   var_ = 0, vfrom_ = 0;      // model variant (model << 6 | item): current / fading out
  volatile int var_t_ = 0;         // target variant (Controls)
  int   mfade_n_ = 0, mfade_len_ = 8;
  int   zfade_len_ = 8;            // MID table zone change fade (EXP_TB_ZONE_XFADE_MS) in 8 kHz samples
  int   mfade_cur_ = 8;            // length of the running fade (mfade_len_ or zfade_len_)
  float mg_ = 1.f;                 // new-model gain during a fade
  float k1s_ = 0.f, k2s_ = 0.f, k3s_ = 0.f;           // smoothed knobs (K1: unused)
  volatile float k1_t_ = 0.f, k2_t_ = 0.f, k3_t_ = 0.f;   // targets
  volatile int rfmc_i_t_ = 0, ish_t_ = 0;   // snapped items: FM ratio index, interval semitones
  DSlide rfmc_, ish_;              // their slid values
  int   disc_len_ = 8;             // EXP_DISC_SLIDE_MS in 8 kHz samples
  float og_[3] = { 0.f, 1.f, 0.f };            // slot weights (up, played, down), slid
  volatile float og_t_[3] = { 0.f, 1.f, 0.f };
  float oct_step_ = 0.f;
  // K4 post filter
  ExpSVF svf_[2];
  float ft_ = 0.f;                 // smoothed t (per block)
  volatile float ft_t_ = 0.f;      // target t = 2 k4 - 1
  volatile bool filt_off_ = false; // SW1 UP: post filter faded out / not run
  // K4 / K5 envelope modulation
  const bool loop_;                // retired LOOPING variant (SW3 DOWN, only with EXP_LOOP_ENABLE)
  const bool lab_;                 // SW3 DOWN = FM LAB (ONE-SHOT, own SW1 families: UP 2-OP, MID SPECTRAL, DOWN ADDITIVE)
  // FM LAB (lab_ only)
  volatile int lab_m_t_ = -1;      // 2-OP mode (K1: 0 HARMONIC, 1 DISHARMONIC, 2 FREE; -1 = MID / DOWN, r rests), Controls
  volatile int lab_st_t_ = 7;      // 2-OP ratio zone (K2, 0..14; HARMONIC / DISHARMONIC), Controls
  DSlide l2r_;                     // slid 2-OP ratio r
  float lab_fbf_ = 1.f;            // K5 LFO fade-in factor on b (touch env: 1 - w (1 - o_r); slid 1 ms)
  float lab_ue_c_ = -1.f;          // u_eff of the last LabFmUpdate (-1 = recompute)
  float lab_L_ = 4000.f;           // 2-OP caps' L = min(EXP_FOLD_LIMIT_HZ, LabFc(u_eff)) (Hz)
  int   diag_lab_z_ = -1;          // DIAG: lab UP mode of the last Controls (-1 = not the lab UP)
  float diag_lab_b_ = 0.f, diag_lab_ue_ = 0.f, diag_lab_lfd_ = 0.f;   // DIAG: b at the knob, u_eff now, LFO depth x fade
  int   diag_lab_side_ = 0;        // DIAG: K3 side (-1 hollow, 0 sine, +1 feedback)
  // FM LAB SPECTRAL (SW1 MID)
  float spec_w_[explorer_k::EXP_SPEC_M + 1] = {};   // partial weights W(n), n = 1..M (RMS-normalised)
  float sp_tilt_[explorer_k::EXP_SPEC_M + 1] = {};  // n^-gamma
  float sp_comb_[explorer_k::EXP_SPEC_M + 1] = {};  // comb(n)
  float sp_y_[explorer_k::EXP_SPEC_M + 1] = {};     // comb position y(n) = n - 1
  float sp_ln_[explorer_k::EXP_SPEC_M + 1] = {};    // ln n
  float sk_g_ = -1.f, sk_nu_ = -1.f, sk_e_ = -1.f;  // inputs the parts were made for
  bool  sp_redo_ = true;           // first SpecWeights: build W
  volatile int spec_bank_t_ = explorer_k::EXP_SPEC_BANK_HARM;   // K1 bank target (0..8, Controls)
  int   spec_bank_ = explorer_k::EXP_SPEC_BANK_HARM;            // bank in use (swapped at g = 0)
  float sp_bg_ = 1.f;              // bank-change gain g on the normalised W
  float sp_pstep_ = 0.25f;         // g slide step per 8 kHz sample (2 / disc_len_)
  float sp_isr8_ = 1.f / 8000.f;   // 1 / sr8
  float sk_k1_ = -1.f, sp_e_ = 1.f;  // K1 the comb exponent e was computed for, e
  float sp_ln_e_ = 0.f;            // ln(E_MAX / E_MIN)
  int   diag_spec_z_ = -1;         // DIAG: 0 = SPECTRAL in the last Controls, -1 = not
  float diag_spec_n_ = 0.f;        // DIAG: K1 bank index at the knob
  float diag_spec_nu_ = 0.f;       // DIAG: applied density nu (written by SpecWeights)
  float sp_nu_last_ = -1.f;        // nu of the previous SpecWeights call (stop detection)
  int   sp_nu_div_ = 0;            // samples until the next comb recompute while nu moves
  // SPECTRAL density travel (SpecTravel, while kLSPEC runs)
  float sk_Td_ = -1.f, sk_Tr_ = -1.f;  // T_d / T_r the depths were made for
  float sp_dd_ = 0.f, sp_dr_ = 0.f;    // depth_d / depth_r (K2 knob units)
  volatile float sp_tr_ = 0.f;     // density travel (K2 knob units): (1 - w) depth_d (1 - o_d) + w depth_r (1 - o_r)
  // K4 TOUCH ENV (docs/DESIGN_PHILOSOPHY.md §12), per instance. TouchEnvUpdate (every 8 kHz sample, ONE-SHOT): the
  // pluck / swell envelopes
  volatile float te_gd_ = 1.f;     // DECAY g_d (onset -> 1, -> 0 over T_d, holds 0; 1 before the first onset)
  float te_gds_ = 1.f;             // g_d slid (EXP_PS_SLIDE_MS one-pole, ps_a_)
  float te_xd_ = 1.f;              // DECAY 2-pole first stage
  bool  te_drun_ = false;          // decay running (an onset started it, not yet at 0)
  volatile float te_gr_ = 1.f;     // RISE g_r (onset -> 0, -> 1 over T_r, holds 1; 1 before the first onset)
  float te_grs_ = 1.f;             // g_r slid (EXP_PS_SLIDE_MS one-pole, ps_a_)
  float te_t4_ = 2.f;              // t4 the times / w were computed for (2 = force)
  volatile float te_Td_ = -1.f;    // decay time T_d (s)
  volatile float te_Tr_ = -1.f;    // rise time T_r (s)
  volatile float te_w_ = 0.f;      // blend w: 0 = decay only (t4 <= 0) .. 1 = rise only (t4 >= EXP_SPEC_MORPH_ZONE)
  float te_cd_ = 0.f, te_dgd_ = 0.f;   // DECAY coefficient (2-pole per stage / one-pole), linear step (EXP_PS_DEC_LINEAR)
  float te_cr_ = 0.f;              // RISE one-pole coefficient (tau T_r / EXP_ENV_TRAVEL_TAUS)
  // TouchOpenUpdate (K4 DYNAMICS, while a K4 mode runs): openness o, read by every K4 target of the running model
  float te_LdT_ = -1.f;            // T_d the L_d fall coefficient was made for
  float te_Ld_ = 0.f, te_Ld_a_ = 1.f;  // decay side's damped live level L_d and its fall coefficient
  bool te_Ld_init_ = true;          // next run: L_d = L (after a pause / looping)
  float te_od_ = 1.f, te_or_ = 1.f;    // pluck openness o_d = min(g_ds, L_d), swell openness o_r = g_rs G
  volatile float te_o_ = 1.f;      // openness o = (1 - w) o_d + w o_r (1 = the K3 value, 0 = the bottom / fully moved)
  volatile float te_L_ = 0.f;      // live level L (0..1; DIAG)
  float te_flin_ = 0.f, te_lnfl_ = 0.f;   // floor level (linear, ln) of EXP_SPEC_DYN_FLOOR_DB
  float te_iLr_ = 0.f, te_iGr_ = 0.f;     // L / G per neper above the floor
  // FM LAB ADDITIVE: weight caches (0 = current side, 1 = fading side), keys, STRETCH rho_m, ln m
  float add_w_[2][explorer_k::EXP_ADD_K_MAX + 1] = {};
  float add_rho_[2][explorer_k::EXP_ADD_STR_M + 1] = {};
  float add_ln_[explorer_k::EXP_ADD_K_MAX + 1] = {};
  int   add_ks_[2] = { -1, -1 }, add_top_[2] = { 1, 1 };
  float add_kh_[2] = { -1.f, -1.f }, add_kd_[2] = { -1.f, -1.f };
  int   diag_add_z_ = -1;          // DIAG: ADDITIVE zone of the last Controls (-1 = not ADDITIVE)
  float diag_add_c3_ = 0.f;        // DIAG: ADDITIVE K3 parameter at the knob
  int   diag_add_bm_ = 0;          // DIAG: ADDITIVE unison beat mode (0 off, 1 prop, 2 Hz)
  float k4s_ = 0.5f, k5s_ = 0.5f;  // smoothed K4 (t4 = 2 k4s_ - 1), K5 (t5 = 2 k5s_ - 1)
  volatile float k4_t_ = 0.5f, k5_t_ = 0.5f;
  // K3 dampener (UP caps per 8 kHz sample, DampFmUpdate; table picks per block, DampUpdate)
  float damp_k_ = -1.f;            // smoothed K3 the ceilings / picks were computed for
  float damp_l_ = 0.f;             // K5 LFO value the FM cutoff was last computed for
  float damp_ld_ = 0.f;            // SW3 MID FM: pluck-side LFO octaves before the K3 scale (change detect)
  float damp_o_ = 0.f;             // ... and the K3 envelope value o3_
  float pick_fc_[kSlots] = { -1.f, -1.f, -1.f };   // tables' fc each slot's picks were made for (-1 = none)
  float tpk_k_ = -1.f;             // smoothed K3 the tables' fc was computed for
  float tpk_fc_ = 4000.f;          // tables' dampener fc (Hz): DampFc(smoothed K3), not modulated (Repick)
  float damp_fc_ = 4000.f;         // UP fc (Hz, incl. the K3 envelope / K4 touch env)
  float damp_lnr_ = 0.f;           // ln(DAMP_FMAX / DAMP_FMIN)
  float hc_fold_ = 4000.f;         // min(EXP_FOLD_LIMIT_HZ, fc): L of the UP caps
  float hc_fold_k_ = 4000.f;       // the same from the K3 knob alone (MID: the envelope's index floor)
  int   pk_fade_n_ = 0, pk_fade_len_ = 8; // discrete pick slide: samples left / length (EXP_DAMP_SLIDE_MS)
  float pk_g_ = 1.f;               // new-pick weight this sample
  // Parked SW1 MID K3 ladder lowpass (LadUpdate per 8 kHz sample, Ladder per block; EXP_LAD_ENABLE)
  static constexpr int kLadSub = 64;   // 8 kHz samples per audio block held (blocks up to 384 frames)
  MoogLadder lad_;                 // Mode A's ladder (src/core/blocks/moog_ladder.h, as is)
  float lad_m_ = 0.f;              // mix m (0 = dry path only, ladder not run; 1 = ladder only)
  float lad_step_ = 0.f;           // m slide per 48 kHz sample (1 / EXP_LAD_XFADE_MS)
  float lad_lnr_ = 0.f;            // ln(EXP_LAD_FMAX / EXP_LAD_FMIN)
  float lad_set_fc_ = 8000.f;      // cutoff last passed to SetCutoff
  volatile float lad_fc_ = 8000.f; // effective cutoff (Hz), latest computed (DIAG)
  float lad_fcb_[kLadSub];         // cutoff per 8 kHz sample of the current block
  bool  lad_tgb_[kLadSub];         // mix target per 8 kHz sample (true = MID table model)
  // SW1 MID K3 2-pole lowpass (TfUpdate per 8 kHz sample, TabFilter per block; m slide = lad_step_)
  float tf_set_q_ = 0.f;           // Q the SVF coefficients were last set for
  ExpSVF sp_svf_;                  // FM LAB SPECTRAL K3 LPF (own instance, same type as tf_svf_)
  float sp_m_ = 0.f, sp_set_fc_ = 0.f, sp_fc_ = 0.f;   // mix, fc the coefficients hold, DIAG fc (0 = off)
  float sp_fcb_[kLadSub] = {};
  bool  sp_tgb_[kLadSub] = {};
  ExpSVF tf_svf_;                  // one TPT SVF stage, LP output (Q EXP_TF_Q)
  float tf_m_ = 0.f;               // mix m (0 = not run; 1 = LP only)
  float tf_lnr_ = 0.f;             // ln(EXP_TF_FMAX / EXP_TF_FMIN)
  float tf_lnb_ = 0.f;             // ln(EXP_TF_ENV_BOTTOM / EXP_TF_FMIN): the one-shot envelope's bottom
  float tf_set_fc_ = 16000.f;      // cutoff the coefficients were last set for
  volatile float tf_fc_ = 16000.f; // effective cutoff (Hz), latest computed (DIAG)
  float tf_fcb_[kLadSub];          // cutoff per 8 kHz sample of the current block
  bool  tf_tgb_[kLadSub];          // mix target per 8 kHz sample (true = MID table model)
  float dfc_diag_ = 0.f;           // DIAG: the tables' dampener fc at the knob (Controls -> DiagControls)
  float on_env_ = 0.f, on_gate_ = 0.f, on_base_ = 0.f;   // fast meter, gate meter, slow baseline (48 kHz)
  float on_env_a_ = 0.f, on_fall_a_ = 0.f;               // meter rise / gate fall coefficients (48 kHz)
  int   on_refr_ = 0, on_refr_len_ = 4800;               // refractory samples left / length (48 kHz)
  bool  on_pending_ = false;                             // an onset fired, not yet consumed by EnvUpdate()
  bool  on_seen_ = false;                                // an onset has happened
  ExpEnv env_;                     // envelope e (ONE-SHOT / LOOPING by loop_)
  float mv_ = 0.f;                 // applied v = dir e (gated), slid (1 ms one-pole, 8 kHz)
  float mv_a_ = 0.f;               // its coefficient (tau EXP_ENV_FM_SLIDE_MS)
  float o2_ = 0.f, o3_ = 0.f;      // LOOPING: envelope offsets on the K2 / K3 parameter (knob units, 1 = full
                                   // span); ONE-SHOT: o2_ = 0, o3_ = the K4 touch env openness o (fc scale, 1 = the K3
                                   // value; 1 while an SW3 MID SW1 DOWN model runs)
  float ln_tau_ratio_ = 0.f;       // ln(EXP_ENV_TAU_FAST_S / EXP_ENV_TAU_SLOW_S)
  float ln_loop_ratio_ = 0.f;      // ln(EXP_LOOP_T_FAST_S / EXP_LOOP_T_SLOW_S)
  // RETIRED ONE-SHOT K4 pluck <-> swell (PsUpdate, not called since 2026-11-05: nothing reads ps_g_ / ps_gs_ / ps_T_);
  // ps_a_ (EXP_PS_SLIDE_MS) and ln_ps_dec_ / ln_ps_rise_ are still read by the touch env
  volatile float ps_g_ = 1.f;      // g (0 = bottom fc, 1 = the K3 value)
  float ps_gs_ = 1.f;              // g slid (EXP_PS_SLIDE_MS one-pole, coefficient ps_a_)
  float ps_a_ = 0.f;               // its coefficient (tau EXP_PS_SLIDE_MS)
  float ps_t4_ = 2.f;              // t4 the side / T / coefficient were computed for
  bool  ps_rise_ = true;           // side: true RISE (t4 >= 0, target 1), false DECAY (t4 < 0, target 0)
  float ps_Ts_ = 0.01f;            // travel time T_side (s) of the current side from |t4|
  volatile float ps_T_ = 0.01f;    // travel time in use T_eff (s): T_side, or max(T_side, EXP_PS_LEAVE_S) unplucked
  bool  ps_plucked_ = false;       // an onset reset g on this side (false after a side change, until the next onset)
  float ps_c2_ = 0.f, ps_x1_ = 1.f; // DECAY 2-pole: per-stage coefficient, first stage
  float ps_c_ = 0.f;               // RISE one-pole coefficient (tau = T_eff / EXP_ENV_TRAVEL_TAUS)
  float ps_dg_ = 0.f;              // DECAY linear step per 8 kHz sample (1 / (T_eff sr8))
  float ln_ps_dec_ = 0.f;          // ln(EXP_PS_DEC_SLOW_S / EXP_PS_DEC_FAST_S)
  float ln_ps_rise_ = 0.f;         // ln(EXP_PS_RISE_SLOW_S / EXP_PS_RISE_FAST_S)
  // ONE-SHOT K5 LFO (LfoUpdate, loop_ = false)
  float ln_lfo_ = 0.f;             // ln(EXP_LFO_HZ_MAX / EXP_LFO_HZ_MIN)
  float ln_lfo_ccw_ = 0.f;         // ln(EXP_LFO_HZ_MAX_CCW / EXP_LFO_HZ_MIN)
  float lfo_u_ = -1.f;             // u = |t5| the rate / depth were computed for
  float lfo_inc_ = 0.f;            // phase increment per 8 kHz sample
  float lfo_depth_ = 0.f;          // min(u / EXP_LFO_DEPTH_FRAC, 1)
  float lfo_ph_ = 0.f;             // free-running phase [0, 1)
  uint32_t lfo_rng_ = 0u;          // S&H LCG state
  float lfo_sh_ = 0.f;             // held random value (-1..+1), new at each cycle start
  float lfo_s_ = 0.f;              // applied l = depth shape, slid (1 ms, lands exactly)
  // Live follower (parked, EXP_FOLLOW_ENABLE)
  EnvFollower fol_;                // rectifier + 4-pole LP, 48 kHz raw input
  float fol_y_ = 0.f;              // follower output, latest 48 kHz sample
  float fol_f_ = 0.f;              // f = min(fol_y_, 1), per 8 kHz step
  float f_m_ = 0.f, f_step_ = 0.f; // dry -> filtered mix and its per-sample slide
  float f_lnhp_ = 0.f, f_lnlp_ = 0.f;
  float f_a_ = 0.f;
  int   f_sz_ = 0;
  bool  f_on_ = false, f_hp_ = false;
  volatile bool mute_ = false;
  float mute_g_ = 1.f, mute_step_ = 0.f;

  // DIAG (main loop only)
  DiagSnap diag_tap_{}, diag_auto_{};
  bool     diag_tap_pend_ = false, diag_auto_pend_ = false;
  int      diag_n_ = 0;
  bool     diag_blink_ = false;
  uint32_t diag_blink_t_ = 0;
  bool     diag_changed_ = true;
  uint32_t diag_t_ = 0;
  float    diag_ref_k_[5] = { -1.f, -1.f, -1.f, -1.f, -1.f };
  int      diag_ref_sw1_ = -1, diag_ref_sw2_ = -1;
};
