# Reverse-engineering roadmap

## Proven now

- The CLI can start the exact cataloged `v2game.exe`, inject the kernel and a
  plugin, initialize both, and reach a responsive main window.
- The executable identity and 29 signatures are machine-checked.
- Current country, province, and game-state layouts plus removed historical
  POP, bank, and GUI evidence are recorded without presenting hypotheses as
  verified facts.
- `campaign_runner` uses `--save` to enter Single Player, select the save through
  the normal handler, and enter campaign mode without mouse or keyboard input.
- The independent `economy_trace` plugin provides a CSV output path once a
  campaign is running.
- `campaign_runner` verifies RTTI `CInGameIdler`, invokes the native pause
  toggle, and verifies the resulting pause byte. Bounded benchmark runs now
  enforce an exact target date or emit a typed terminal failure.
- Optional full-AI observer mode uses the native return-to-AI transition and
  verifies that no player-control entry remains before unpausing; runtime
  acceptance passed with `benchmark.v2` and `JAN`.
- The observer watchdog has a statically checked annexation-failover attempt,
  but runtime testing proves it is not yet reliable: annexing the viewed
  country leaves `CInGameIdler` before the timer can switch views.
- Native speed selection, generic message-popup suppression, and bounded
  run-for-days targets are implemented. Two identical 365-day observer runs
  paused at the exact target with a 0.50 percent throughput difference and no
  telemetry gaps, drops, or source-save mutation.
- The built-in `scripting` plugin runs source-visible Lua in private bounded
  states off the game callback, delivers copied daily snapshots, schedules
  in-memory callbacks, and queues a signature-checked native pause. Runtime run
  `b076f162-9700-40c3-9c9b-7f56c53991b3` completed the pause with readback while
  both Victoria II Lua DLLs remained loaded; see `SCRIPTING.md`.

## Immediate blocker

Campaign entry, observer setup, native speed 5, generic message-popup
suppression, and exact-date pause work. The immediate automation blocker is a
verified save/checkpoint and clean native exit boundary; until then benchmark
runs deliberately remain paused and open. Non-generic modal interruptions also
need an explicit policy and reproducible runtime fixture.

## Next mapping sequence

1. Find a verified save/checkpoint and clean exit boundary, including final
   telemetry drain semantics.
2. Reproduce non-generic modal event interruptions and define an explicit
   benchmark policy.
3. Find `CGuiTypes::LookupString` by following references to known names such
   as `tax_0_slider` and `take_loan`.
4. Dynamically verify historical POP constructor candidates at `0x554a40`,
   `0x554f40`, and `0x555450` before restoring `CPop` fields.
5. Validate `CPop::GiveMoney` at `0x55a5f0`, then restore read-only POP money
   and savings instrumentation before restoring the interest fix.

## Runtime acceptance tests

- Load `autosave.v2` without mouse or keyboard input.
- Confirm the date advances and `economy_trace.csv` contains every country.
- Compare raw treasury values with the visible budget screen for one country.
- Verify a fixed RNG seed or define aggregate, tolerance-based assertions.
- Run vanilla and patched scenarios from the same fixture and compare world
  money totals over time.
