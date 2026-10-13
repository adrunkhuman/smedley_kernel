# Licensing

Smedley is **LGPL-3.0 by default**, with the independent components below
licensed under **MIT**. This is a component split, not a choice of license for
the whole repository.

Original Smedley remains copyright its authors. This fork includes modifications
by adrunkhuman in 2026. Existing copyright and third-party notices remain intact.

## Source licenses

MIT applies recursively to these directories, with the one stated exception:

| MIT component | Source paths |
| --- | --- |
| Launcher frontends | `apps/launcher/cli/`, `apps/launcher/gui/` |
| Offline trace tool | `apps/trace/` |
| Public C service declarations | `include/`, except `include/smedley/logging_api.h` |
| Shared telemetry registry | `lib/telemetry_registry/` |
| Independent plugins | `plugins/campaign_runner/`, `plugins/scripting/`, `plugins/telemetry/` |
| Development and research utilities | `tools/` |
| Example scripts | `examples/` |

See the [MIT license](LICENSES/MIT.txt), copyright 2026 adrunkhuman.
Everything else retains the [LGPL v3](LICENSE.txt), including `kernel/`,
`apps/launcher/core/`, `plugins/interest_bug_fix/`, the logging API header,
and shared top-level build files. LGPL v3 incorporates the accompanying
[GPL v3 text](LICENSES/GPL-3.0.txt).

The split follows source history before and after upstream commit `d39454b`.
The launcher core and interest fix retain LGPL because their ancestry is mixed
or uncertain; the logging header retains upstream level declarations. New code
inside the kernel remains part of the LGPL library. This map covers the current
source tree, not historical versions of similarly named files.

Third-party code and license texts retain their own terms, regardless of path.
See [third-party notices](THIRD_PARTY_NOTICES.md) for Lua, MinHook, and toml++.
No license to Victoria II, its assets, or third-party mods is granted here.

## Binary distributions

An MIT source component does not make its linked executable MIT-only. The
launcher executables statically include the LGPL launcher core; plugins use the
LGPL kernel through C services. Distributions must preserve the applicable
notices and meet LGPL source, modification, and replacement/relinking obligations.
Do not prohibit reverse engineering needed to debug modifications to LGPL parts.

CMake installs this map, license texts, and dependency notices under
`<GAME_DIR>/smedley-licenses/`. Keep that directory with distributed binaries.
Provide the corresponding source for the exact distributed LGPL build and the
code/build materials needed to relink statically combined applications; a link
to a changing branch alone is not a substitute. See LGPL sections 3–4 and the
[build instructions](https://github.com/adrunkhuman/smedley_kernel/blob/master/docs/development.md).
