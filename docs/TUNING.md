# Tuning Mode — Specification

## Purpose

The pedal has no display. During development we need to dial in internal constants (oscillator curve, filter character, envelope threshold, gain staging) by ear against a real bass signal, then commit those values to the source code.

Tuning mode solves this by temporarily repurposing the hardware controls as tuning knobs, printing values over USB serial for capture, and persisting work-in-progress values to a dedicated flash slot so sessions can be resumed.

## Design Principles

1. **Tune in context, not in isolation.** The oscillator gets rough-tuned alone in Stage 1, but final tuning happens in Stage 3 once envelope follower and filter are in the chain. Tuning mode persists from Stage 1 onward so this is painless.
2. **No reflash between tune and test.** A/B knob values against live playing in seconds, not minutes.
3. **Same binary, dev and production.** Tuning mode coexists with normal operation. No `#ifdef` flips, no separate build.
4. **Dormant when not in use.** Zero impact on normal-mode behavior or CPU budget.

---

## Entry and Exit

**Enter tuning mode:** hold both footswitches for 2 seconds. LED fast double-blink confirms entry.

**Exit tuning mode:** hold both footswitches again for 2 seconds. LED slow single-blink confirms exit. Values on the current page are *not* auto-committed on exit — explicit serial print or long-press save is required to capture work.

**Accidental entry protection:** the 2-second hold requires both footswitches pressed simultaneously. Single-switch stomps during performance cannot trigger it.

---

## Control Remapping in Tuning Mode

| Control | Normal Mode | Tuning Mode |
|---|---|---|
| Toggle 3 | Mode select | **Tuning page select** (1 / 2 / 3) |
| Toggle 1 | Waveform | Waveform (still active — audition tuning against saw/tri/sq) |
| Toggle 2 | Preset select | *(unused)* |
| Knobs 1–6 | Per-mode UX | **Tuning parameters per active page** |
| Footswitch 1 | Bypass | Bypass (unchanged — still useful during tuning) |
| Footswitch 2 short | Recall preset | **Print current page values over USB serial** |
| Footswitch 2 long | Save preset | **Save current page values to dev-tuning flash slot** |

---

## Tuning Pages (Mode A — Bordun)

### Page 1 — Oscillator Character

| Knob | Parameter | Range | Default | Notes |
|---|---|---|---|---|
| K1 | `k` (parabolic curve) | 0.0 – 0.6 | 0.35 | Fundamental boost. 0 = linear saw, 0.5+ = very round bass |
| K2 | `dcTrim` | -0.05 – 0.05 | 0.0 | Fine DC centering after parabolic shaping |
| K3 | `foldAmt` (tri-core variant) | 0.0 – 0.3 | 0.15 | Only active if tri-core variant is compiled in |
| K4 | `oscPeakGain` | 0.0 – 1.5 | 1.0 | Pre-filter oscillator trim |
| K5 | *(reserved)* | — | — | Future parameter |
| K6 | *(reserved)* | — | — | Future parameter |

**Tuning tip:** set `k` by ear at low frequencies (audition at C1–E1). Target is a slightly weaker 2nd harmonic than a pure saw (which sits at −6 dB relative to fundamental). Trust bass weight over visual spectrum analysis. Audition across all three waveforms via Toggle 1 — `k` affects saw most, triangle minimally, square not at all, so a single `k` that works for all waveforms is the goal.

### Page 2 — Envelope Follower

| Knob | Parameter | Range | Default | Notes |
|---|---|---|---|---|
| K1 | Follower LP cutoff (Hz) | 15 – 80 | 33 | Moog-canonical is 33 Hz (4-pole). Lower = smoother but sluggish |
| K2 | Threshold gate | 0.0 – 0.3 | 0.02 | Below this, oscillator is muted. Prevents bleed at silence |
| K3 | Input pre-gain | 0.5 – 4.0 | 1.0 | Gain before rectifier. Different for passive vs active bass |
| K4 | Attack bias | 0.5 – 2.0 | 1.0 | Asymmetry in filter response. 1.0 = symmetric |
| K5 | Release bias | 0.5 – 2.0 | 1.0 | Same, release side |
| K6 | *(reserved)* | — | — | Future parameter |

**Tuning tip:** K1 is the single most important parameter. The Moog 33 Hz value is a reference point, not a mandate — on a bright bass or with low-tuned strings you may want 25–28 Hz for smoother tracking. Play staccato notes and listen for oscillator "chatter" (cutoff too high) vs "drag" (cutoff too low).

### Page 3 — Stage / Mix / Ladder

| Knob | Parameter | Range | Default | Notes |
|---|---|---|---|---|
| K1 | `OSC_GAIN` | 0.0 – 1.5 | 0.7 | Final osc level before mix stage. Calibrates perceptual match with dry at Mix=50% |
| K2 | Ladder drive | 0.5 – 3.0 | 1.0 | Input gain to ladder. Higher = more `tanh` saturation character |
| K3 | Ladder cutoff offset | −0.2 – +0.2 | 0.0 | Trim applied to the Tone knob's range |
| K4 | Dry trim | 0.8 – 1.2 | 1.0 | Trim dry path to match oscillator level in mix |
| K5 | *(reserved)* | — | — | Future parameter |
| K6 | *(reserved)* | — | — | Future parameter |

