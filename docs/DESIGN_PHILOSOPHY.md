# Design Philosophy

How this project is built and how agents work in it. Read this before any
work, together with `agents-instructions.md` (git / docs / process rules).
These are the builder's rules, distilled from many sessions. Where a rule has
a reason, the reason matters as much as the rule.

DRAFT 2026-10-03 — consolidated from the agents' notes; open points are marked
**[OPEN]** for the builder to settle.

---

## 1. The ear decides

- The builder's ears are the authority. A spec, a plan, a drawing, a model or
  a theory is context, never the verdict. When live feedback and the spec
  disagree, the feedback wins.
- Nothing that looks right on paper is right until it is heard. Never drop or
  replace something the builder has heard because of how it looks.
- Ask "what are you hearing?", not "the spec says…".
- The builder is an industrial / noise artist. Metallic, inharmonic, clangy,
  harsh, self-oscillating results are usually the point. Don't warn about
  them; don't explain music theory to him.

## 2. No precautionary audio processing

- We solve audio problems **when we hear them**. No filter, limiter,
  normalisation, smoothing, high-pass, "so it doesn't get muddy / too loud"
  stage goes in by default. Precaution engineers away the happy accidents.
- Discussing such an option is fine; building it unasked is not.
- Be cautious about **broken code** (crashes, overruns, unwritten reads,
  non-finite samples, clicks from bugs), never about **the sound**.
- When the builder describes a mechanism, build exactly that, minimally.
  Saturators: drive hard, compensate with a FIXED post trim (never 1/drive —
  that undoes the saturation). "Reacts to dynamics" = the raw level drives
  the nonlinearity; normalising the input kills it.

## 3. Sacrosanct

- **Clean bypass / dry path:** passes completely unaltered, end to end. Never
  process the summed output; any taming acts on the wet signal alone, before
  the sum.
- **The builder's EQ** (and any tone surface he has dialled): never touched to
  fix an unrelated problem. Solve it on the wet output or in a separate stage.
- **Gain staging is relative.** A pedal sits in a chain (bass, guitar, synth).
  There is no absolute "too loud", no output ceiling. Tame feedback relative
  to the signal, inside the loop (e.g. a build-up ducker).
- **Tuned, working DSP** is near-sacrosanct: no "improvements" or refactors
  without a grounded, hardware-verifiable reason, stated up front.
- **Approved values are locked.** When code moves to a new context, keep them
  exactly; don't "compensate" without asking.

## 4. Nothing hidden

- Every choice in a change is shown before it is built: how each value is
  picked (fixed / random / per loop / per event / by position), its range,
  and anything **carried over from older code**. Nothing the builder hasn't
  seen goes in. He can only judge by ear what he knows exists.
- Randomness that changes what a control position plays is always declared
  up front. (Random per-loop rotations were hidden once and cost days; they
  are now wanted — but out in the open.)
- Emergent side effects (a mechanism that audibly does more than was asked)
  are flagged plainly and first, never as a footnote.
- Test tools (benches, simulations, DIAG logs) model what the pedal actually
  does, or say plainly what they don't. A test of something the pedal can't
  play is worthless.

## 5. Discuss, then build

- A **question, a suggestion request or a problem report is not a go.**
  Answer with the diagnosis and the options, then wait. Only a pick ("1, 4,
  5", "do it", "go", "yes" to a concrete proposal) is a go. Momentum is not
  consent.
- When offering options, stop and let him pick — never "I recommend A" and
  apply A in the same turn.
- When he states a requirement or a no-go (e.g. in ratings), it goes into the
  very next build — don't defer it as a "known gap".
- Once a plan is agreed, execute it without re-confirming each step.
- Implement only what is named. Adjacent ideas are offered as one short
  opt-in suggestion, never folded into the diff.
- **[OPEN]** Numeric values inside an agreed change: the notes say both "just
  pick a value, don't ask about trivia" and "every choice is surfaced first".
  Proposed reading: pick it, but name it in the same message as the plan.

## 6. Tuning

- A complaint about amount, speed, length or frequency is a **constant**, not
  a model. Find the dial, move it, name it. Rewrite a mechanism only when he
  says the model is wrong.
- One behavioural change per build, so a regression has one cause — unless he
  pre-batches several related tunings in one message.
- Bold jumps: bracket the range decisively and let his ear pull it back; no
  timid creeping.
- Taper vocabulary: "more detail / finer / more sensitive around noon" = more
  RESOLUTION there (curve exponent > 1), not more effect. Confirm the
  direction in one line when ambiguous.
- Give concrete levers to tune by ear; don't pile on DSP theories or new
  signal-path stages. If the cause is unknown, say so and measure.
- Read the whole signal path before diagnosing; a hidden inherited stage can
  confound every judgement.
- A constant sized for an abandoned model is a bug, not a taste call.

## 7. Rhythm (vestige K3 and similar)

- A Euclidean pattern only means something against a reference (a straight
  pulse or another layer). Layers interlock; no master / slave beat; rhythm
  becomes timbral colour.
- Rests are not a voice: they make a second voice perceivable (ducking, or
  negative space).
- Rotation is part of the rhythm. Stutters land on the 1; rests must stay
  audible (never swallowed by stutters).
- Rolling = cycles that do not divide the bar.
- Never protect / exempt the downbeat (step 1) in glitch placement.
- Add, don't replace: a rhythm he has heard stays selectable.
- To pick patterns, log exactly what plays (DIAG) and let him rate; lock in
  only what he rated.

## 8. Real-world signal

- Input levels are low and instrument-dependent: passive bass ~0.02–0.1,
  guitar ~0.0015–0.03 (fast env). Onset / trigger thresholds default VERY low.
- One firmware for bass and guitar (ChronoTron3): no instrument-specific
  builds or constants; tune agnostically or adaptively.
- Anything that depends on the real instrument (gates, onsets, envelopes):
  log on hardware early; host models (sines) miss real-signal behaviour.

## 9. Faults

- Intermittent fault → DIAG serial log first, read it before naming a cause.
- Never auto-recover from an unexplained fault; observe, don't intervene.
- `isfinite` is not sanity — check magnitude too. Host harnesses can't see
  memory-layout bugs.
- Present a mechanism as a hypothesis until reproduced.

## 10. Working rhythm

- Short replies, one step at a time (ADHD). Confirm intent on pasted content.
- While he tests on hardware, the flashable binary does not change; name the
  exact build (hash) to flash. Minimal messages during tests.
- Discovery phase: no behaviour tests for ideas that may be thrown away; keep
  the safety suite. Write behaviour tests when he locks an idea in.
- Iteration speed matters: when only data changes (e.g. a rhythm set), no
  plumbing in between.
- **[OPEN]** Builds: an older note says "after tuning edits don't build, he
  flashes himself" (NitroTron3 / instrument profiles). On ChronoTron3 we build
  the DIAG firmware and hand over the flash command. Which holds now?

## 11. Process (see agents-instructions.md for the full rules)

- Commit savepoints at every step, also unverified work, stating the
  verification level. Never push; never touch `main`; never bypass signing.
- Never revert or discard work without explicit confirmation; on an
  ambiguous "revert", state target and scope first.
- Docs are updated only when he says so.
