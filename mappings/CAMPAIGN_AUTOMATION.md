# Campaign automation

`smedley_cli --save` enters a Victoria 2 campaign without synthetic mouse or
keyboard input. The visible screen changes because Victoria 2's own frontend
handlers run; the harness dispatches native GUI signals rather than coordinates.

## Sequence

1. The launcher starts Victoria 2 suspended, injects Smedley and
   `campaign_runner`, then resumes the game.
2. Constructor hooks capture the main-menu controller (RVA `0x354a00`) and
   Single Player controller (RVA `0x36a2f0`).
3. Ten seconds after the Single Player controller appears, the plugin resolves
   `mainmenu_panel/single_player_button` through the main controller's GUI
   registry at `+0x704`.
4. Native press and release dispatchers at RVAs `0x5ee510` and `0x5ee550`
   enter the Single Player lobby.
5. The plugin confirms the requested save filename is present at `+0x590`,
   constructing it only when that game string is empty; it rejects a different
   existing value. It then sets `+0x5bc=1` and `+0x5bd=0`.
6. Victoria 2's normal lobby update calls `CCurrentGameState::LoadSave` from
   call RVA `0x36f8b3`, performs its complete post-load contract, clears
   `+0x5bc`, and sets `+0x5bd`.
7. The plugin resolves `play_button` through the lobby GUI registry at `+0x278`
   and emits the same native press/release sequence.
8. `CGameState+0xb24` changes from RTTI `CFrontEnd` to `CInGameIdler`, proving
   campaign entry.
9. When `--observe` is present, the runner invokes the native return-to-AI
   transition at RVA `0x287a70` for the current player country.
10. The runner verifies that every `CGameState+0xaec` player-control entry is
    zero, the former player country's `CCountry+0x208` AI pointer is non-null,
    and the global AI scheduler list grew.
11. The runner invokes the registered native `debug` command with argument
    `fow`, verifies the process-global byte at RVA `0xb092fb` is `0`, and leaves the map fully
    visible.
12. The runner invokes the native speed-up or speed-down handler until
    `CGameState+0xb28` matches the selected speed minus one, reading the field
    after every call. Both paths are runtime-verified against the supported
    executable; the retained speed-down probe selected speed 2 from a higher
    initial speed and checked each decrement.
13. The runner checks the pause state at `CInGameIdler+0x1538`, invokes
    `CInGameIdler::TogglePause` at RVA `0x26a2c0` when the value differs from the
    requested state, and verifies the result. Observer mode rejects
    `start_paused` because its watchdog requires simulation advancement.
14. While observer mode remains active, nine duplicated generic message
    dispatcher hooks skip visible popup and `CPauseGame` construction. This
    covers configurable notifications including technology, invention, upper
    house, bankruptcy, and diplomatic/Great War messages. Logging, map notices,
    icons, message production, effects, and AI event choices continue normally.

## Command

```powershell
smedley_cli --game-dir "C:\path\to\Victoria 2" `
  --plugin plugins/campaign_runner.toml `
  --save "C:\Users\name\Documents\Paradox Interactive\Victoria II\save games\autosave.v2" `
  --observe `
  --detach
```

The CLI constrains the save to Victoria 2's `save games` directory. The plugin
uses the file name to select a row already known to the game.

`--observe` is optional and requires `--save`. It leaves the current player tag
intact as the UI viewing perspective because Victoria II has no native
spectator/null-country UI. The underlying country is no longer human-owned: the
native transition clears its player-control entry, constructs its `CCountryAI`,
and registers that AI with the scheduler. The runner remains paused if any
country is still human-controlled, AI restoration cannot be verified, or full
map visibility cannot be enabled.

Observer message suppression is presentation-only. Victoria II duplicates the
same policy gate across nine dispatchers; all nine are signature-checked and
bypassed regardless of the user's `messagetypes.txt` policy. This avoids
per-message configuration without suppressing the underlying message,
simulation effect, log entry, map notice, or icon.

## Fixed-date benchmark