**Tuning tip:** tune K1 (`OSC_GAIN`) last, after pages 1 and 2 are dialed. Set Mix to 50% and play real bass lines — `OSC_GAIN` is right when oscillator and dry feel like equal partners, not when they measure equal in dB.

---

## Workflow: Dial, Capture, Commit

### During a tuning session

1. Enter tuning mode (both footswitches, 2 s).
2. Select page via Toggle 3.
3. Adjust knobs while playing. Toggle 1 switches waveform to audition across saw/tri/sq.
4. **Short-press FS2** to print current page values over USB serial. Do this repeatedly as you converge — the terminal log becomes your tuning history.
5. **Long-press FS2** to save current page to the dev-tuning flash slot. This persists across power cycles so multi-session tuning is possible.
6. Exit tuning mode to play-test the new values in normal context. Re-enter to refine.

### When values are locked in

1. Connect Daisy over USB. Open serial monitor: `screen /dev/tty.usbmodem*` on macOS, or the Arduino IDE serial monitor at 115200 baud.
2. In tuning mode, short-press FS2 on the page you're committing. Copy the printed line.
3. Paste into `constants.h` (or wherever the constants live), replacing the old values.
4. Rebuild, reflash.
5. The dev-tuning flash slot can be cleared or left alone — it's only consulted when tuning mode is active.

### Serial print format

One line per press, structured for easy grep and copy-paste:

```
[TUNE] page=1 k=0.382 dcTrim=0.000 foldAmt=0.150 oscPeakGain=1.050
[TUNE] page=2 folCutoff=31.2 thresh=0.018 preGain=1.250 attBias=1.000 relBias=1.000
[TUNE] page=3 oscGain=0.680 ladderDrive=1.400 ladderOfs=0.000 dryTrim=1.000
```

A commented `constants.h` snippet is printed alongside for direct paste:

```
// --- paste into constants.h ---
constexpr float OSC_K            = 0.382f;
constexpr float OSC_DC_TRIM      = 0.000f;
constexpr float OSC_FOLD_AMT     = 0.150f;
constexpr float OSC_PEAK_GAIN    = 1.050f;
```

---

## Dev-Tuning Flash Slot

Separate from the 9 user preset slots. Reserved area in `PersistentStorage`. Stores one snapshot per page.

Loaded on boot only if tuning mode is entered. Normal-mode operation never reads this slot — normal mode uses the compile-time constants from `constants.h`.

This means: your tuning mode session is always available to resume, but the pedal always plays with the compiled-in values. No risk of "the pedal sounds different today" because of stale tuning-mode state leaking into normal operation.

---

## Implementation Sketch

```cpp
enum Mode { NORMAL, TUNING };
enum TuningPage { PAGE_OSC = 0, PAGE_ENV = 1, PAGE_STAGE = 2 };

Mode current_mode = NORMAL;
TuningPage tuning_page = PAGE_OSC;

void HandleControls() {
    // Both footswitches held 2s → toggle mode
    if (both_fs_held_2s()) {
        current_mode = (current_mode == NORMAL) ? TUNING : NORMAL;
        BlinkLED(current_mode == TUNING ? FAST_DOUBLE : SLOW_SINGLE);
        return;
    }

    if (current_mode == TUNING) {
        tuning_page = (TuningPage)toggle3.Read();  // 0/1/2
        ApplyTuningKnobs(tuning_page);

        if (fs2.ShortPress()) PrintPage(tuning_page);
        if (fs2.LongPress())  SaveDevSlot(tuning_page);
    } else {
        ApplyNormalModeControls();  // original per-mode UX
    }
}

void ApplyTuningKnobs(TuningPage page) {
    switch (page) {
        case PAGE_OSC:
            osc.k            = map(knob1, 0.0f, 0.6f);
            osc.dcTrim       = map(knob2, -0.05f, 0.05f);
            osc.foldAmt      = map(knob3, 0.0f, 0.3f);
            osc.peakGain     = map(knob4, 0.0f, 1.5f);
            break;
        case PAGE_ENV:
            env.cutoffHz     = map(knob1, 15.0f, 80.0f);
            env.threshold    = map(knob2, 0.0f, 0.3f);
            env.preGain      = map(knob3, 0.5f, 4.0f);
            env.attackBias   = map(knob4, 0.5f, 2.0f);
            env.releaseBias  = map(knob5, 0.5f, 2.0f);
            break;
        case PAGE_STAGE:
            stage.oscGain    = map(knob1, 0.0f, 1.5f);
            stage.ladderDrv  = map(knob2, 0.5f, 3.0f);
            stage.ladderOfs  = map(knob3, -0.2f, 0.2f);
            stage.dryTrim    = map(knob4, 0.8f, 1.2f);
            break;
    }
}
```

---

## When Tuning Mode Gets Expanded

As Modes B (Sprawl) and C (Schism) are specced, each gets its own set of tuning pages. Toggle 3 continues to select among them — either by growing to more pages (if controls allow) or by scoping to the active mode (tune Mode A's pages while Mode A is selected as current mode, etc.). Decision deferred until Mode B is specced.

---

## Checklist Before Closing a Tuning Session

- [ ] All pages print clean values over serial — no NaN, no stuck-at-extreme knobs.
- [ ] Values captured to `constants.h` and committed to git.
- [ ] Rebuild + flash + play-test in normal mode confirms the compiled values sound the same as the tuning-mode session.
- [ ] Dev-tuning flash slot optionally cleared.
