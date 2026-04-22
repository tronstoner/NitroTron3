# Agent Instructions

Hard rules for all AI agents working on this project. These override default behaviors and must be followed exactly.

## Git

- **Never `git commit` without the user explicitly confirming** (e.g., "commit", "yes commit"). Preparing the message is fine; executing `git commit` is not until confirmed.
- **Never `git push`.** Only the user pushes.
- **Never add `Co-Authored-By` lines** to commit messages.

## Documentation is source of truth

The README and PROJECT.md are the only user-facing references for what the pedal does. They must reflect the **implemented** state of the code — never specced, planned, or future behavior.

- **README controls and LEDs tables** must match what the firmware actually does right now. If a control is not wired in the code, list it as "Unused" or "Reserved for X (not yet implemented)".
- **PROJECT.md Current Status** must list every completed milestone accurately. The **Next** line must reflect only the actual next steps, not already-completed work.
- **After every code change session**, update both README (controls, LEDs) and PROJECT.md (Current Status, Next) before considering the work done. This is non-negotiable — it is part of the task, not a follow-up.
- Use the full controls template (all 6 knobs, 3 switches with UP/MIDDLE/DOWN, 2 footswitches) even if some are unused. See AGENTS.md for the template.

## Decision-making

- **Always ask before coding a fix.** When something needs fixing or changing, describe the problem, list 2-3 concrete options, and wait for the user to choose. Never assume what the fix should be and just write code.
- **Exception — trivial tweaks.** For small numeric values or obvious choices (e.g., "should this constant be 5 or 6?"), just pick a reasonable value. Save questions for decisions that actually matter. The line: would the user care which option? If not, just pick one.

## Prototype-to-firmware translation

When translating from an HTML demo, simulation, or any higher-level prototype to firmware, explicitly identify every implicit assumption the prototype makes about its execution model. Common divergences:

- **Event-driven vs. polling:** A browser demo fires callbacks only on user interaction. Firmware polls ADC/GPIO every N ms and must distinguish "value changed since last poll" from "value differs from stored state." Any comparison needs a clear answer to: *changed compared to what?*
- **Infinite precision vs. ADC noise:** Simulated knobs have exact float values. Hardware ADC readings jitter. Every comparison needs a dead zone or threshold.
- **Instant state transitions vs. timing overlap:** A demo can atomically set five variables. Firmware state changes span multiple ticks — guard against intermediate states where half the update has run.

When writing an implementation plan from a prototype, add a **"Firmware translation"** section that lists each divergence and how the firmware handles it. If a mechanism "just works" in the prototype (e.g., dirty detection is implicit in event callbacks), flag it — that's exactly where bugs hide.

## Code conventions

- All source code in `src/`, all documentation in `docs/`.
- Compile-time DSP constants live in `src/constants.h`. Do not hand-edit values outside the tuning workflow unless explicitly asked.
- DaisySP is the preferred DSP library. The Huovilainen ladder and parabolic oscillator shaper are implemented directly because DaisySP does not ship them.
- Tuning mode is part of the main binary, not a separate build. No `#ifdef DEV_MODE` guards.
- Stages are incremental. When working on Stage N, assume stages 0 through N-1 are complete and tested. If unclear, ask the user.