`--run-days N` and `--run-until-date-raw N` are mutually exclusive bounded
campaign targets. After the final requested speed and unpaused-state readback
(and, for observer mode, full AI/FOW/valid-view postconditions), the runner
reads `CCurrentGameState::current_date_raw()`. Relative targets add `N * 24`
in int64; absolute targets must advance and be aligned to the recorded
verified-runtime 24 raw units per game day.

A recurring Win32 `SetTimer` uses `USER_TIMER_MINIMUM` (effectively 10 ms).
It does not poll a game hook or mutate from `DailyUpdateEvent`.
Each tick checks `CInGameIdler`, readable date, pause state, and lightweight
observer invariants. At the exact target it invokes verified `TogglePause` and
reads back the paused state. Overshoot, timeout, date regression, invalid
idler/pause state, or observer invariant failure is a failed benchmark. Both
terminal paths stop monitoring and popup suppression, cancel the timer, and
leave the campaign safely paused/open when pause readback succeeds. They never
attempt an exit or write a save.

An invalid runtime target or unavailable benchmark timer is terminal. The runner
first attempts and reads back pause; its failure record reports whether that
readback is known. It never claims the campaign is paused when that readback
fails.

Runtime acceptance on July 31, 2026 ran the unmodified `benchmark.v2` save
twice with no mods, observer mode, native speed 5, `--run-days 365`, and only
the `campaign_runner` and `telemetry` plugins. Runs
`a53ae91b-5e1b-4a52-9422-c63c65d52643` and
`5a0134ce-2125-4da6-a59e-9b0dc7e8f551` both started at raw date `59883384`,
paused at target and actual date `59892144`, emitted one start and one completed
record, and remained responsive and open. Each trace contained 374 strictly
ordered records with no gaps, drops, or write failure. Their bounded intervals
took 21,124,677 and 21,230,817 microseconds (17.28 and 17.19 game days per
second, a 0.50 percent repeat difference). Both observed 98,644 daily callbacks,
and the source save retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.
This verifies the fixed-date pause harness on the supported executable; it does
not establish a clean exit, final telemetry flush, saved result, or replay
equivalence.

Additional final-DLL acceptance covered three non-happy-path boundaries. Run
`31a41204-121e-4e11-be5d-6ae3fb47f771` used no observer mode and an absolute
target, completing exactly 10 days at raw date `59883624` with paused readback.
Run `e1c015df-5b89-4f08-9925-90c49214f924` used a one-second timeout and failed
after 1,013,936 microseconds at raw date `59883864`, reporting `timeout` and
paused readback. Run `866c652a-fbef-49f5-91e1-b76bcadc0e81` requested the
nonadvancing start date, emitted standalone `invalid_target`, and remained
paused without inventing unavailable progress counters. All three traces had
zero sequence gaps; available counters reported zero drops and no write
failure. The source save hash remained unchanged.

The resource-profiling build repeated the same unmodified vanilla observer
benchmark with one Windows process snapshot immediately before each terminal
record. Both traces contained 375 ordered records, including exactly one
`benchmark.resources`, with zero gaps, drops, or write failures:

| Run | Wall us | Process CPU us | CPU / wall | Working set start -> end | Private bytes start -> end | Process peak working set |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `c835ced2-9140-4acb-a656-648795a77de3` | 20,874,288 | 27,500,000 | 131.741% | 1,338,437,632 -> 1,360,338,944 | 1,540,493,312 -> 1,564,241,920 | 1,372,725,248 |
| `8b259c50-5cf7-476a-a634-95b87b152c9a` | 21,133,183 | 31,734,375 | 150.164% | 1,362,038,784 -> 1,361,985,536 | 1,565,822,976 -> 1,568,247,808 | 1,373,532,160 |

Wall throughput was 17.49 and 17.27 game days per second, a 1.23 percent
repeat difference, while process CPU differed by 14.30 percent. Two runs do not
bound CPU-time variance sufficiently for an optimization claim. CPU / wall can
exceed 100 percent because Windows sums process CPU time across threads. The
source save again retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

