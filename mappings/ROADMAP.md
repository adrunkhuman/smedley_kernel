# Reverse-engineering roadmap

## Proven now

- The CLI can start the exact cataloged `v2game.exe`, inject the kernel and a
  plugin, initialize both, and reach a responsive main window.
- The executable identity and fifteen code signatures are machine-checked.
- Current country, province, and game-state layouts plus removed historical
  POP, bank, and GUI evidence are recorded without presenting hypotheses as
  verified facts.
- `economy_trace` provides a CSV output path once a campaign is running.

## Immediate blocker

Save data can be loaded unattended, but this does not perform the frontend
Play transition into campaign mode. Until that transition is mapped, the
in-game idler is not initialized and simulation cannot tick safely.

## Next mapping sequence

1. Load this exact executable into Ghidra and preserve its SHA-256 in the
   project metadata.
2. Trace the Play action after a save is selected and map the transition into
   campaign mode. `LoadSave` at RVA `0x27f1d0` is already runtime-verified.
3. Find `CGuiTypes::LookupString` by following references to known names such
   as `button_speedup`, `tax_0_slider`, and `take_loan`.
4. Dynamically verify historical POP constructor candidates at `0x554a40`,
   `0x554f40`, and `0x555450` before restoring `CPop` fields.
5. Validate `CPop::GiveMoney` at `0x55a5f0`, then restore read-only POP money
   and savings instrumentation before restoring the interest fix.
6. Use the verified `CInGameIdler` pause toggle after campaign transition, map
   speed control, then investigate frame pacing for faster simulation.

## Runtime acceptance tests

- Load `autosave.v2` without mouse or keyboard input.
- Confirm the date advances and `economy_trace.csv` contains every country.
- Compare raw treasury values with the visible budget screen for one country.
- Verify a fixed RNG seed or define aggregate, tolerance-based assertions.
- Run vanilla and patched scenarios from the same fixture and compare world
  money totals over time.
