# Reverse-engineering roadmap

## Proven now

- The CLI can start the exact cataloged `v2game.exe`, inject the kernel and a
  plugin, initialize both, and reach a responsive main window.
- The executable identity and 52 signatures are machine-checked.
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
- Native plugins can advertise the fixed-width C lifecycle ABI v1 while legacy
  `CreatePlugin` modules remain compatible. Supplied-game run
  `5fca45a1-a770-41e4-8eab-ed472c0ddfc9` loaded one of each in a responsive
  process; normal-exit callbacks remain blocked on a verified shutdown path.
- Native C plugins can dynamically register for the runtime-exercised daily
  country hook through a fixed-width event table. The hook copies provisional
  mapped date, tag, treasury, owned-province presence, and country/AI counts plus
  verified-runtime human-control presence without exposing a game pointer;
  registration is bounded and the callback path is allocation-free. Run
  `479a31ac-6fd4-4ae2-b170-865c63b70d66` exercised the cross-DLL callback while
  advancing an exact one-day campaign target in a responsive process.

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
4. Establish the correct pre/post-update correlation boundary for the bounded
   `interest_probe`; the seven-day state/vector shape run is complete. See
   `INTEREST_FIX.md`.
5. Add bounded POP-level savings instrumentation, then dynamically validate
   `CPop::GiveMoney` at `0x55a5f0` before restoring the interest fix.

## Runtime acceptance tests

- Load `autosave.v2` without mouse or keyboard input.
- Confirm the date advances and `economy_trace.csv` contains every country.
- Compare raw treasury values with the visible budget screen for one country.
- Verify a fixed RNG seed or define aggregate, tolerance-based assertions.
- Run vanilla and patched scenarios from the same fixture and compare world
  money totals over time.
