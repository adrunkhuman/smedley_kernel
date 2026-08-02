# Native plugins

Native plugins are trusted x86 Windows DLLs loaded into Victoria II. They are
not sandboxed and have the same filesystem, memory, and process access as the
user running the game. Prefer the constrained scripting API when its copied
state and queued operations are sufficient.

## Internal shared readers

`smedley_game_state` is a private static library for bounded, observational
Victoria II state readers shared by bundled plugins. It is not a DLL, plugin,
public ABI, or general game-services interface. Keep read-only traversal and
validation in this layer. Mutation, lifecycle, publication, and policy remain
owned by the consuming plugin.

## Lifecycle ABI v1

New plugins can use the compiler-independent lifecycle interface in
[`include/smedley/plugin_abi.h`](../include/smedley/plugin_abi.h). The loader
prefers the `SmedleyPluginGetApiV1` export. Existing bundled plugins continue to
work through the legacy `CreatePlugin` C++ interface.

ABI v1 deliberately exposes lifecycle only. It does not expose Victoria II
objects, logging, or mutation operations. Separate versioned C interfaces expose
bounded capabilities with their own ownership and thread contracts; do not cast
host pointers or copy the legacy C++ classes into an ABI-v1 plugin.

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

The current game integration has a verified native exit request but no generic
pre-exit plugin lifecycle boundary. It therefore guarantees these callbacks
during initialization rollback and explicit loader shutdown, but not on normal
process exit. Do not depend on `unload` for persistent writes or other
session-completion work. The bundled telemetry plugin additionally drains during
explicit unload and through its telemetry-specific pre-exit API. Generic plugin
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

## Daily event capability v1

[`include/smedley/event_api.h`](../include/smedley/event_api.h) exposes the first
kernel capability table. Resolve `SmedleyGetEventApiV1` dynamically from
`smedley_kernel.dll`; do not link against the kernel C++ import library. The
table registers copied daily country snapshots and returns opaque 64-bit
registration handles.

```c
HMODULE kernel = GetModuleHandleW(L"smedley_kernel.dll");
SmedleyGetEventApiV1Fn get_api = (SmedleyGetEventApiV1Fn)GetProcAddress(
    kernel, SMEDLEY_EVENT_GET_API_V1_SYMBOL);
SmedleyEventApiV1 events = {0};
events.struct_size = sizeof(events);
events.version = SMEDLEY_EVENT_API_VERSION_V1;
if (get_api == 0 || get_api(&events) != SMEDLEY_EVENT_SUCCESS) {
    return SMEDLEY_PLUGIN_FAILURE;
}
```

`register_daily` accepts a C callback and caller-owned context pointer. It
returns a nonzero handle that must be passed to `unregister` before that context
is destroyed. Registration is capped at 64 callbacks and performs no game
mutation. A successful `unregister` prevents later calls and waits for a call
already in flight; calling it from the same callback returns
`SMEDLEY_EVENT_BUSY` instead of deadlocking. Returning
`SMEDLEY_EVENT_CALLBACK_DISABLE`, returning an unknown value, or throwing an
accidental C++ exception disables later calls until the owner unregisters.

Callbacks execute synchronously on Victoria II's country-update thread. They
must remain bounded and nonblocking: copy required values into a preallocated
queue and return. Do not allocate, log, access files or the network, wait on a
lock, call `unregister`, retain the event pointer, or throw. The kernel's hook
path uses fixed-capacity storage and lock-free atomics and performs no allocation
or I/O.

`SmedleyDailyEventV1` is a 56-byte copied record. All values are observational:

| Field | Evidence | Meaning |
| --- | --- | --- |
| `game_date_raw` | `provisional` | Current raw Clausewitz date value; runtime progression is correlated |
| `country_tag` | `provisional` | Three-byte country tag plus trailing NUL |
| `treasury_raw` | `provisional` | Current country treasury signed 48.15 fixed-point raw value |
| `has_owned_province` | `provisional` | `1` when the mapped owned-province vector is nonempty; not a complete lifecycle state |
| `country_slot_count` | `provisional` | Country database slots, not necessarily living countries |
| `ai_scheduler_entry_count` | `provisional` | Country AI scheduler entries, not AI decisions |
| `human_control_present` | `verified-runtime` | `1` when any player-control entry exists |

The pointer and record are valid only during the callback. Except for the
human-control invariant, these fields retain the provisional evidence levels
documented in [`mappings/SCRIPTING.md`](../mappings/SCRIPTING.md). A tag is an
identifier copied from the current event, not a durable object handle or proof
that the country will exist later. ABI v1 exposes no mutation and no way to
dereference a tag outside the event.

## Validation

The x86 Release tests compile both public headers as C, dynamically discover and
run an independent C fixture DLL that resolves the event table, exercise
lifecycle failures, event registration, bounded capacity, self-unregister,
callback disablement, exception containment, rollback, and v1-only launcher
preflight. `dumpbin /exports` confirms that the fixture exposes only undecorated
`SmedleyPluginGetApiV1`.

Supplied-game run `a0af29ca-1b35-42f4-abf0-1a29701e3288` loaded that fixture
through the installed launcher, injector, and kernel and reached a responsive
main window. Run `5fca45a1-a770-41e4-8eab-ed472c0ddfc9` loaded the same v1
fixture followed by legacy `campaign_runner` in one responsive process. These
runs verify startup selection and compatibility, not normal-exit callbacks.

Run `479a31ac-6fd4-4ae2-b170-865c63b70d66` used the event-capable C fixture,
legacy `campaign_runner`, the exact supported executable, and unmodified
`benchmark.v2`. The campaign advanced through the active cross-DLL daily
callback from raw date `59883384` to the exact one-day target `59883408`, paused,
and remained responsive. The source save retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.
