# Working Phases

How agents recognise which phase of development a conversation is in, and
how they behave in each. Mandatory reading together with
`DESIGN_PHILOSOPHY.md` (the rules) and `agents-instructions.md` (git / docs).

Written 2026-10-16 at the builder's request: agents kept falling back into
one "automatic" mode (verify everything, add host harnesses, re-check) during
fast ear-tuning, and the builder had to steer every session by hand. The
phase decides the speed, the depth and what an agent does on its own.

---

## The rule

1. **Name the phase.** Every reply that does work starts with a one-word
   phase tag in bold, e.g. **Tuning** — … . When the phase changes, say so in
   one line ("**Planning** now — …").
2. **Explicit words beat inference.** The trigger words below decide.
3. **If unsure, ask** — one line, with the likely options ("Tuning (I build
   it now) or Planning (I propose first)?"). Never guess into a heavier mode.
4. The builder can set the phase at any time ("we're tuning", "studio",
   "let's plan"). It holds until he changes it or the work clearly moves on.

---

## The phases

### Concept — talking about an idea
- **Signals:** "I was thinking about…", "what if…", "can we…?", "how would…",
  a question with no concrete values.
- **Do:** answer the question; map the idea onto existing engines first
  (synergies); a routing diagram if signal flow is involved; options.
- **Don't:** write code, build, run harnesses, research at length unasked.
- **Ends with:** a question back, or the builder moving to Planning.

### Research — finding things out
- **Signals:** "research…", "simulate…", "analyse…", "what does X do?",
  "have we looked at…?".
- **Do:** read code / docs / sources; host simulations with REAL signal
  models (plucks, chords — never sines); report in listening terms, short.
- **Don't:** change firmware; turn a finding into a build.
- **Ends with:** findings + a proposal; the builder decides.

### Planning — a concrete proposal
- **Signals:** "make a plan", "propose", "how would you do it", "work it out".
- **Do:** every value named (nothing hidden), carried-over logic named, a
  routing diagram for any signal-path change, side effects first. Ends with
  "Go?".
- **Don't:** build. An answer to one detail is not a go; a question is not a go.

### Tuning — sound design by ear (the most common phase)
- **Signals:** after a go on a sound change; "too fast / too slow / too loud",
  "try…", "I need to hear it", "make it…", flashing hashes back and forth.
- **Do, per round:** edit → build (non-DIAG) → run the existing host suite →
  commit → hand over the hash + `task flash-chronotron3` → a reply of a few
  lines (what changed, the knob/constant to adjust next). Small numeric
  picks inside the request: just pick and name them.
- **Don't:** write ad-hoc host harnesses, run simulations, add tests, "verify"
  on the host, refactor, bundle extra ideas, explain theory. The builder's ear
  is the check. (Exception: something is suspected BROKEN → Bug phase.)
- **Ends with:** "lock it in" (→ Lock-in) or a move to the next topic.

### Studio — live testing session
- **Signals:** "I'm in the studio", "quick adjustments", "while I'm testing".
- **Do:** like Tuning, even tighter: the hash first, one or two lines. Keep
  the flashable build stable between his flashes; name exactly what to flash.
- **Don't:** anything that isn't the requested adjustment; long replies;
  questions that can wait.

### Bug — something is broken
- **Signals:** clicks / glitches / crashes / "doesn't work" / "weird", a
  behaviour that contradicts what was built.
- **Do:** find the cause before naming it (read the code path; DIAG log on
  hardware for intermittent faults; host repro allowed here). Fix clear bugs
  right away, say what the cause was in plain words.
- **Don't:** auto-recover from unexplained faults; present a guess as the cause.

### Lock-in — hardening a decision
- **Signals:** "lock it in", "keep it", "that's the one" (when followed by
  "lock it in").
- **Do:** host tests that pin the approved behaviour, each verified to fail
  on its regression; memory note of the approved state.
- **Don't:** change the sound while doing it.

### Docs — documentation and housekeeping
- **Signals:** only an explicit instruction ("update the docs", "document…").
- **Do:** exactly what was asked; keep `PROJECT.md` § Document Map complete.

---

## Quick table

| Phase | Builds firmware | Host checks | Tests | Reply length |
|-|-|-|-|-|
| Concept | no | no | no | short, ends with a question |
| Research | no | sims allowed | no | short findings + proposal |
| Planning | no | no | no | plan with every value, "Go?" |
| Tuning | yes, every round | existing suite only | no | a few lines + hash |
| Studio | yes | existing suite only | no | one or two lines + hash |
| Bug | when fixed | allowed | regression only if asked | cause in plain words |
| Lock-in | if needed | yes | yes (mutation-checked) | what is pinned |
| Docs | no | no | no | what was written |
