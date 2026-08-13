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

`smedley_dll_boundary_audit` requires the explicit `LoadPlugins`,
`LoadPluginsThread`, and all versioned C service-provider exports from the
kernel. It also checks exact plugin export sets and rejects every
`smedley_kernel.dll` import from production plugins. CMake automatic exports are
disabled for the kernel and all plugins.

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

## Legacy bindings

The legacy generated C++ engine bindings and
`SMEDLEY_REGENERATE_BINDINGS` option were removed. They are not required by,
and cannot be regenerated as part of, the current build. Add engine access as a
narrow kernel-private layout, document its mapping evidence, and do not restore
the mirror headers or generator to support new code. Historical model inputs and
their disposition are recorded in
[`mappings/research/retired-engine-mirrors.md`](mappings/research/retired-engine-mirrors.md).
