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
view_tag = "ENG" # optional; three uppercase ASCII alphanumeric characters (for example D01) and requires observer
speed = 5 # optional; 1 through 5, applied after unattended save loading
start_paused = false # optional; requires a save and campaign_runner; incompatible with observer
detach = false
run_days = 365 # optional; 1 through 1000000, mutually exclusive with run_until_date_raw
run_until_date_raw = 123456 # optional signed int raw-date target
quit_after_run = false # optional; native exit only after successful exact-target completion
run_timeout_seconds = 600 # persisted default; used only when a run target exists, 1 through 86400
telemetry_enabled = false
telemetry_output = "C:\\traces\\gfm.jsonl" # optional; default is per-run under %LOCALAPPDATA%
telemetry_categories = ["lifecycle", "state"] # lifecycle, state, or both
telemetry_sample_days = 1 # 1 through 365
telemetry_queue_capacity = 1024 # 64 through 8192
telemetry_overwrite = false # required to replace an existing .jsonl file
scripts = ["scripts/examples/country_log.lua"] # optional Lua source under GAME_DIR/scripts
script_instruction_budget = 100000 # 1000 through 10000000 per chunk/callback
script_memory_bytes = 8388608 # 262144 through 67108864 per script
script_queue_capacity = 256 # 16 through 4096 copied events
```

`mods` selects ordinary Victoria II descriptors. Each descriptor must be under
`GAME_DIR/mod`, and the launcher supplies Victoria II's verified syntax:
`-mod=mod/<descriptor>.mod`. A mod is data interpreted by the game.

`plugins` selects Smedley TOML manifests under `GAME_DIR/plugins`. Their DLLs
are injected native code. A plugin is not sandboxed and has the same authority
as the user running Victoria II. Load DLLs and manifests only from trusted
sources.

`scripts` selects source-visible Lua files under `GAME_DIR/scripts`. It requires
the trusted built-in `scripting` plugin, but user scripts execute in independent
bounded Lua states and never receive Victoria II's Lua state. See
[`scripting.md`](scripting.md) for the API and limits.

## CLI

```powershell
smedley_cli --game-dir "C:\Games\Victoria 2" --mod mod\GFM.mod --no-inject --detach
smedley_cli --profile "C:\Profiles\gfm observer.toml" --dry-run
smedley_cli --game-dir "C:\Games\Victoria 2" --discover
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin "plugins\campaign runner.toml" --save "C:\Users\me\Documents\Paradox Interactive\Victoria II\save games\run.v2" --observe
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin plugins\campaign_runner.toml --save "C:\Users\me\Documents\Paradox Interactive\Victoria II\save games\run.v2" --speed 3 --start-paused --detach
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin plugins\campaign_runner.toml --plugin plugins\telemetry.toml --telemetry --save "C:\Users\me\Documents\Paradox Interactive\Victoria II\save games\run.v2" --speed 5 --run-days 365 --run-timeout-seconds 600 --detach
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin plugins\campaign_runner.toml --save "C:\Users\me\Documents\Paradox Interactive\Victoria II\save games\run.v2" --run-days 1 --quit-after-run --detach
smedley_cli --history
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin plugins\telemetry.toml --telemetry --telemetry-category lifecycle --telemetry-category state --telemetry-sample-days 7 --telemetry-queue-capacity 512 --detach
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin plugins\scripting.toml --script scripts\examples\country_log.lua --detach
```

`--dry-run` prints the resolved command line and structured preflight
diagnostics without starting a process. `--discover` enumerates valid
`GAME_DIR/plugins/*.toml` manifests and `GAME_DIR/mod/*.mod` descriptors in a
stable order. `--no-inject` starts the verified game with ordinary selected mods
but does not load the kernel or native plugins.

`--quit-after-run` enables `quit_after_run`. It has no disabling counterpart, so
a profile value of `true` remains active unless the profile is edited.

`--telemetry` enables the profile telemetry settings. These options override
their corresponding profile fields:

- `--telemetry-output`
- `--telemetry-category`
- `--telemetry-sample-days`
- `--telemetry-queue-capacity`
- `--telemetry-overwrite`

Shared preflight requires selected plugin ID `telemetry` when telemetry is
enabled. It accepts only `lifecycle` and `state` categories and validates the
documented numeric ranges. Safe mode warns and ignores every telemetry control.

`--script` may be repeated. The three `--script-*` limit options override the
profile defaults. Shared preflight contains scripts under `GAME_DIR/scripts`,
requires plugin ID `scripting`, and warns that all script controls are ignored
in safe mode.

## Run history

Every `Launch()` attempt writes one atomic, human-readable TOML record under
`%LOCALAPPDATA%\Smedley\runs`. Records use schema version 1 and contain a
stable run ID, UTC start timestamp, outcome, known PID/exit code, profile and
launch settings, resolved executable/command line, selected mod descriptors,
and selected plugin IDs and manifests. They also contain paths to the Smedley
and Victoria II logs, user directory, telemetry JSON Lines trace, and source
save when those paths can be derived. History records only
reference game content; they
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

Installation provides four bundled plugin manifests: `campaign_runner`,
`interest_bug_fix`, `telemetry`, and `scripting`. It removes retired bundled
plugin DLLs and manifests rather than leaving them discoverable. The native
`pop_money_fixture` validation target is build-only and is not installed.

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
`State only`), sample-days input, queue-capacity input, and overwrite checkbox.
These controls build the same profile and use the same preflight as the CLI.

The GUI preserves and launches script paths and limits loaded from a profile.
Adding or removing scripts currently uses the profile file or CLI; the GUI does
not yet provide a script list editor.

The current profile API supports unattended save loading, observer mode, an
optional observer view tag, an initial speed from 1 through 5, and a
start-paused checkbox. Observer mode cannot start paused because its watchdog
must observe simulation advancement. If the viewed country is annexed, the
campaign runner selects the first living scheduled-AI country at the verified
native Annex entry boundary and changes only the camera tag; player ownership
and the AI scheduler remain unchanged. Other country disappearances use the
timer-driven safe-switch fallback.

Observer monitoring also resumes an unexpected native pause after verifying
the pause toggle. It stops monitoring if the game cannot resume or reports an
invalid pause state; bounded benchmark runs defer this recovery to the observer
watchdog instead of reporting `unexpected_pause` first.

| Control | Requirements and behavior |
| --- | --- |
| Non-default run controls | Require a save and the `campaign_runner` plugin. |
| `run_days` or `run_until_date_raw` | Request a bounded benchmark interval. Require injection, a selected save, `campaign_runner`, `start_paused = false`, and `detach = true`. Safe mode warns that stale targets are ignored. |
| `quit_after_run` | Requires a bounded run target. After exact-target completion, attempts to queue `benchmark.completed` when telemetry is available, then requests Victoria II's verified native quit operation. The request does not flush or drain plugins; queued records and `telemetry.summary` may be lost at process exit. A failed native-request validation or readback leaves the campaign paused and open. It never applies to failed runs. |
| `view_tag` with a run target | Rejected because the view switch is asynchronous after simulation resumes. Observer mode itself remains optional. |
| Custom timeout without a target | Inert and produces a preflight warning. It neither requires campaign automation nor reaches the plugin. |

The GUI exposes Run days, Run target raw, Timeout seconds, and **Quit after
successful bounded run** through the same preflight.

When one selected mod declares a safe `user_dir`, save preflight and run-record
links use that mod-specific `save games` and log directory. Multiple distinct
mod user directories are ambiguous and rejected instead of guessing which one
the game will use.

By default the process is intentionally left paused and open on completion.
Every safe failure also remains open. An explicitly enabled `quit_after_run`
requests the verified native game exit only after exact-target success. No
checkpoint save, benchmark save, final plugin drain, or final game-state
assertion is implemented or claimed.

## Current limits

The core resolves exact plugin IDs listed in optional `dependencies` and
`conflicts` arrays. It does not yet resolve version ranges, write plugin
settings beyond the built-in telemetry and scripting contracts, or validate
game/mod runtime behavior.
It only accepts the one verified executable identity and x86 PE kernel/plugin
DLLs. No-injection and injected launches still require the same verified game.
The pre-annexation observer handoff is mapped only for that supported
executable; it is not a version-independent Victoria II facility.
Run history records launcher outcomes, not game health: `started` does not
prove injection, plugin initialization, campaign loading, save integrity, or
later process exit for detached runs.
