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
  toggle, and verifies the resulting pause byte. Date progression was observed
  in runtime testing but is not yet enforced by the runner.

## Immediate blocker

Campaign entry and unpause work. Speed 5 is already effectively unpaced. The
next blockers are selecting native speed 5 before unpause and handling modal
events that stop unattended date progression.

## Next mapping sequence

1. Select native speed index `4` before unpausing, then add run-until-date and
   clean exit.
2. Detect modal event interruptions and define an explicit benchmark policy.
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
