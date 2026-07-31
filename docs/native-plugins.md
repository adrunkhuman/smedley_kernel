# Native plugins

Native plugins are trusted x86 Windows DLLs loaded into Victoria II. They are
not sandboxed and have the same filesystem, memory, and process access as the
user running the game. Prefer the constrained scripting API when its copied
state and queued operations are sufficient.

## Lifecycle ABI v1

New plugins can use the compiler-independent lifecycle interface in
[`include/smedley/plugin_abi.h`](../include/smedley/plugin_abi.h). The loader
prefers the `SmedleyPluginGetApiV1` export. Existing bundled plugins continue to
work through the legacy `CreatePlugin` C++ interface.

ABI v1 deliberately exposes lifecycle only. It does not expose Victoria II
objects, event hooks, logging, telemetry, or mutation operations. Those require
separate versioned C interfaces with verified ownership and thread contracts;
do not cast host pointers or copy the legacy C++ classes into an ABI-v1 plugin.

The host and plugin follow this sequence:

1. The host zeroes `SmedleyPluginApiV1`, sets `struct_size` and `version`, then
   calls `SmedleyPluginGetApiV1`.
2. The plugin validates those two fields, fills every v1 field, leaves all
   reserved values zero, and returns `SMEDLEY_PLUGIN_SUCCESS`.
3. The host validates the table, allocates aligned zeroed instance storage, and
   calls `create` followed by `load`.
4. If `load` fails, the host immediately calls `unload` and then `destroy`. When
   the loader is explicitly stopped, successfully loaded plugins receive the
   same callbacks in reverse load order.
5. After `destroy`, the host frees its storage. It does not unload the DLL
   because the injector, not the plugin loader, owns the module reference.

The current game integration has no verified orderly shutdown boundary. It
therefore guarantees these callbacks during initialization rollback, but not on
normal process exit. Do not depend on `unload` for final telemetry flushes,
persistent writes, or other session-completion work. Adding a loader-lock-free
shutdown call remains blocked on a verified native game exit path; plugin
callbacks will not be run from `DllMain`.

`create` must either succeed with a destroyable instance or fail after cleaning
up its own partial work. The host does not call `destroy` after a failed
`create`. A failing `load` may acquire resources incrementally because `unload`
is always called after a failed load attempt. `destroy` must release all
remaining plugin-owned resources before returning.

No callback may throw a C++ exception across the boundary, retain the API table
pointer, retain the host storage after `destroy`, or ask the host to free memory
allocated by the plugin. The loader catches accidental exceptions as a final
containment measure, but that does not make exception propagation part of the
contract.

Advertising `SmedleyPluginGetApiV1` is authoritative. If discovery, table
validation, `create`, or `load` fails, the loader rejects the plugin and rolls
back earlier plugins; it does not fall back to `CreatePlugin` from the same DLL.
This prevents a broken new entry point from silently selecting an older ABI.

## Minimal C plugin

```c
#define SMEDLEY_PLUGIN_BUILD
#include <smedley/plugin_abi.h>

typedef struct ExampleState {
    uint32_t loaded;
} ExampleState;

static SmedleyPluginResult SMEDLEY_PLUGIN_CALL create_plugin(void *instance, uint32_t size)
{
    if (instance == 0 || size != sizeof(ExampleState)) {
        return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    }
    return SMEDLEY_PLUGIN_SUCCESS;
}

static SmedleyPluginResult SMEDLEY_PLUGIN_CALL load_plugin(void *instance)
{
    ((ExampleState *)instance)->loaded = 1;
    return SMEDLEY_PLUGIN_SUCCESS;
}

static SmedleyPluginResult SMEDLEY_PLUGIN_CALL unload_plugin(void *instance)
{
    ((ExampleState *)instance)->loaded = 0;
    return SMEDLEY_PLUGIN_SUCCESS;
}

static void SMEDLEY_PLUGIN_CALL destroy_plugin(void *instance)
{
    (void)instance;
}

SMEDLEY_PLUGIN_EXPORT SmedleyPluginResult SMEDLEY_PLUGIN_CALL
SmedleyPluginGetApiV1(SmedleyPluginApiV1 *api)
{
    if (api == 0 || api->struct_size != sizeof(*api)
        || api->version != SMEDLEY_PLUGIN_ABI_VERSION_V1) {
        return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    }
    api->instance_size = sizeof(ExampleState);
    api->instance_alignment = 4;
    api->create = &create_plugin;
    api->load = &load_plugin;
    api->unload = &unload_plugin;
    api->destroy = &destroy_plugin;
    return SMEDLEY_PLUGIN_SUCCESS;
}
```

Build the DLL for x86 with MSVC and export exactly
`SmedleyPluginGetApiV1`. A plugin still needs a TOML manifest under the game's
`plugins` directory:

```toml
id = "example"
name = "Example native plugin"
version = "1.0.0"
module = "example.dll"
```

The launcher accepts a module exporting either the v1 symbol or legacy
`CreatePlugin`, verifies that it is an x86 PE image, and applies the ordinary
manifest dependency and conflict checks before injection.

## Validation

The x86 Release tests compile the public header as C, dynamically discover and
run an independent C fixture DLL, exercise lifecycle failures and rollback, and
verify launcher preflight for a v1-only export. `dumpbin /exports` confirms that
the fixture exposes only undecorated `SmedleyPluginGetApiV1`.

Supplied-game run `a0af29ca-1b35-42f4-abf0-1a29701e3288` loaded that fixture
through the installed launcher, injector, and kernel and reached a responsive
main window. Run `5fca45a1-a770-41e4-8eab-ed472c0ddfc9` loaded the same v1
fixture followed by legacy `campaign_runner` in one responsive process. These
runs verify startup selection and compatibility, not normal-exit callbacks.
