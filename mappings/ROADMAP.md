# Reverse-engineering roadmap

## Proven now

- The CLI can start the exact cataloged `v2game.exe`, inject the kernel and a
  plugin, initialize both, and reach a responsive main window.
- The executable identity and twenty code signatures are machine-checked.
- Current country, province, and game-state layouts plus removed historical
  POP, bank, and GUI evidence are recorded without presenting hypotheses as
  verified facts.
- `economy_trace` provides a CSV output path once a campaign is running.
- `--save` enters Single Player, selects the save through the normal handler,
  and enters campaign mode without mouse or keyboard input.

## Immediate blocker

Campaign entry works, but the resulting game remains paused. Automation must
verify RTTI `CInGameIdler` before invoking pause or speed controls.

## Next mapping sequence

1. Add a verified campaign-phase check and invoke `CInGameIdler::TogglePause`.
2. Find `CGuiTypes::LookupString` by following references to known names such
   as `button_speedup`, `tax_0_slider`, and `take_loan`.
3. Dynamically verify historical POP constructor candidates at `0x554a40`,
   `0x554f40`, and `0x555450` before restoring `CPop` fields.
4. Validate `CPop::GiveMoney` at `0x55a5f0`, then restore read-only POP money
   and savings instrumentation before restoring the interest fix.
5. Map speed control, then investigate frame pacing for faster simulation.

## Runtime acceptance tests

- Load `autosave.v2` without mouse or keyboard input.
- Confirm the date advances and `economy_trace.csv` contains every country.
- Compare raw treasury values with the visible budget screen for one country.
- Verify a fixed RNG seed or define aggregate, tolerance-based assertions.
- Run vanilla and patched scenarios from the same fixture and compare world
  money totals over time.
