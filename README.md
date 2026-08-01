# Smedley

Smedley is an instrumentation, automation, and native extension framework for
Victoria II: Heart of Darkness 3.04. It includes a graphical launcher, a
scriptable CLI, plugin preflight, unattended campaign loading, safe observer
mode, economy tracing, and bounded structured telemetry. Ordinary players and modders use profiles and the
launcher; constrained Lua scripts can extend supported behavior without C++.
C++ is only needed to build Smedley or write native plugins.

New native plugins can use the narrow versioned C lifecycle ABI documented in
[`docs/native-plugins.md`](docs/native-plugins.md); existing plugins retain a
legacy compatibility path. A separate C capability table provides copied daily
country events without exposing game pointers. Native plugins may submit bounded
typed records through the telemetry C extension API in
[`include/smedley/telemetry.h`](include/smedley/telemetry.h). They resolve
telemetry dynamically and never require it as a dependency.

This fork currently supports one exact English x86 `v2game.exe`:

| Property | Supported value |
| --- | --- |
| Version | Victoria II: Heart of Darkness 3.04 |
| SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| Size | `12294656` bytes |

The launcher rejects any other executable before injection.

## Install And Launch

Build and install the x86 Release configuration by following
[`BUILDING.md`](BUILDING.md). Installed files are placed in the configured game
directory.

Run `smedley_launcher.exe` from the Victoria II directory. It automatically
discovers the game, ordinary `.mod` descriptors, and Smedley plugin manifests.
Choose a mod and trusted plugins, inspect the diagnostics, then launch. Safe
mode starts the verified original game without injecting Smedley, which provides
a recovery path for broken plugin configurations.

Profiles are documented TOML files shared by the GUI and CLI. They preserve the
game directory, mod and plugin selections, campaign save, observer settings,
initial speed, pause state, and launch behavior. See
[`docs/launcher.md`](docs/launcher.md) for the schema and examples.

## CLI

The CLI supports desktop-free automation and dry-run validation:

```powershell
smedley_cli --game-dir "C:\Games\Victoria 2" --discover
smedley_cli --game-dir "C:\Games\Victoria 2" --mod mod\GFM.mod --no-inject --detach
smedley_cli --profile "C:\Profiles\gfm-observer.toml" --dry-run
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin plugins\campaign_runner.toml --save "C:\Users\me\Documents\Paradox Interactive\Victoria II\save games\run.v2" --speed 5 --observe --view-tag ENG --detach
```

Paths may contain spaces and Unicode characters. `--dry-run` performs the same
shared preflight as a real launch without creating a process.

## Recent Runs

Each real launcher attempt writes a small TOML metadata record in
`%LOCALAPPDATA%\Smedley\runs`; no game content is copied. The CLI command
`smedley_cli --history` lists recent records, and the GUI's **Recent runs**
button opens them and any linked logs, user directory, economy trace, or source
save that still exists. Detached GUI and CLI launches are recorded as started,
not exited, because the launcher does not watch them after it returns.

## Built-In Tools

`campaign_runner` loads a selected save through Victoria II's native frontend
operations. It can choose speed 1 through 5, preserve a paused start, or enter
observer mode. Observer mode returns every country to AI control, enables full
map visibility, suppresses modal message pauses, safely changes the viewing
country, and verifies each transition against live game state.

It also has bounded fixed-date benchmark runs: `--run-days 365 --detach` resumes
a configured campaign, pauses it at the exact target date, and reports typed
telemetry when available. It deliberately leaves the game open and paused; this
is not a verified clean exit, save, or state-assertion workflow.

`economy_trace` is the legacy CSV tool for daily country treasury snapshots.
Use `telemetry` for versioned JSON Lines records and add `economic_telemetry`
for bounded world economic snapshots.

`interest_probe` is a read-only reverse-engineering tool for the creditor-POP
interest investigation. It writes bounded creditor, destination-bank, and
destination POP-savings observations to `interest_probe.csv` and individual
bank deltas to `interest_probe_transfers.csv` without applying the historical
fix. Its fields and limits are documented in
[`mappings/INTEREST_FIX.md`](mappings/INTEREST_FIX.md).

`interest_fix` is the independently selectable gameplay fix. It restores each
verified creditor-bank interest transfer to that bank's positive-savings POPs
with exact deterministic integer conservation and records outcomes in
`interest_fix.csv` plus structured health/value telemetry when `telemetry` is
also selected. It is disabled unless its manifest is selected, conflicts with
the historical `v2up` plugin, and intentionally returns interest omitted by
vanilla to depositor POP balances. Comprehensive world-money supply remains
unmapped, so no total-money effect is claimed. See the mapping document before
enabling it.

`economic_telemetry` is a separately selected, read-only producer for bounded
world economic snapshots. It depends on `telemetry`, follows its state sampling
interval and date bounds, and reports traversal health, capacity utilization,
observed treasuries and POP balances, savings, and explicitly provisional
credit/state candidates. It keeps liquid holdings and financial claims separate
instead of inventing a double-counted world-money total.

Native contributors can separately build the non-installed
`pop_money_fixture` target. Explicitly selecting its manifest performs one
reversible `+1000/-1000` POP money ABI check; ordinary players should use the
read-only probe instead.

`telemetry` is the opt-in JSON Lines telemetry plugin. Enable it in a profile,
the CLI, or the native launcher and select its trusted manifest like any other
native plugin. It records lifecycle events and sampled country economy state to
an explicit `.jsonl` output or `%LOCALAPPDATA%\Smedley\traces\<run-id>.jsonl`.
See [`docs/telemetry.md`](docs/telemetry.md) for the schema, configuration, and
current evidence limits. Country economy records are state snapshots, not AI
decision reasoning.

`smedley_trace` validates, summarizes, compares, filters, and exports telemetry
traces without external dependencies. Run `smedley_trace summary TRACE.jsonl` or
see [`docs/telemetry.md`](docs/telemetry.md) for all commands.

`scripting` runs selected source-visible Lua 5.1 files without exposing
Victoria II's mutable Lua state. Scripts receive copied daily snapshots and can
log, schedule an in-memory callback, or queue the verified native pause
transaction. Independent memory limits, instruction limits, a nonblocking
bounded event queue, and per-script error disablement contain failures. See
[`docs/scripting.md`](docs/scripting.md) for profile settings, examples, and the
explicit non-sandbox trust boundary.

`v2up` and `dailyupdate_example` are inherited native plugin examples. Gameplay
changes remain opt-in.

## Trust And Limits

Smedley plugins are native DLLs. They are not sandboxed and have the same access
as the user running Victoria II. Install only open, trusted plugins. Ordinary
Victoria II data mods do not cross this native-code trust boundary.

The current release supports Windows, MSVC x86, and the executable identified
above. The native C ABI currently covers lifecycle, not game services. Plugin
dependency versions, general third-party plugin settings, broad
AI decision telemetry, profiling, and profiler-backed engine optimizations remain in active
development. Normal-exit plugin callbacks also await a verified game shutdown
boundary. See the repository's GitHub issues for the current roadmap and
[`mappings/`](mappings/) for reverse-engineering evidence.
