# Smedley

Instrumentation, automation, and extensions for **Victoria II: Heart of Darkness
3.04**. Smedley runs inside the original game; it is not a replacement engine.
Use the launcher for normal play, the CLI for unattended runs, and constrained
Lua for ordinary mod extensions. Native plugins support engine fixes and work
that needs synchronous callbacks or high-volume capture.

## Components

| Component | Purpose |
| --- | --- |
| `smedley_launcher.exe` / `smedley_cli.exe` | Configure, validate, and launch the game through shared profiles. |
| `campaign_runner` | Load saves, enter observer mode, set speed, and run bounded benchmarks. |
| `telemetry` / `smedley_trace.exe` | Capture structured observations; validate, compare, and export them offline. |
| `scripting` | Run Lua 5.1 source files against copied state and queued operations. |
| `interest_bug_fix` | Optionally pay omitted state interest to depositor POPs. Changes gameplay. |

The kernel owns engine access. Plugins use versioned C services, not raw game
objects or a C++ DLL ABI. See [architecture](docs/architecture.md).

## Supported game

Only the English Windows x86 executable below is supported. The launcher rejects
other builds before injection.

| Property | Value |
| --- | --- |
| Version | Heart of Darkness 3.04 |
| SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| File size | `12294656` bytes |

```powershell
python tools/validate_mappings.py "C:\Games\Victoria 2\v2game.exe"
```

## Start

1. [Build and install](docs/development.md) with Windows, MSVC x86, and CMake.
2. Run `smedley_launcher.exe` from the game directory.
3. Select the game directory, mods, and trusted plugins. Review preflight
   diagnostics, then launch.

For automation, validate a profile without starting the game, then launch it:

```powershell
smedley_cli --profile "C:\Profiles\observer.toml" --dry-run
smedley_cli --profile "C:\Profiles\observer.toml" --detach
```

[Launcher documentation](docs/launcher.md) covers profiles, CLI options, save
loading, observer mode, and bounded runs. Bounded runs do not save the campaign.
Safe mode launches the verified game with ordinary mods but **without injection**;
plugin, script, telemetry, and campaign-automation settings are ignored.

Native plugins have the game's full process and user authority. Lua is
constrained execution, not a security sandbox. Install only trusted extensions.
Run history records launcher activity—not successful injection or game health.

## Documentation

| Task | Read |
| --- | --- |
| Build, install, test, and follow C++ style | [Development](docs/development.md) |
| Launch and automate campaigns | [Launcher](docs/launcher.md) |
| Capture and analyze game state | [Telemetry](docs/telemetry/README.md) |
| Write Lua extensions | [Scripting](docs/scripting.md) |
| Enable the POP interest fix | [Interest fix](docs/interest-fix.md) |
| Understand fiscal and credit behavior | [Modder reference](docs/fiscal-credit.md) |
| Develop native plugins | [Plugins](docs/plugins.md) |
| Understand engine ownership and safety | [Architecture](docs/architecture.md) |
| Audit reverse-engineered claims | [Mapping catalog](mappings/README.md) |
| Inspect Clausewitz script workloads | [Workload inventory](docs/workload-inventory.md) |
| Contribute | [Contributing](CONTRIBUTING.md) |

## License

LGPL v3 for the inherited core and other retained components; MIT-0 for the
independent components listed in the [license map](LICENSE.md). Third-party
dependencies keep their own licenses.
