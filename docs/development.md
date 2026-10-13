# Development

## Prerequisites

Smedley requires these build tools:

- [CMake >= 3.20](https://cmake.org/download/)
- [Microsoft C++ Compiler (MSVC)](https://visualstudio.microsoft.com/vs/community/)
- Git and network access for the initial pinned dependency fetch

Smedley currently supports only MSVC, the compiler used to build `v2game.exe`.

The initial configure downloads pinned GoogleTest and Lua 5.1.5 source archives.
Lua is linked privately into the optional scripting plugin; it is not resolved
from Victoria II's Lua DLLs. See [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
The configure also fetches pinned MinHook v1.3.4, linked privately into the x86
kernel for function-entry detours.
After a failed dependency download, rerun configure. If CMake retained a broken
partial checkout, remove only that dependency's directory under `build/_deps`
and configure again; do not delete checked-in third-party material.

## Building

Configure CMake with the Visual Studio generator and target the x86
architecture. From the repository root, run:

```powershell
New-Item -ItemType Directory -Force "C:\Games\Victoria 2\plugins"
cmake -S . -B build -A Win32 -DV2_GAME_DIR="C:\Games\Victoria 2"
```

Next, build the binaries:

```powershell
cmake --build build --config Release
```

## Installing

Optionally install Smedley, its plugins, `smedley_launcher.exe`,
`smedley_cli.exe`, and `smedley_trace.exe` in the configured game directory:

```powershell
cmake --install build --config Release --prefix "$PWD\build\install"
```

`V2_GAME_DIR` and its `plugins` directory must exist when CMake configures the
project because install rules target those directories directly. The prefix
redirects dependency-owned relative install artifacts to `build\install`; it
does not relocate Smedley's absolute game destinations.

The installed kernel is unusable without the Smedley launcher and injection
bootstrap path.

## Testing

Smedley uses GoogleTest and CTest. Run the complete test suite:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The suite includes `smedley_engine_ownership_audit` and
`smedley_engine_ownership_tool_tests`. They reject recreated
`smedley_game_state` or `smedley_game_runtime` targets, duplicate ownership of
`game_state/src` implementation files, and internal game-state imports in
migrated plugins.

`smedley_clausewitz_workload_inventory_tool_tests` covers supported event-key
recognition, static lead extraction, deterministic ordering, and CLI output-path
validation for the Clausewitz workload inventory.

`smedley_dll_boundary_audit` requires the explicit `LoadPlugins`,
`LoadPluginsThread`, and all versioned C service-provider exports from the
kernel. It also checks exact plugin export sets and rejects every
`smedley_kernel.dll` import from production plugins. CMake automatic exports are
disabled for the kernel and all plugins.

For a focused check while changing target boundaries, run from the repository
root:

```powershell
python tools/check_engine_ownership.py --root .
python tools/tests/check_engine_ownership_test.py
```

## Validation limits

Host tests verify the x86 ABI, checked failure paths, and source/build layering;
they do not establish successful behavior in Victoria II. Before a runtime test,
verify the exact supported executable identity and active mapping signatures,
use an x86 Release build and a disposable save, and record enabled plugins,
mods, settings, and mapping version. A successful launch alone does not prove
injection, plugin initialization, campaign control, observer behavior, or
telemetry completeness. See the [engine boundary](architecture.md) and
[mapping evidence](../mappings/README.md).

New engine access belongs in narrow kernel-private layouts with mapping
evidence. There is no generated-bindings build step; see
[ownership rules](architecture.md#ownership).

## C++ style

Use four spaces for indentation; do not use tabs. Begin every header with
`#pragma once`.

Use a [K&R-like brace style](https://en.wikipedia.org/wiki/Indentation_style#K&R_style):
put function, class, and struct opening braces on the next line, and
control-flow opening braces on the same line as the statement.

| Name | Convention |
| --- | --- |
| Functions, classes, and structs | PascalCase |
| Variables | Lowercase letters and `snake_case` are allowed |
| Class methods | Typically PascalCase |
| Property-like methods | Lowercase `snake_case`: getters with no arguments, setters with one argument |
| Private and protected member variables | Leading underscore |
| Public member variables | Normal variable naming |

```cpp
class ExampleClass
{
    int _foo;

public:
    inline int foo() { return _foo; }
    inline void foo(int val) { _foo = val; }

    void ExampleMethod()
    {
        if (_foo > 0) {
            foo(0);
        }
    }
};
```

Do not indent case clauses within a switch. Avoid fallthrough whenever possible.

```cpp
switch (ch) {
case 'a':
    // ...
    break;
case 't':
    // ...
    break;
default:
}
```
