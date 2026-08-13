# Smedley

Smedley is an instrumentation, automation, and native extension framework for
Victoria II: Heart of Darkness 3.04. It includes a graphical launcher, a
scriptable CLI, plugin preflight, unattended campaign loading, safe observer
mode, economy tracing, and bounded structured telemetry.

Players and modders use profiles and the launcher. Constrained Lua scripts can
extend supported behavior without C++. C++ is required only to build Smedley or
write native plugins.

Native plugins use the narrow versioned C lifecycle ABI documented in
[`docs/plugin-development.md`](docs/plugin-development.md). A separate C capability
table provides copied daily country events without exposing game pointers. Native plugins may submit bounded
typed records through the telemetry C extension API in
[`include/smedley/telemetry.h`](include/smedley/telemetry.h). Plugins resolve the
telemetry ABI dynamically. They may still declare `telemetry` as a manifest
dependency when they require the telemetry plugin at runtime.

This fork currently supports one exact English x86 `v2game.exe`:

| Property | Supported value |
| --- | --- |
| Version | Victoria II: Heart of Darkness 3.04 |
| SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| Size | `12294656` bytes |

The launcher rejects any other executable before injection.

## Install and launch

Build and install the x86 Release configuration by following
[`BUILDING.md`](BUILDING.md). Installed files are placed in the configured game
directory.

Run `smedley_launcher.exe` from the Victoria II directory. It automatically
discovers the game, ordinary `.mod` descriptors, and Smedley plugin manifests.
Choose and order one or more mods and trusted plugins, inspect the diagnostics,
then launch. Safe mode starts the verified original game without injecting
Smedley, which provides a recovery path for broken plugin configurations.

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

## Recent runs

Each real launcher attempt writes a small TOML metadata record in
`%LOCALAPPDATA%\Smedley\runs`; no game content is copied. The CLI command
`smedley_cli --history` lists recent records, and the GUI's **Recent runs**
button opens them and any linked logs, user directory, telemetry trace, or source
save that still exists. Detached GUI and CLI launches are recorded as started,
not exited, because the launcher does not watch them after it returns.

## Built-in tools

`campaign_runner` loads a selected save through Victoria II's native frontend
operations. It can choose speed 1 through 5, preserve a paused start, or enter
observer mode. Observer mode returns every country to AI control, enables full
map visibility, suppresses modal message pauses, safely changes the viewing
country, and verifies each transition against live game state. If that country
is annexed, the supported 3.04 executable moves the camera to another living AI
country before game-over evaluation without changing country ownership or AI
scheduling. A timer-driven fallback handles other country disappearances.
Frontend validation failures stop automation rather than guessing at object
state or falling back to synthetic input. Automated return to menu and repeated
campaign loads within one process are not currently supported.

It also has bounded fixed-date benchmark runs: `--run-days 365 --detach` resumes
a configured campaign, pauses it at the exact target date, and reports typed
telemetry when available. The default leaves the game open and paused.
`--quit-after-run` instead requests Victoria II's verified native exit after a
successful exact-target run; failures remain paused and open. It does not save
or provide a generic plugin-drain or state-assertion workflow. With the bundled
drain-capable telemetry plugin, `campaign_runner` first drains accepted records
and joins the writer, then writes the final summary, flushes, and closes the
trace. If telemetry is absent or lacks the drain symbol, quit still proceeds
without a final-summary or telemetry-durability guarantee.

`telemetry` provides versioned JSON Lines records. Profiles can independently
schedule verified country, world, economic, and provisional province families
daily, every seven days, monthly, or yearly, with field and entity filters.
Explicit daily all-entity polling is supported; it is opt-in and reports its
delivery and collection cost rather than silently limiting the request.

`interest_bug_fix` is the independently selectable gameplay fix. It completes
the native creditor-to-bank-to-state pipeline by distributing each state
interest pool among that state's positive-savings POPs with exact deterministic
integer conservation. Native bank recapitalization and bank-to-state allocation
remain unchanged. The fix records outcomes in
`<GAME_DIR>/interest_bug_fix.csv` plus structured health/value telemetry when
`telemetry` is also selected. Each launch truncates this fixed CSV output. The
fix is disabled unless its manifest is selected and intentionally returns
interest omitted by vanilla to depositor POP balances. Serialized state interest
is discarded when a campaign session is first observed. A failed or incomplete
payout schedules another cleanup before the next native daily bank-distribution
pass; successful payouts already leave their pools zero. Comprehensive world-money
supply remains unmapped, so no total-money effect is claimed. See the mapping
document before enabling it.

The telemetry plugin's world scan follows the state sampling interval and date
bounds and reports traversal health, capacity utilization, observed treasuries
and POP balances, savings, and explicitly provisional credit/state candidates.
It keeps liquid holdings and financial claims separate instead of inventing a
double-counted world-money total. Installation removes obsolete bundled plugin
artifacts automatically.

`telemetry` is the opt-in JSON Lines telemetry plugin. Enable it in a profile,
the CLI, or the native launcher and select its trusted manifest like any other
native plugin. It records lifecycle events and configurable world, country,
province, and POP snapshots at daily through yearly cadences to
an explicit `.jsonl` output or `%LOCALAPPDATA%\Smedley\traces\<run-id>.jsonl`.
See [`docs/telemetry.md`](docs/telemetry.md) for the schema, configuration, and
current evidence limits. Unmapped factories, units, battles, technologies, and
decision internals remain unavailable rather than being guessed. State
snapshots are not AI decision reasoning.

`smedley_trace` validates, summarizes, compares, filters, and exports telemetry
traces without external dependencies. It derives verified daily factory value
added and strict country nominal, real, and per-capita GDP from complete
producer, market, and population captures. It also exports reconciled daily POP
cash-flow accounts at country and candidate POP-type scope while retaining
filtered individual detail in JSONL. The `producer-sales`, `pop-cashflow`, and
`country-gdp` commands are strict offline exports: gaps, drops, incomplete
boundaries, or unhealthy terminal summaries fail rather than becoming zero.
Run
`smedley_trace summary TRACE.jsonl` or
see [`docs/telemetry.md`](docs/telemetry.md) for all commands.

`scripting` runs selected source-visible Lua 5.1 files without exposing
Victoria II's mutable Lua state. Scripts receive copied daily snapshots and can
log, schedule an in-memory callback, or queue the verified native pause
transaction. Independent memory limits, instruction limits, a nonblocking
bounded event queue, and per-script error disablement contain failures. See
[`docs/scripting.md`](docs/scripting.md) for profile settings, examples, and the
explicit non-sandbox trust boundary.

## Trust and limits

Smedley plugins are native DLLs. They are not sandboxed and have the same access
as the user running Victoria II. Install only open, trusted plugins. Ordinary
Victoria II data mods do not cross this native-code trust boundary.

The current release supports Windows, MSVC x86, and the executable identified
above. Plugins use versioned C lifecycle and capability APIs and must not link to
`smedley_kernel.dll`. Plugin dependency versions, general third-party plugin
settings, broad AI decision telemetry, profiling, and profiler-backed engine
optimizations are still in active development. A generic pre-exit callback for
plugins other than telemetry remains unimplemented. GitHub issues track future work; see
[`mappings/`](mappings/) for reverse-engineering evidence.
