# Building Smedley

## Prerequisites

Smedley requires the following to be built:

    * [CMake >= 3.20](https://cmake.org/download/)
    * [Microsoft C++ Compiler (MSVC)](https://visualstudio.microsoft.com/vs/community/)

Smedley currently only supports building with MSVC (the compiler used to build v2game).

The initial configure downloads pinned GoogleTest and Lua 5.1.5 source archives.
Lua is linked privately into the optional scripting plugin; it is not resolved
from Victoria II's Lua DLLs. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Building

First one must configure CMake to use the Visual Studio generator and target the x86 architecture. In command prompt or your preferred shell environment, run the following command in the project directory:

```
cmake -S . -B build -A Win32
```

Next, build the binaries:

```
cmake --build build --config Release
```

(Optional) Install Smedley, plugins, `smedley_launcher.exe`, `smedley_cli.exe`,
and `smedley_trace.exe` in the game directory

```
cmake --install build
```

Keep in mind, while the kernel may be installed it is unusable without the bootstrapper.

## Regenerating bindings

Generated Victoria II bindings are checked in, so a normal build does not need
Python packages or Cog. Contributors changing a model can regenerate bindings
with an isolated environment:

```
uv run --with cogapp --with toml --with "pydantic<2" python -m cogapp -r -I codegen @generatedfiles.txt
```

Review the generated diff before committing it. The generator and generated
wrappers preserve an x86 MSVC ABI and are not portable bindings.
