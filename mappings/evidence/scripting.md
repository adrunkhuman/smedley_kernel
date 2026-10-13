# Scripting boundary

The built-in `scripting` plugin executes user-selected Lua 5.1 source in private
states compiled into `scripting.dll`. It does not resolve either Victoria II Lua
DLL or export its Lua symbols. The x86 Release DLL exports only
`SmedleyPluginGetApiV1` and dynamically resolves the event, campaign-control,
and logging C services. Private Lua 5.1 states are the only supported scripting
runtime; engine access uses the versioned C services documented in
[plugins](../../docs/plugins.md).

## Data path

The plugin registers through the versioned C daily-event service. The
kernel-owned game-state implementation consumes the hook's country pointer,
builds a checked fixed-size snapshot, and dispatches a copied
`SmedleyDailyEventV1`; malformed or unavailable input suppresses dispatch. The
plugin copies that record and attempts a bounded queue lock without waiting. It performs no Lua
execution, script parsing, garbage collection, user logging, result logging, or
filesystem access. One worker owns every private `lua_State` and converts copied
snapshots into Lua tables.

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

1. The checked kernel-owned game-state adapter requires the kernel's supported
   executable identity and reads the current game-state and idler pointers only
   through readable spans.
2. Require RTTI `.?AVCInGameIdler@@` at `CGameState+0xb24`.
3. Require the verified prologue at RVA `0x26a2c0`:
   `55 8b ec 64 a1 00 00 00 00`.
4. Read `CInGameIdler+0x1538` through a readable span and accept only pause
   state `0` or `1`.
5. Invoke `TogglePause` only from state `0`.
6. Require state `1` on immediate readback through a readable span.

The script's boolean result means that the atomic request was queued, not that
the later transaction completed. Completion or explicit rejection is logged.
The slot is not reopened until the worker consumes that result, preventing a
later request from overwriting an earlier transaction outcome.
Only pause is exposed: a paused simulation supplies no next daily callback for
a symmetric queued unpause.

The scripting plugin owns neither these addresses nor a game pointer. It
acquires `SmedleyCampaignControlApiV1` and calls `set_paused(1)`, then maps the C
success, outside-campaign, invalid-state, signature-mismatch, and
readback-failed results to the existing `PauseResult` values and worker log
messages. The copied C daily event remains observation-only; campaign control is
a separate mutation capability.

## Runtime acceptance

The private-Lua coexistence fixture used the exact supported executable, no
mods, source save `benchmark.v2`, native speed 5, `campaign_runner`, and
`scripting` with `scripts/examples/pause_after_30_days.lua`. The source save
remained unchanged.

The plugin initialized while Victoria II's `lua51.dll` and `lua5.1.dll` were
both loaded. Campaign automation reached `CInGameIdler` and unpaused. The
script scheduler queued its callback at raw date `59884128`; the plugin then
invoked the transaction and logged paused readback. The process remained
responsive after the pause. It was terminated as test cleanup; no verified
native clean-exit path was available to that test.

This verifies private-runtime coexistence, daily copied-event delivery,
in-memory day scheduling, and pause transaction completion on the supported
runtime. It does not establish safe arbitrary mutation, save-persistent script
state, crash-time teardown, deterministic multiplayer behavior, or support for
another executable or mod.

The checked-reader fixture repeated the same save, plugins, script, and speed
with checked snapshot and pause boundaries. It predates the C service-table
migration and therefore validates the underlying engine adapters, not that
binary interface. At raw date `59884128` the worker logged the queued request,
the next game-thread transaction logged paused readback, and the process
remained responsive. Test cleanup terminated the paused process; the source
save remained unchanged.

Neither fixture records exact-built-commit provenance; these results do not
establish acceptance of the current C service-table implementation.
