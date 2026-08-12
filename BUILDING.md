# Building Smedley

## Prerequisites

Smedley requires these build tools:

- [CMake >= 3.20](https://cmake.org/download/)
- [Microsoft C++ Compiler (MSVC)](https://visualstudio.microsoft.com/vs/community/)
- Git and network access for the initial pinned dependency fetch

Smedley currently supports only MSVC, the compiler used to build `v2game.exe`.

The initial configure downloads pinned GoogleTest and Lua 5.1.5 source archives.
Lua is linked privately into the optional scripting plugin; it is not resolved
from Victoria II's Lua DLLs. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
The configure also fetches pinned MinHook v1.3.4, linked privately into the x86
kernel for function-entry detours.
After a failed dependency download, rerun configure. If CMake retained a broken
partial checkout, remove only that dependency's directory under `build/_deps`
and configure again; do not delete checked-in third-party material.

## Building

Configure CMake with the Visual Studio generator and target the x86
architecture. From the repository root, run:

```powershell
cmake -S . -B build -A Win32
```

Next, build the binaries:

```powershell
cmake --build build --config Release
```

Run the complete test suite:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The suite includes `smedley_engine_ownership_audit` and
`smedley_engine_ownership_tool_tests`. They reject recreated
`smedley_game_state` or `smedley_game_runtime` targets, duplicate ownership of
`game_state/src` implementation files, and internal game-state imports in
migrated plugins.

`smedley_dll_boundary_audit` requires kernel exports `LoadPlugins`,
`LoadPluginsThread`, `SmedleyGetEventApiV1`, and
`SmedleyGetCampaignControlApiV1`; it also checks exact plugin export sets. The
test fails for missing required kernel exports, unexpected plugin exports, or an
increase in a plugin's frozen transitional import count. It prints each mangled
kernel import so migration progress remains visible.

Production plugin targets disable CMake automatic exports. The kernel retains
automatic exports only as an active temporary compatibility boundary for #41:
`campaign_runner`, `interest_bug_fix`, `scripting`, and `telemetry` still import
C++ symbols. Removing those imports through versioned C services is required
before kernel automatic exports can be disabled.

## Installing

Optionally install Smedley, its plugins, `smedley_launcher.exe`,
`smedley_cli.exe`, and `smedley_trace.exe` in the configured game directory:

```powershell
cmake --install build
```

The installed kernel is unusable without the Smedley launcher and injection
bootstrap path.

## Validation limits

Host tests verify the x86 ABI, checked failure paths, and source/build layering;
they do not establish successful behavior in Victoria II. Before a runtime test,
verify the exact supported executable identity and active mapping signatures,
use an x86 Release build and a disposable save, and record enabled plugins,
mods, settings, and mapping version. A successful launch alone does not prove
injection, plugin initialization, campaign control, observer behavior, or
telemetry completeness. See [`docs/game-state-boundary.md`](docs/game-state-boundary.md)
for the engine boundary and retained runtime evidence.

## Regenerating bindings

Generated Victoria II bindings are checked in, so a normal build does not need
Python packages or Cog. Contributors changing a model can regenerate bindings
with an isolated environment:

```powershell
uv run --with cogapp --with toml --with "pydantic<2" python -m cogapp -r -I codegen @codegen/generated_outputs.txt
```

The paths in `codegen/generated_outputs.txt` are the generated outputs. Review their diff
against the model files before committing. The generator and generated wrappers
preserve an x86 MSVC ABI and are not portable bindings.

`SMEDLEY_REGENERATE_BINDINGS=ON` expects the configured Python interpreter to
provide `cogapp`, `toml`, and Pydantic 1.x.

Model entries describe reverse-engineering candidates and generated call
wrappers; they do not imply an installed Smedley hook or current runtime
evidence. In particular, the retained `CCountry` models for `AddToSphere`,
`MonthlyUpdate`, and `Westernize` are unhooked search leads. Active hooks and
their evidence are defined by `mappings/v2game-3.04.toml`.
