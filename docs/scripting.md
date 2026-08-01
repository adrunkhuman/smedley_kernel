# Scripting

The built-in `scripting` plugin lets a mod author subscribe to supported game
events, inspect copied game-state values, schedule callbacks, and queue a
verified native pause without compiling C++. Scripts are ordinary visible Lua
5.1 source files under `GAME_DIR/scripts`; precompiled bytecode is rejected.

Smedley creates a private Lua state for each script. It never uses or modifies
Victoria II's Lua state. The plugin DLL is native trusted code, but a script
does not receive raw pointers, game-owned strings, native module loading, or
the Lua `io`, `os`, `package`, or `debug` libraries. This is a constrained
interpreter, not a security sandbox: inspect scripts before selecting them.

Host instruction errors must not be catchable, moved to an unhooked Lua thread,
or deferred into teardown. Therefore, `pcall`, `xpcall`, `coroutine`, and
`newproxy` are unavailable.

The native Lua pattern and bytecode functions `string.find`, `string.match`,
`string.gmatch`, `string.gsub`, and `string.dump` are also unavailable because
the VM instruction hook does not bound their C work. Basic table and math
operations and bounded string formatting and manipulation remain available.

## Configure

Select `plugins/scripting.toml` and one or more scripts in a launcher profile:

```toml
plugins = ["plugins/scripting.toml"]
scripts = [
  "scripts/examples/country_log.lua",
]
script_instruction_budget = 100000
script_memory_bytes = 8388608
script_queue_capacity = 256
```

Relative paths resolve from `game_dir` and must remain under
`GAME_DIR/scripts`. A script must end in `.lua`, be non-empty, and be no larger
than 1 MiB. At most 16 scripts may be selected. The limits apply as follows:

| Setting | Range | Meaning |
| --- | ---: | --- |
| `script_instruction_budget` | 1,000 to 10,000,000 | Maximum VM instructions in one chunk or callback |
| `script_memory_bytes` | 262,144 to 67,108,864 | Allocator cap for each independent script state |
| `script_queue_capacity` | 16 to 4,096 | Shared copied-event slots between the game and script worker |

The equivalent CLI options are `--script`,
`--script-instruction-budget`, `--script-memory-bytes`, and
`--script-queue-capacity`. `--script` may be repeated. The native GUI preserves
script fields loaded from a profile; edit the profile or use the CLI to add and
remove scripts in this first scripting release.

Preflight rejects traversal, missing files, duplicate paths, wrong extensions,
oversized files, invalid limits, or scripts selected without the `scripting`
plugin. Safe mode ignores scripts and reports a warning instead of injecting
the plugin.

## Callbacks

Every callback is optional. Defining a supported callback with a non-function
value is an explicit error.

```lua
function on_load(context)
    smedley.log("API " .. context.api_version)
end

function on_daily(event)
    if event.country.tag == "ENG" then
        smedley.log("treasury raw: " .. event.country.treasury_raw)
    end
end

function on_unload(context)
    -- Called during orderly plugin teardown, not after a process crash.
end
```

`on_daily` receives a copied table. It runs later on the script worker, so it
must not be interpreted as a live object handle:

| Field | Type | Current quality |
| --- | --- | --- |
| `kind` | string (`"daily"`) | verified-current event hook |
| `mapping_id` | string (`"v2game-3.04"`) | exact supported executable |
| `quality` | string (`"provisional"`) | applies to mapped game-state fields below |
| `date_raw` | number | copied raw Clausewitz date |
| `country.tag` | string | copied three-character tag |
| `country.exists` | boolean | true when the mapped owned-province collection is non-empty |
| `country.treasury_raw` | number | copied fixed-point raw treasury value |
| `country.treasury` | number | `treasury_raw / 32768` |
| `world.country_slot_count` | number | mapped country pointer slot count |
| `world.ai_scheduler_entry_count` | number | mapped AI scheduler entry count |
| `world.human_control_present` | boolean | any nonzero mapped player-control entry |

Daily callbacks originate once per country update, not once per game day. Use
the country tag or remember `date_raw` when a script needs lower volume.

## Host API

`smedley.log(message)` writes at most 512 bytes to `smedley.log` with the script
filename. It performs file I/O only on the script worker.

`smedley.after_days(days, callback)` schedules a Lua callback against the
current event's raw date using the runtime-verified 24 raw units per game day.
It is valid only from an event or scheduled callback, accepts 1 through
1,000,000 days, and permits at most 1,024 outstanding callbacks per script.
The callback receives the first later daily snapshot at or beyond its target.
This scheduler is in-memory and is not persisted in a Victoria II save.

`smedley.request_pause()` queues the currently supported native transaction and
returns `true` only when the request entered the one-slot transaction queue. On
the next daily game callback, the plugin verifies `CInGameIdler`, the mapped
pause state, and the native function signature; it invokes `TogglePause` only
when needed and requires paused readback. Completion or rejection is written to
`smedley.log`. A `true` return means queued, not completed. There is no scripted
unpause operation because a paused campaign produces no daily callback on which
to execute it safely.

The one-slot queue remains occupied through native readback and worker-side
result logging. Additional calls return `false` until that result is consumed,
so one request cannot overwrite another request's completion or rejection.

No mutation methods, console access, save loading, raw pointers, arbitrary
native calls, or Victoria II Lua globals are exposed. Calling an absent or
invented API fails normally in Lua rather than falling through to game memory.

## Failure And Cost

The game callback copies a fixed-size snapshot and uses a nonblocking queue
attempt. Full or contended queues drop the snapshot; accepted, processed,
dropped, error, disabled-script, and high-water counters are logged on orderly
unload together with a terminal worker-failure flag. Lua parsing, allocation,
garbage collection, user logging, and callback execution remain on the worker.

Every chunk and callback runs under `lua_pcall` and an instruction-count hook.
Allocator exhaustion or runtime errors count against that script. Three
callback errors disable only the failing script. Initialization errors reject
the plugin load instead of leaving a partially initialized script set.
Instruction hooks cannot interrupt an indefinitely blocking native C function;
the API therefore removes native pattern matching, module loading, file access,
and other unbounded user-callable functions. A process crash
cannot run `on_unload` or emit final counters.

See [`examples/scripts/country_log.lua`](../examples/scripts/country_log.lua)
and [`examples/scripts/pause_after_30_days.lua`](../examples/scripts/pause_after_30_days.lua).
