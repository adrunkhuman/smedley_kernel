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
5. The plugin writes the save filename to the Single Player controller at
   `+0x590`, then sets `+0x5bc=1` and `+0x5bd=0`.
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
12. The runner invokes the native speed-up handler until `CGameState+0xb28`
    equals `4`, displayed by Victoria II as speed 5.
13. The runner checks the pause state at `CInGameIdler+0x1538`, invokes
    `CInGameIdler::TogglePause` at RVA `0x26a2c0` only when the value is `1`, and
    verifies that it changed to `0`.
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

## Current boundary

Campaign entry, full-AI observer mode, and native unpause are verified. A
runtime `benchmark.v2` test returned `JAN` to AI control, changed the scheduler
count from 271 to 272, found no remaining nonzero player-control entries, and
The corrected `debug fow` invocation produced zero-valued visibility readback
and continued advancing `economy_trace` rows at speed 5.

The observer watchdog detects when the viewing country owns no provinces. It
pauses, selects the lowest-ordinal living AI country, invokes native `tag`, waits
for the queued switch to mark that country human, immediately calls the native
return-to-AI transition, verifies scheduler restoration, and resumes. This
annexation failover is statically validated but awaits an actual runtime
annexation in the corrected build.

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
