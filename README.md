# Smedley

Smedley is an instrumentation, automation, and extension framework for
Victoria II: Heart of Darkness 3.04. It adds a launcher, unattended campaign
control, observer mode, telemetry, constrained Lua scripting, and optional
native plugins without replacing the game engine.

Use the launcher and profiles for normal play. Use the CLI for automation.
Native development is optional.

## What It Includes

| Component | Purpose |
| --- | --- |
| `smedley_launcher.exe` | Configure, validate, and launch the game through a native GUI. |
| `smedley_cli.exe` | Run the same preflight and launch workflow from scripts or a terminal. |
| `campaign_runner` | Load a save, set speed or pause state, enter observer mode, and run bounded benchmarks. |
| `telemetry` | Write bounded structured traces for lifecycle and selected game-state families. |
| `smedley_trace.exe` | Validate, inspect, summarize, compare, and export telemetry offline. |
| `scripting` | Run selected Lua 5.1 source files against copied state and queued operations. |
| `interest_bug_fix` | Optionally distribute omitted state interest to eligible depositor POPs. |

## Supported Game

Smedley supports one exact English Windows executable:

| Property | Value |
| --- | --- |
| Game | Victoria II: Heart of Darkness 3.04 |
| SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| File size | `12294656` bytes |
| Architecture | x86 |

The launcher rejects other executables before injection. Validate a game
installation with:

```powershell
python tools/validate_mappings.py "C:\Games\Victoria 2\v2game.exe"
```

## Build And Install

Requirements: Windows, CMake 3.20 or newer, MSVC, Git, Python 3 for the test
suite, and network access for the initial pinned dependency fetch.

```powershell
New-Item -ItemType Directory -Force "C:\Games\Victoria 2\plugins"
cmake -S . -B build -A Win32 -DV2_GAME_DIR="C:\Games\Victoria 2"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix "$PWD\build\install"
```

Smedley binaries, manifests, scripts, and notices install directly to the
configured `V2_GAME_DIR`. The prefix redirects dependency-owned relative install
artifacts to `build\install`; it does not relocate Smedley's absolute game
destinations. The `plugins` directory must exist when CMake configures the
project so it generates the bundled-plugin install rules.

See [`BUILDING.md`](BUILDING.md) for prerequisites, dependency behavior,
installation details, and validation limits.

## First Launch

1. Run `smedley_launcher.exe` from the Victoria II directory.
2. Select the game directory. Smedley discovers mods and plugin manifests under
   that directory.
3. Select any mods and trusted plugins.
4. Review preflight diagnostics, then launch.

Safe mode starts the verified game without injecting Smedley. Ordinary mods
still load; plugins, scripts, telemetry, save automation, observer, speed, and
bounded-run controls are ignored with warnings. Use it to recover from a broken
plugin configuration.

Profiles are schema-v1 TOML files shared by the GUI and CLI. Relative mod and
plugin paths resolve from `game_dir` under `mod/` and `plugins/`. See
[`docs/launcher.md`](docs/launcher.md).

## CLI Examples

Discover available mods and plugins:

```powershell
smedley_cli --game-dir "C:\Games\Victoria 2" --discover
```

Validate a profile without starting the game:

```powershell
smedley_cli --profile "C:\Profiles\observer.toml" --dry-run
```

Load a save in observer mode:

```powershell
smedley_cli --game-dir "C:\Games\Victoria 2" `
  --plugin plugins\campaign_runner.toml `
  --save "C:\Users\me\Documents\Paradox Interactive\Victoria II\save games\run.v2" `
  --observe --speed 5 --detach
```

Paths may contain spaces and Unicode characters. GUI and CLI launches use the
same preflight and profile model.

## Optional Features

### Campaign Automation

`campaign_runner` loads a selected save through native game operations. Observer
mode returns the player country to AI control, enables full-map visibility, and
verifies the resulting state. Bounded runs can stop after an exact number of
game days and optionally request native game exit after successful completion.
They require injection, a selected save, `campaign_runner`, a detached launch,
and an unpaused start. Failures leave the campaign paused and open; bounded runs
do not save. See [`docs/launcher.md`](docs/launcher.md).

### Telemetry

`telemetry` records versioned JSON Lines for lifecycle events and configured
world, country, province, economy, factory, and population families. Capture is
opt-in, bounded, and explicit about gaps, drops, unavailable fields, and mapping
quality. Crashes and forced exits can lose queued records and recent buffered
writes; safe mode and dry runs produce no trace. `smedley_trace` provides strict
offline validation and exports. See [`docs/telemetry.md`](docs/telemetry.md).

### Scripting

`scripting` runs visible Lua 5.1 source files in independent constrained states.
Scripts receive copied events and supported queued operations, not Victoria
II's Lua state or raw game pointers. This is constrained execution, not a
security sandbox. See [`docs/scripting.md`](docs/scripting.md).

### Interest Fix

`interest_bug_fix` is an independently selectable gameplay change. It preserves
the native bank and state pipeline while distributing omitted state interest to
eligible positive-savings POPs. CSV diagnostics and interest telemetry are
disabled by default. Debug mode truncates `<GAME_DIR>/interest_bug_fix.csv` on
launch; interest telemetry also requires the `telemetry` plugin. The manifest
conflicts with plugin ID `v2up`. Review the retained evidence in
[`mappings/evidence/interest-payout.md`](mappings/evidence/interest-payout.md)
before enabling the fix.

## Runs And Recovery

The launcher attempts to write run metadata under
`%LOCALAPPDATA%\Smedley\runs`. `smedley_cli --history` lists recent records.
Detached runs remain marked `started`; that status does not prove successful
injection, campaign entry, or game exit. Persistence failures warn but do not
block launch.

Telemetry defaults to `%LOCALAPPDATA%\Smedley\traces\<run-id>.jsonl` when no
explicit output is configured. Safe mode remains available when injected launch
or plugin initialization is unavailable.

## Trust And Limits

- Native plugins are DLLs with the same process, filesystem, and user authority
  as Victoria II. Install only trusted plugins.
- Smedley supports Windows, MSVC x86, and only the executable identified above.
- Plugins use versioned C lifecycle and capability APIs; no C++ ABI crosses the
  DLL boundary.
- Plugin dependencies and conflicts are resolved by stable ID. Version-range
  dependency resolution is not implemented.
- Native plugins are x86 PE DLLs, export `SmedleyPluginGetApiV1`, and resolve C
  services dynamically rather than importing kernel internals. See
  [`docs/plugin-development.md`](docs/plugin-development.md).
- Launch metadata records launcher activity, not game health.
- Unknown engine state remains unavailable rather than being guessed.

## Documentation

| Topic | Document |
| --- | --- |
| Build and install | [`BUILDING.md`](BUILDING.md) |
| Launcher, profiles, CLI, observer mode, and runs | [`docs/launcher.md`](docs/launcher.md) |
| Telemetry configuration, schemas, and trace tools | [`docs/telemetry.md`](docs/telemetry.md) |
| Clausewitz static workload inventory | [`docs/workload-analysis.md`](docs/workload-analysis.md) |
| Lua configuration and API | [`docs/scripting.md`](docs/scripting.md) |
| Native plugin development | [`docs/plugin-development.md`](docs/plugin-development.md) |
| Engine ownership and service boundaries | [`docs/game-state-boundary.md`](docs/game-state-boundary.md) |
| Reverse-engineering evidence | [`mappings/README.md`](mappings/README.md) |
| Contributing | [`CONTRIBUTING.md`](CONTRIBUTING.md) |
