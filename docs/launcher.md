# Launcher

`smedley_launcher_core` is the shared C++17 preflight and launch layer used by
the CLI and the native Win32 `smedley_launcher.exe` GUI. It only supports the
verified x86, English Victoria II: Heart of Darkness 3.04 executable. The
launcher checks its exact size and SHA-256 before it starts either the injected
or no-injection path.

## Profiles

Profiles are TOML files using schema version 1. Paths are TOML strings and may
contain spaces. Relative selected mod and plugin paths are resolved from
`game_dir`; use `mod/example.mod` and `plugins/example.toml` respectively.

```toml
# Smedley launcher profile schema v1
name = "GFM observer"
game_dir = "C:\\Games\\Victoria 2"
kernel = "C:\\Games\\Victoria 2\\smedley_kernel.dll" # optional
inject = true
mods = ["mod/GFM.mod"]
plugins = ["plugins/campaign_runner.toml"]
save = "C:\\Users\\me\\Documents\\Paradox Interactive\\Victoria II\\save games\\run.v2" # optional
observer = true
view_tag = "ENG" # optional; three ASCII letters and requires observer
speed = 5 # optional; 1 through 5, applied after unattended save loading
start_paused = false # optional; requires a save and campaign_runner; incompatible with observer
detach = false
telemetry_enabled = false
telemetry_output = "C:\\traces\\gfm.jsonl" # optional; default is per-run under %LOCALAPPDATA%
telemetry_categories = ["lifecycle", "state"] # lifecycle, state, or both
telemetry_sample_days = 1 # 1 through 365
telemetry_queue_capacity = 1024 # 64 through 8192
telemetry_overwrite = false # required to replace an existing .jsonl file
```

`mods` selects ordinary Victoria II descriptors. Each descriptor must be under
`GAME_DIR/mod`, and the launcher supplies Victoria II's verified syntax:
`-mod=mod/<descriptor>.mod`. A mod is data interpreted by the game.

`plugins` selects Smedley TOML manifests under `GAME_DIR/plugins`. Their DLLs
are injected native code. A plugin is not sandboxed and has the same authority
as the user running Victoria II. Load DLLs and manifests only from trusted
sources.

## CLI

```powershell
smedley_cli --game-dir "C:\Games\Victoria 2" --mod mod\GFM.mod --no-inject --detach
smedley_cli --profile "C:\Profiles\gfm observer.toml" --dry-run
smedley_cli --game-dir "C:\Games\Victoria 2" --discover
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin "plugins\campaign runner.toml" --save "C:\Users\me\Documents\Paradox Interactive\Victoria II\save games\run.v2" --observe
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin plugins\campaign_runner.toml --save "C:\Users\me\Documents\Paradox Interactive\Victoria II\save games\run.v2" --speed 3 --start-paused --detach
smedley_cli --history
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin plugins\telemetry.toml --telemetry --telemetry-category lifecycle --telemetry-category state --telemetry-sample-days 7 --telemetry-queue-capacity 512 --detach
```

`--dry-run` prints the resolved command line and structured preflight
diagnostics without starting a process. `--discover` enumerates valid
`GAME_DIR/plugins/*.toml` manifests and `GAME_DIR/mod/*.mod` descriptors in a
stable order. `--no-inject` starts the verified game with ordinary selected mods
but does not load the kernel or native plugins.

`--telemetry` enables the profile telemetry settings. `--telemetry-output`,
`--telemetry-category`, `--telemetry-sample-days`,
`--telemetry-queue-capacity`, and `--telemetry-overwrite` override their corresponding profile fields. The
shared preflight requires selected plugin ID `telemetry` when telemetry is
enabled, accepts only `lifecycle` and `state` categories, and validates the
documented numeric ranges. Safe mode warns and ignores every telemetry control.

## Run History

Every `Launch()` attempt writes one atomic, human-readable TOML record under
`%LOCALAPPDATA%\Smedley\runs`. Records use schema version 1 and contain a
stable run ID, UTC start timestamp, outcome, known PID/exit code, profile and
launch settings, resolved executable/command line, selected mod descriptors,
and selected plugin IDs and manifests. They also contain paths to the Smedley
and Victoria II logs, user directory, `economy_trace.csv`, telemetry JSON Lines
trace, and source save when
those paths can be derived. History records only reference game content; they
never copy saves, logs, mods, plugins, or game files.

`--history` lists the newest 20 records concisely. A normal CLI launch prints
the record path after it starts. The Win32 launcher's **Recent runs** window
uses the same records, opens a selected metadata file on double-click, and can
open linked locations that still exist.

Run metadata is best-effort. A write failure appears as a launcher warning and
does not convert a successful game launch into a failed one. Detached launches
are recorded as `started`: the launcher does not own a process watcher after it
returns. Blocking CLI launches update the same record to `exited` with the exit
code when Windows provides it. A malformed individual TOML record is reported
as a history diagnostic without hiding other records.

## Win32 GUI

`smedley_launcher.exe` is installed beside the other Smedley binaries when
`cmake --install` targets the configured game directory. It provides file and
folder browse dialogs for profile, game, and save paths; a single optional mod;
and a checkable list of discovered native plugins. `Refresh` rediscovers mods
and plugins while preserving valid selections. The diagnostics panel contains
both discovery and preflight messages, and Launch remains disabled while the
shared preflight reports an error.

The profile schema and CLI can represent multiple mods, but the current GUI
offers one mod selector. Loading a multi-mod profile preserves every path and
blocks launch until the user explicitly replaces that selection with one mod;
it never silently drops hidden mod or plugin paths.

Safe mode starts the verified game without injection. Plugin DLLs are native
code and are not sandboxed. GUI launches are detached and report either the
new process ID or the launch diagnostics. Profile save/load uses the same TOML
schema as the CLI. A loaded profile's `kernel` and `detach` values are preserved
when saving; GUI launches themselves are always detached so the launcher stays
responsive. **Recent runs** opens the shared local history rather than keeping a
separate GUI-only list.

The GUI includes telemetry enablement, an optional JSON Lines output browse
field, a compact category selector (`Lifecycle + state`, `Lifecycle only`, or
`State only`), sample-days input, queue-capacity input, and overwrite checkbox. These controls build
the same profile and use the same preflight as the CLI.

The current profile API supports unattended save loading, observer mode, and an
optional observer view tag. It also supports an initial speed from 1 through 5
and a start-paused checkbox. Non-default run controls require both a save and
the `campaign_runner` plugin. Observer mode cannot start paused because its
watchdog must observe simulation advancement.

## Current limits

The core resolves exact plugin IDs listed in optional `dependencies` and
`conflicts` arrays. It does not yet resolve version ranges, write plugin
settings, or validate game/mod runtime behavior.
It only accepts the one verified executable identity and x86 PE kernel/plugin
DLLs. No-injection and injected launches still require the same verified game.
Run history records launcher outcomes, not game health: `started` does not
prove injection, plugin initialization, campaign loading, save integrity, or
later process exit for detached runs.
