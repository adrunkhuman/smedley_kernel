# Scripting boundary

The built-in `scripting` plugin executes user-selected Lua 5.1 source in private
states compiled into `scripting.dll`. It does not call
`clausewitz::GetLuaState`, resolve either Victoria II Lua DLL, or export its Lua
symbols. The final x86 Release DLL exports only `CreatePlugin`.

## Data path

`DailyUpdateEvent` runs synchronously in the game update path. The scripting
handler copies only fixed-size values and attempts a bounded queue lock without
waiting. It performs no Lua execution, script parsing, garbage collection, user
logging, result logging, or filesystem access. One worker owns every private `lua_State` and
converts copied snapshots into Lua tables.

| Script field | Native source | Evidence | Limit |
| --- | --- | --- | --- |
| `date_raw` | `CGameState+0xb0c` through `current_date_raw()` | `provisional` | Raw Clausewitz date; runtime progression is correlated but the historical layout remains broader than this API |
| `country.tag` | `CCountry+0x1c` | `provisional` | Copied three-byte tag |
| `country.exists` | non-empty `CCountry+0x9d8` owned-province vector | `provisional` | Means at least one mapped owned-province entry, not every possible country lifecycle state |
| `country.treasury_raw` | `CCountry+0xe78` | `provisional` | Signed 48.15 fixed-point raw value |
| `world.country_slot_count` | `CGameState+0xadc` vector size | `provisional` | Includes country slots, not necessarily living countries |
| `world.ai_scheduler_entry_count` | `CGameState+0xa4` vector size | `provisional` | Scheduler entries, not an AI decision trace |
| `world.human_control_present` | any nonzero `CGameState+0xaec` entry | `verified-runtime` | Same ownership invariant used by observer acceptance |

No native pointer, game-owned string, mutable container, console manager, or
Victoria II Lua value crosses to the worker.

## Pause transaction

`smedley.request_pause()` sets a one-slot atomic request from the worker. A
later daily callback on the game update path performs the transaction:

1. Require RTTI `.?AVCInGameIdler@@` at `CGameState+0xb24`.
2. Require the verified prologue at RVA `0x26a2c0`:
   `55 8b ec 64 a1 00 00 00 00`.
3. Read `CInGameIdler+0x1538` and accept only pause state `0` or `1`.
4. Invoke `TogglePause` only from state `0`.
5. Require state `1` on immediate readback.

The script's boolean result means that the atomic request was queued, not that
the later transaction completed. Completion or explicit rejection is logged.
The slot is not reopened until the worker consumes that result, preventing a
later request from overwriting an earlier transaction outcome.
Only pause is exposed: a paused simulation supplies no next daily callback for
a symmetric queued unpause.

## Runtime acceptance

Run `b076f162-9700-40c3-9c9b-7f56c53991b3` used the exact supported executable,
no mods, source save `benchmark.v2`, native speed 5, `campaign_runner`, and
`scripting` with `scripts/examples/pause_after_30_days.lua`. The source save
started with and retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

The plugin initialized while Victoria II's `lua51.dll` and `lua5.1.dll` were
both loaded. Campaign automation reached `CInGameIdler` and unpaused at
16:49:11. The script scheduler queued its callback at raw date `59884128`; at
16:49:12 the plugin invoked the transaction and logged paused readback. The
process remained responsive after the pause. It was then terminated as test
cleanup because a verified native clean-exit path does not exist.

This verifies private-runtime coexistence, daily copied-event delivery,
in-memory day scheduling, and pause transaction completion on the supported
runtime. It does not establish safe arbitrary mutation, save-persistent script
state, crash-time teardown, deterministic multiplayer behavior, or support for
another executable or mod.
