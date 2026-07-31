# Campaign automation

`smedley_cli --save` enters a Victoria 2 campaign without synthetic mouse or
keyboard input. The visible screen changes because Victoria 2's own frontend
handlers run; the harness dispatches native GUI signals rather than coordinates.

## Sequence

1. The launcher starts Victoria 2 suspended, injects Smedley and
   `economy_trace`, then resumes the game.
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

## Command

```powershell
smedley_cli --game-dir "C:\path\to\Victoria 2" `
  --plugin plugins/economy_trace.toml `
  --save "C:\Users\name\Documents\Paradox Interactive\Victoria II\save games\autosave.v2" `
  --detach
```

The CLI constrains the save to Victoria 2's `save games` directory. The plugin
uses the file name to select a row already known to the game.

## Current boundary

Campaign entry is verified. The campaign starts paused, so date progression and
daily economy output do not begin until `CInGameIdler::TogglePause` is invoked.
Do not call pause or speed functions based on a non-null pointer alone; verify
the idler phase first.
