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
leave the campaign safely paused/open when pause readback succeeds. A successful
exact-target run may then request the verified native quit operation when
`quit_after_run` is explicitly enabled. Failures never attempt an exit, and no
terminal path writes a save.

The benchmark is terminally paused before the exit request. The runner validates
the idler RTTI, exact vtable target, and post-call request flag. A failed
validation or readback logs a failure and leaves the campaign paused and open.
Before dispatch, the runner waits up to five seconds for the optional sibling
telemetry drain. Completed or unavailable telemetry permits exit. Busy, timeout,
or failure leaves the campaign paused and open. A completed drain has stopped
telemetry ingress, consumed accepted payload records, and joined its writer; it
then writes any enabled final summary, flushes, and closes the file. Unavailable
means telemetry is absent, inactive, or lacks the drain symbol, so that
compatibility path permits exit without a final-summary or durability guarantee.

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
not establish a final telemetry flush, saved result, or replay
equivalence.

Runtime acceptance on August 1, 2026 mapped the accepted `QUIT` confirmation
callback at RVA `0x276500` to primary `CInGameIdler` vtable slot `+0x110`. Its
target at RVA `0x24edb0` sets `CInGameIdler+0x1d20` to one. Detached run
`21b024dd-ec69-4d46-a22e-1d309ab83b21` loaded the unmodified `benchmark.v2`,
completed an exact one-day target, read back that native quit-request state, and
exited without `ExitProcess`, `TerminateProcess`, `WM_CLOSE`, or synthetic
input. The source save retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.
This verifies process exit only. The telemetry drain was added afterward and has
separate acceptance evidence below; the kernel still has no generic pre-exit
plugin lifecycle notification.

Runtime drain acceptance on August 1, 2026 used detached run
`4d3d4e44-b4e5-4d7f-b10a-b50bd5b96cfb` with the bundled `campaign_runner` and
`telemetry` plugins, lifecycle capture, and an exact one-day target. The runner
logged drain completion before the native quit request and the process exited.
The validated 11-record trace placed `benchmark.completed` at sequence 10 and
`telemetry.summary` as the final newline-terminated sequence 11. Summary counters
reported accepted 10, written 10, dropped 0, and no write failure. The source
save retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.
This acceptance covers the bundled completed-drain path, not timeout retry or
legacy-plugin compatibility.

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
zero-valued visibility readback and continued advancing at speed 5.

Observer mode hooks `CCountry::Annex` at RVA `0x118620`, before the function
captures `CGameState::_player_tag` or removes provinces. If the annexed ordinal
is the current observer view, the hook selects a living scheduled-AI country
and changes only the verified `CGameState+0xb5c` viewing tag. It verifies that
all player-control entries remain zero, the target AI remains scheduled, and
the scheduler count is unchanged. The normal timer-driven safe-switch state
machine remains as a fallback for country disappearance outside this path.

Runtime run `bac0c2f2-94a5-403d-a836-47a5689a5bcc` loaded a deterministic
`ENG = { inherit = THIS }` fixture, reached verified JAN observer postconditions,
moved the view to ENG inside the annex entry hook, avoided the game-over
transition, and continued in `CInGameIdler` beyond April 1837. An earlier run
confirmed that native `tag` is asynchronous and cannot complete at this
boundary; direct reassignment of the camera tag is intentionally narrower and
does not alter country ownership or AI scheduling.

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

A subsequent observer test bypassed six generic message dispatches while the
campaign advanced about 3,085 days without a watchdog-detected pause.
After expanding coverage to all nine dispatchers, a smoke run suppressed six
policy-marked popups while advancing 385,895 country rows without pausing.

The player tag remains `JAN` as a UI viewing perspective; it is not an ownership
marker for AI scheduling. Human ownership is represented by the
`player_nations` map, and AI participation by `CCountry::_ai` plus the global AI
scheduler list.

Structured telemetry can be loaded alongside `campaign_runner` when state
output is needed. Do not call pause or speed functions based on a non-null
pointer alone; verify the idler phase first.

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
