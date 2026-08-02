# Building Smedley

## Prerequisites

Smedley requires these build tools:

- [CMake >= 3.20](https://cmake.org/download/)
- [Microsoft C++ Compiler (MSVC)](https://visualstudio.microsoft.com/vs/community/)

Smedley currently supports only MSVC, the compiler used to build `v2game.exe`.

The initial configure downloads pinned GoogleTest and Lua 5.1.5 source archives.
Lua is linked privately into the optional scripting plugin; it is not resolved
from Victoria II's Lua DLLs. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

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

## Installing

Optionally install Smedley, its plugins, `smedley_launcher.exe`,
`smedley_cli.exe`, and `smedley_trace.exe` in the configured game directory:

```powershell
cmake --install build
```

The installed kernel is unusable without the Smedley launcher and injection
bootstrap path.

## Regenerating bindings

Generated Victoria II bindings are checked in, so a normal build does not need
Python packages or Cog. Contributors changing a model can regenerate bindings
with an isolated environment:

```powershell
uv run --with cogapp --with toml --with "pydantic<2" python -m cogapp -r -I codegen @codegen/generatedfiles.txt
```

The paths in `codegen/generatedfiles.txt` are the generated outputs. Review their diff
against the model files before committing. The generator and generated wrappers
preserve an x86 MSVC ABI and are not portable bindings.

Model entries describe reverse-engineering candidates and generated call
wrappers; they do not imply an installed Smedley hook or current runtime
evidence. In particular, the retained `CCountry` models for `AddToSphere`,
`MonthlyUpdate`, and `Westernize` are unhooked search leads. Active hooks and
their evidence are defined by `mappings/v2game-3.04.toml`.
