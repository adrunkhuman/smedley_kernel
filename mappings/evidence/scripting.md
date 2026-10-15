# Scripting boundary

Scripting uses private Lua 5.1 states compiled into `scripting.dll`, not either
Victoria II Lua DLL or the game's Lua state. The x86 Release DLL exports only
`SmedleyPluginGetApiV1`; no Lua symbols or C++ ABI cross the plugin boundary.
Engine access uses versioned C services. Current script operations and limits
are in [scripting](../../docs/scripting.md), with ABI requirements in
[plugins](../../docs/plugins.md).

## Copied state

The kernel consumes the native daily hook's country pointer and builds a
checked fixed-size `SmedleyDailyEventV1`. Static callsite inspection places
hook RVA `0x001085ae` in the earlier country pass (entry RVA `0x00108590`),
not the later country update at RVA `0x001091a0`. The callback is therefore
not an end-of-country-update or whole-day completion signal; see the
[phase evidence](../research/engine-scheduling.md#existing-dailyupdateevent-observes-the-earlier-pass).
Malformed or unavailable input suppresses dispatch rather than producing
invented values. Only copied records
cross to the script worker; no native pointer, game-owned string, mutable
container, console manager, or Victoria II Lua value does.

Game callbacks perform a bounded, nonblocking queue attempt. Lua execution,
parsing, allocation, garbage collection, logging, and filesystem access stay on
the worker, which owns the private Lua states. A later Lua table is a snapshot,
not live object access or synchronous game-thread authority.

| Script field | Native source | Evidence | Limit |
| --- | --- | --- | --- |
| `date_raw` | `CGameState+0xb0c` | `provisional` | Raw Clausewitz date; progression is correlated, not the entire historical layout |
| `country.tag` | `CCountry+0x1c` | `provisional` | Copied three-byte tag |
| `country.exists` | non-empty `CCountry+0x9d8` owned-province vector | `provisional` | At least one mapped owned-province entry, not every possible country lifecycle state |
| `country.treasury_raw` | `CCountry+0xe78` | `provisional` | Signed 48.15 fixed-point raw value; monetary value is `treasury_raw / 32768` |
| `world.country_slot_count` | `CGameState+0xadc` vector size | `provisional` | Country slots, not necessarily living countries |
| `world.ai_scheduler_entry_count` | `CGameState+0xa4` vector size | `provisional` | Scheduler entries, not an AI decision trace |
| `world.human_control_present` | any nonzero `CGameState+0xaec` entry | `verified-runtime` | Correlated with the zero-human-control observer invariant |

## Queued pause

A script can request pause through a one-slot atomic transaction. A later daily
callback on the game update path performs the checked native operation:

1. Require the supported executable identity and readable current game-state
   and idler pointers.
2. Require RTTI `.?AVCInGameIdler@@` at `CGameState+0xb24`.
3. Require the prologue at RVA `0x26a2c0`:
   `55 8b ec 64 a1 00 00 00 00`.
4. Read `CInGameIdler+0x1538` through a readable span; accept only `0` or `1`.
5. Invoke `TogglePause` only from state `0`.
6. Require state `1` on immediate readback through a readable span.

A successful script return means queued, not completed. The slot remains
occupied until the worker consumes the completion or rejection, so another
request cannot overwrite the outcome. Only pause is exposed: a paused
simulation supplies no next daily callback for symmetric queued unpause.

The copied daily event is observation-only; campaign control is a separate
mutation capability. The plugin owns neither engine addresses nor a game
pointer. It requests `set_paused(1)` through `SmedleyCampaignControlApiV1`;
queue admission does not bypass that service's checks.

## Runtime scope and limits

Private Lua coexisted with both game DLLs, `lua51.dll` and `lua5.1.dll`, loaded
on the supported executable. Copied daily-event delivery, in-memory day
scheduling, and queued pause with paused readback were observed in an unmodded
campaign at native speed 5. The paused process remained responsive.

These observations concern the historical private runtime and checked engine
adapters. They predate the C service-table migration and lack exact-built-commit
provenance, so they do not establish runtime acceptance of the current binary
interface. Native clean exit from the scripted paused state and crash-time
teardown are not established.

The boundary does not establish arbitrary mutation safety, save-persistent
script state, deterministic multiplayer behavior, or support for another
executable or mod. The mapped state fields retain their individual evidence
levels; successful pause does not promote the broader layouts.
