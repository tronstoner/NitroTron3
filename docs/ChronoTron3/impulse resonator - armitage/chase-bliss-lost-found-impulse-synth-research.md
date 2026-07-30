## What Chase Bliss actually confirms

Chase Bliss describes the **Impulse Synthesizer** as:

> “A polyphonic sub-octave synthesizer built from a network of vibrating strings”

and says it is best suited to **chords and slower playing**. Its controls are portamento, texture, and a combined attack/release control. The manual also explains that the filter is deliberately relaxed: it closes only when the pedal detects no input signal. ([cb2k22.squarespace.com](https://cb2k22.squarespace.com/s/Lost-Found_Manual_Pedal_Chase-Bliss.pdf))

That wording is important: Chase Bliss does **not explicitly call the Impulse Synthesizer a “polyphonic pitch-tracking” effect**. The adjacent **Sympathetic Resonator** is explicitly described that way, including its bank of chromatically tuned pipes. ([cb2k22.squarespace.com](https://cb2k22.squarespace.com/s/Lost-Found_Manual_Pedal_Chase-Bliss.pdf))

So the strongest official distinction is:

- **Impulse Synthesizer:** polyphonic, sub-octave, “network of vibrating strings.”
- **Sympathetic Resonator:** polyphonic, pitch-tracking, chromatically tuned resonators.

Chase Bliss has not published the DSP topology, detector type, number of voices, analysis-window size, FFT resolution, or voice-allocation method. I also found no developer statement identifying a licensed pitch-detection library, neural model, or specific algorithm.

## My best reconstruction

Your observation that it recognises chords best when they are struck together, but does not build them from arpeggiated notes, is the most revealing clue.

The Impulse Synthesizer probably does **not** work like six independent monophonic pitch trackers. It more likely uses a **transient-triggered spectral snapshot**:

```text
guitar input
    ↓
transient/onset detector
    ↓
short analysis window
    ↓
spectral or pitch-class analysis
    ↓
select several simultaneous pitch candidates
    ↓
quantise/stabilise them
    ↓
transpose down an octave
    ↓
retune/excite a bank of string-like resonators
    ↓
slow filter/amplitude envelope
```

The central idea is that it identifies a **set of pitches present during one onset event**, rather than maintaining a persistent note list to which later notes are added.

### Why a single strummed chord works

A guitar strum is not literally simultaneous, but the strings may all arrive within perhaps a few tens of milliseconds. A sufficiently broad analysis window can treat that as one event:

```text
E string ──┐
A string  ──┤  all fall inside one analysis frame
D string   ─┤  → interpreted as one spectral object/chord
G string    ┘
```

The detector can inspect the combined spectrum and initialise several resonant voices at once.

This is relatively easy compared with continuous polyphonic transcription. It does not need to solve note identities indefinitely; it only has to make a plausible chord estimate around the attack.

### Why arpeggios fail

With an arpeggio, each note arrives outside that initial snapshot:

```text
note 1 ───────
        note 2 ───────
                note 3 ───────
```

A conventional polysynth would allocate a new voice for each note. But this algorithm may instead do one of the following:

1. **Replace the entire target spectrum** with each new attack.
2. Glide the existing resonator bank towards the newly detected state.
3. Ignore weaker later notes while the current synth envelope remains active.
4. Wait for the input to fall below a threshold before accepting a completely new event.

Any of those would produce the behaviour you describe: a chord can be captured as a unit, but it cannot be assembled one note at a time.

The portamento control particularly supports the idea of a **global resonator-state transition**. Chase Bliss says it controls how quickly “the synth glides between notes,” and recommends higher settings for sprawling transitions. That could mean interpolation between two detected groups of resonant frequencies rather than normal per-note portamento. ([cb2k22.squarespace.com](https://cb2k22.squarespace.com/s/Lost-Found_Manual_Pedal_Chase-Bliss.pdf))

## Is it genuinely tracking individual pitches?

Probably to some extent, but “polyphonic pitch tracking” can mean several different things.

### Full note transcription

This would produce something equivalent to:

```text
C3 on
E3 on
G3 on
C3 off
...
```

That requires voice allocation, onset detection, note continuation, octave-error handling and separation of overlapping harmonics. The Lost + Found’s playing limitations suggest it is **not doing this**, or at least not exposing the result in that form.

### Spectral peak extraction

A much simpler system can find prominent peaks in a short FFT spectrum and organise them into plausible fundamentals. For example, it could:

- calculate a magnitude spectrum;
- identify strong peaks;
- fold energy into pitch classes or candidate fundamentals;
- suppress candidates that are probably harmonics of stronger notes;
- select perhaps three to six candidates;
- snap them to a pitch grid;
- initialise resonators at corresponding sub-octave frequencies.

This would be enough to appear polyphonic on clean, simultaneously played chords while behaving badly on arpeggios, sustained tones and complex distortion.

### Chromagram or semitone filter bank

Another plausible implementation is a fixed bank of detectors corresponding to chromatic pitches:

```text
C  C♯  D  D♯  E  F  F♯  G  G♯  A  A♯  B
```

Energy from several octaves can be accumulated or compared. Active bins then control the string resonators.

This is computationally predictable, naturally quantised, and fits a pedal better than elaborate real-time transcription. It would also explain a slightly “locked” or chromatic response.

However, **chromatic quantisation is confirmed only for the Sympathetic Resonator**, not for the Impulse Synthesizer. For Impulse, it remains a good inference rather than documented fact. ([cb2k22.squarespace.com](https://cb2k22.squarespace.com/s/Lost-Found_Manual_Pedal_Chase-Bliss.pdf))

## What “network of vibrating strings” probably means

That phrase strongly suggests some form of **physical or modal synthesis**, not just conventional oscillators.

The likely candidates are:

- coupled digital waveguides;
- Karplus–Strong-style delay loops;
- modal resonators;
- highly resonant comb filters;
- a hybrid network containing several of these.

A basic digital string can be represented as a short feedback delay:

```text
excitation → delay line → damping filter ─┐
              ↑                          │
              └──────── feedback ────────┘
```

Its pitch is primarily determined by delay length. Several such models can be coupled or summed to create a dense string ensemble. “Texture” could then alter damping, coupling, dispersion, excitation density, detuning, or the number and balance of partial resonators.

An alternative is a bank of modal filters. Modal synthesis creates an object-like sound by exciting many narrow resonances; similar public resonator designs describe parallel banks of narrow resonant filters as “modal synthesis.” ([aavepyora.online](https://aavepyora.online/poly-modal-impulse-resonator-synth-for-bitwig-2-5/?utm_source=chatgpt.com))

The term **network** is suggestive of interaction between the strings rather than six completely separate oscillators. That could contribute to its huge, unstable, cinematic character.

## Why ringing single notes behave strangely

The manual says the attack/release control governs a filter and that this filter closes only when **no sound is detected at the input**. This is not a normal synth envelope following every picked note. ([cb2k22.squarespace.com](https://cb2k22.squarespace.com/s/Lost-Found_Manual_Pedal_Chase-Bliss.pdf))

Users independently report that:

- the synth has a definite trigger threshold;
- playing softly may not activate it;
- muting does not always stop it immediately;
- the swell can continue until its internal envelope has finished;
- returning input can interrupt or postpone the release. ([reddit.com](https://www.reddit.com/r/chaseblissaudiophiles/comments/1qrf5qb/tipstechniques_for_the_lostfound_synth_mode_5a/))

That suggests two partly separate systems:

```text
pitch/chord analysis: mostly around attacks
gate/filter control: based on broader input-presence detection
```

A sustained ringing note may therefore keep the filter open without providing a sufficiently distinct new onset to reanalyse or re-excite the string network. In practical terms, the pedal knows that **something is still sounding**, but does not necessarily treat it as a new playable synth note.

This also explains why the response feels unlike a normal guitar synth. The detector is controlling the **configuration and excitation of a resonant system**, while a comparatively slow gate controls whether that system is audible.

## What the forum discussion establishes

There is surprisingly little serious reverse-engineering discussion. Most community conversation focuses on how difficult the mode is to control rather than its pitch detector.

The most useful thread confirms the same behavioural characteristics:

- a threshold below which the synth does not trigger;
- long, somewhat autonomous envelopes;
- slow playing being necessary;
- input noise or preceding effects potentially preventing release;
- the mode working more like a deliberately self-sustaining pad than a responsive conventional synth. ([reddit.com](https://www.reddit.com/r/chaseblissaudiophiles/comments/1qrf5qb/tipstechniques_for_the_lostfound_synth_mode_5a/))

I found no credible forum post from a Chase Bliss developer disclosing the tracking algorithm. There are also no convincing indications that it uses machine learning. A traditional onset detector plus FFT/filter-bank analysis would be cheaper, deterministic, low latency, and entirely sufficient for this behaviour.

## Most likely conclusion

I would describe it as:

> **A transient-driven polyphonic resonator synth that takes a short spectral snapshot of a chord, reduces that spectrum to several stabilised or quantised pitch targets, transposes them downward, and uses them to retune and excite a physical-modelled string network.**

It is probably **polyphonic at the moment of analysis**, but not polyphonic in the MIDI-synth sense of maintaining independently gated voices.

The simultaneous-strum preference is therefore probably not a defect in otherwise normal tracking. It is likely fundamental to the design: the input is treated as an **impulse containing a chord spectrum**, rather than as a chronological stream of note-on and note-off events.