GFM preflight resolves the installed descriptor's `user_dir = "GFM"` and accepts
a save beneath that mod-specific `save games` directory. Runtime compatibility
is not yet established: the only available candidate was a copied vanilla save,
not a GFM-authored fixture, and runs `2ee97720-b85b-4616-9749-b59ad9e04e90` and
`26e16827-a6a2-45e1-84f9-963ce6f80460` never reached the verified
`CInGameIdler` transition. The source save retained SHA-256
`662425a530dfacfb8e90fce73aa0555464cfd3803c036cb23c34423a252a571d`.

## Current boundary

Campaign entry, full-AI observer mode, and native unpause are verified. A
runtime test using the `benchmark.v2` save returned `JAN` to AI control, changed
the scheduler count from 271 to 272, and found no remaining nonzero
player-control entries. The corrected `debug fow` invocation produced
zero-valued visibility readback
and continued advancing `economy_trace` rows at speed 5.

The observer watchdog contains a timer-driven attempt to recover when the
viewing country owns no provinces: pause, select the lowest-ordinal living AI
country, invoke native `tag`, restore that country to AI, verify the scheduler,
and resume. It is not runtime-effective for annexation. Disposable fixture run
`dd97c381-ea6d-490f-bff8-6173cda92079` first reached verified JAN observer
postconditions, then had AI-controlled ENG execute `inherit = JAN` on January
1, 1837. The game log confirmed that England annexed Jan Mayen, but Victoria II
moved away from `CInGameIdler` before the one-second watchdog could observe the
missing view; `campaign_runner` stopped with `observer campaign left
CInGameIdler` and emitted no failover. Reliable recovery therefore remains
blocked on a verified pre-game-over country-lifecycle boundary or a safe route
back from the resulting controller. The disposable mod was removed and the
source save retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

For manual view changes, observer mode replaces native `tag` with an error and
registers `switch TAG`. `switch` validates a living scheduled-AI target, pauses,
calls the saved native handler, waits for its asynchronous ownership transition,
returns the target to AI, verifies exact scheduler restoration, and resumes.
Outside observer mode, native `tag` is not modified.
`--view-tag TAG` dispatches that same registered `switch` command once after the
observer watchdog starts, providing a deterministic initial view and an
end-to-end automation path for testing the transaction.

Runtime acceptance with `--view-tag ENG` switched the view from JAN to ENG,
restored ENG's AI and exact scheduler count one second later, and resumed the
simulation. A subsequent native `tag FRA` command was rejected by the
observer-only replacement handler.

A subsequent observer test bypassed six generic message dispatches while
`economy_trace` advanced about 3,085 days without a watchdog-detected pause.
After expanding coverage to all nine dispatchers, a smoke run suppressed six
policy-marked popups while advancing 385,895 country rows without pausing.

The player tag remains `JAN` as a UI viewing perspective; it is not an ownership
marker for AI scheduling. Human ownership is represented by the
`player_nations` map, and AI participation by `CCountry::_ai` plus the global AI
scheduler list.

`economy_trace` is a separate observer and can be loaded alongside
`campaign_runner` when CSV output is needed. Do not call pause or speed
functions based on a non-null pointer alone; verify the idler phase first.

## Lifecycle Telemetry

With an already active optional telemetry plugin, the runner dynamically
resolves its C ABI and emits `verified-runtime` records after the requested
save filename is confirmed present and the save flags are written, `+0x5bd`
observation, RTTI entry readback, observer postconditions,
speed readback, and final pause readback above. The Win32 timer callback context
is not independently established as a game UI thread, so telemetry makes no
thread guarantee. `observer.configured` waits for an observed valid view after
any requested switch. View tags are exactly three normalized uppercase ASCII
alphanumeric characters, including dynamic tags such as `D01`.

Runtime acceptance on July 31, 2026 used `benchmark.v2`, observer view `ENG`,
speed 5, and the lifecycle/state telemetry categories. Records observed the
save-selection flags, load-complete flag, `CInGameIdler`, speed 5 readback, final
unpaused readback, and ENG observer postconditions in that order. The resulting
trace `f2d403e1-82f8-46b8-8832-a136c822d38a` advanced 515 game days and
validated 1,039 strictly ordered records with no sequence gaps, drops, write
failure, or campaign-runner telemetry warning. This validates the emission
points; it does not establish a general timer-callback thread contract.
