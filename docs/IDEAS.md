# Ideas notepad — platform / product direction

> **Not decisions. Not a spec.** A running scratchpad of ideas about the pedal
> family's identity, UI philosophy and build strategy, kept so they can be
> iterated on later instead of re-derived. Anything here is provisional until it
> lands in `ARCHITECTURE.md` (structure), a module spec (behaviour), or
> `ChronoTron3/DESIGN_DECISIONS.md` (rationale).
>
> Format: dated entries, newest at the top. Mark whose idea it is, and keep
> "what was actually said" separate from "what it might imply" — the second part
> is food for thought, not agreed work.

---

## 2026-09-14 — Module interchangeability, UI economy, and one-module firmwares

**Builder's framing (Ralf).**

- **The goal is NOT to unify the three modes into one engine.** They stay
  distinct engines. What should be interchangeable is the *module as a unit* —
  one mode = one contained module.
- **What Eurorack actually does, and why it doesn't copy over directly.** Those
  modules open up their range through a hardware/software *topology*: CV inputs,
  patching, modules feeding each other. That is what broadens their range and
  opens up control. A pedal has none of it — we are self-contained, no CV. So
  synthesising the same openness in the pedal world needs tricks, not imitation.
- **What we have to trade with instead:** six knobs, three 3-position toggles,
  two footswitches, two LEDs. The footswitches in particular are under-used and
  are the obvious place to buy back range.
- **UI is everything in the pedal world.** Knob ranges and functions have to be
  as intuitive as possible *even when the thing underneath is complex*.
- **NitroTron3 got this right, but it was an easier problem**: no tap tempo, no
  looper functions, so nothing competed with the preset system for the
  footswitches.
- **The preset system was dropped for ChronoTron3** and how to handle it is
  still open.
- **The idea on the table:** instead of one densely packed firmware (3 modes ×
  24 presets, NitroTron3-style), ship firmwares with **one module and ~3
  presets**. Reducing what a single build has to carry would:
  - keep **tap tempo** viable, because FS1 is no longer owned by preset duty;
  - **free footswitch control for the other modules too**, each of which wants
    its own gestures (looper transport, freeze, panic, tap).
- **Gated on user feedback.** Ralf wants to hear from friends actually playing
  the pedal before committing to a direction.

**Food for thought (Claude — unagreed, for later argument).**

- **The pedal-world analogue of "topology" is probably surface budget.** Eurorack
  buys range with patch points; a sealed pedal can only buy it by giving each
  function more of a fixed surface. Framed that way, "fewer modes per firmware"
  isn't a reduction in capability — it is the only lever that buys openness, and
  it is the same trade the module world makes with panel space.
- **It collides with one stated invariant, deliberately worth re-opening.**
  `ARCHITECTURE.md` § Decided says *"SW3 selects the mode on every pedal"*,
  uniform across the family. A one-module firmware frees SW3 entirely — a whole
  3-position toggle back, which is a large amount of surface. Candidates: preset
  select (3 slots maps suspiciously well onto 3 positions), or a per-module
  character/voicing switch. If SW3 stops meaning mode select, that decision needs
  rewriting rather than quietly contradicting.
- **3 presets on a toggle is WYSIWYG-friendly.** A toggle shows its state
  physically, with no LED language to learn and no hidden state — which is much
  closer to the manual/WYSIWYG paradigm than a footswitch-cycled bank ever was.
  The catch is the knobs still lie about the loaded values, which is the same
  problem NitroTron3's preset system has.
- **This makes the OmniTron3 custom-build idea much more attractive.** If a build
  is literally "pick one module", the bundle/dynamic-build question collapses
  into something almost trivial to generate, and the web updater already makes
  reflashing to swap effect realistic rather than theoretical.
- **The cost to name honestly:** one module per firmware means reflashing to
  change effect, so the pedal stops being three pedals in one. Whether that is a
  loss depends entirely on whether people actually switch modes mid-set — which
  is exactly the question the friend feedback should answer. Worth asking them
  that explicitly rather than only "do you like it".
- **A middle option nobody has costed yet:** two modules per firmware instead of
  one or three. SW3 keeps mode select but only uses two positions, leaving the
  third for something else, and each module still gets a footswitch.

**Open questions to carry forward**

1. Do people switch modes mid-performance, or pick one and stay? (ask the
   friends directly)
2. If SW3 is freed, does it become presets, or module character?
3. Does a preset recall knob values, or only switch/hidden state? The WYSIWYG
   objection to the former is unresolved from NitroTron3.
4. Can tap tempo and a looper transport genuinely coexist on two footswitches,
   or does that force the one-module split on its own?
