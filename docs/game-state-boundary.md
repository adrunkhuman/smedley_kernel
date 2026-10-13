# Game-state boundary

The kernel owns all Victoria II engine access. Plugins receive copied records
and opaque handles through versioned C services; they own feature policy, not
engine layouts. Native plugins remain trusted DLLs, not sandboxed code.

## Ownership

| Layer | Owns | Must not expose or duplicate |
| --- | --- | --- |
| `smedley_kernel/` | Injection lifecycle, executable identity, event dispatch, plugin loading, hooks. | C++ ABI across DLLs. |
| `game_state/` | Kernel-private layouts, bounded readers, native calls, checked mutations, session/controller state. Compiled only into `smedley_kernel.dll`. | A separate plugin-side runtime or raw engine objects. |
| `plugins/` | Allocation and observer policy, capture selection, aggregation, timers, retries, scripts, logging, publication. | Engine offsets, native stubs, raw pointers, or copies of kernel state. |
| `include/smedley/` | Public versioned C service headers. | STL types, exceptions, or compiler-specific ownership. |
| `mappings/` | Signatures, provenance, evidence levels, and known limits. | Unsupported hypotheses presented as safe operations. |

Tests enforce complementary boundaries:

| CTest | Check |
| --- | --- |
| `smedley_game_layering_audit` | Scan first-party plugin production C/C++ for raw headers, engine addresses, native stubs, `memory::Map`, and game-object pointers. |
| `smedley_engine_ownership_audit` | Reject duplicated `game_state/src` ownership, recreated shared runtimes, and plugin imports of kernel-private headers. |
| `smedley_dll_boundary_audit` | Check exact exports and reject plugin imports from `smedley_kernel.dll`. |

These checks enforce source/build policy; they do not prove that arbitrary
native code is safe. There is no CMake raw-adapter registration escape hatch.
Retired `smedley/v2`, `smedley/clausewitz`, and `smedley/std` mirrors are not
compatibility surfaces. See [build checks](../BUILDING.md) and
[mirror provenance](../mappings/research/retired-engine-mirrors.md).

## References and snapshots

Inside the kernel, `Reference<Category>` values such as `CountryRef` and
`PopRef` label non-owning addresses. Construction, null checks, and address
comparison do not establish identity, lifetime, or access permission. Resolve
references in the callback that needs them and use them only synchronously.
Pass copied records across callbacks, queues, workers, or unload boundaries.

`CurrentGameSession` pairs the observed game-state address with a monotonic
process-local epoch. The epoch advances when the runtime observes that address
change. It cannot detect an unobserved replacement at the same address: no
verified game-state constructor/destructor hook exists. Callback authority also
checks its dispatch generation and creating thread; an unchanged epoch alone
never grants access.

Readers return caller-owned, bounded snapshots. They validate readable spans
and foreign string/container metadata before traversal. Availability is scoped
to each capture group: one malformed group must not erase other readable groups
or become numeric zero. A snapshot does not extend the source object's lifetime.

## Public services

[Plugin development](plugin-development.md) covers discovery, lifecycle, daily
events, logging, and examples. The engine tables are:

| Header | Discovery symbol | Capability |
| --- | --- | --- |
| [campaign_control_api.h](../include/smedley/campaign_control_api.h) | `SmedleyGetCampaignControlApiV1` | Copied campaign state and checked pause, speed, quit. |
| [campaign_runtime_api.h](../include/smedley/campaign_runtime_api.h) | `SmedleyGetCampaignRuntimeApiV2` | Campaign/game-over observation, frontend save control, observer operations. |
| [campaign_automation_api.h](../include/smedley/campaign_automation_api.h) | `SmedleyGetCampaignAutomationApiV1` | Frontend, annexation, console-capture notifications; tag switching, popup suppression, process metrics. |
| [interest_pool_api.h](../include/smedley/interest_pool_api.h) | `SmedleyGetInterestPoolApiV1` | State/POP pool snapshots, preparation, payout, cleanup. |
| [province_modifier_reconciliation_api.h](../include/smedley/province_modifier_reconciliation_api.h) | `SmedleyGetProvinceModifierReconciliationApiV1` | Bounded modifier/policy snapshots and checked removal. |
| [telemetry_game_api.h](../include/smedley/telemetry_game_api.h) | `SmedleyGetTelemetryGameApiV1` | Basic copied snapshots, bounded hook subscriptions and drains. |
| [telemetry_observation_api.h](../include/smedley/telemetry_observation_api.h) | `SmedleyGetTelemetryObservationApiV1` | Detailed caller-buffered world, market, country, POP, factory, province, and RGO reads. |

Discovery and caller-initialized single-record outputs require exact
`struct_size`, `version`, and zero reserved fields. Bulk arrays are bounded and
caller-owned; the kernel initializes returned metadata. Campaign runtime v2 is
the supported table; the removed v1 discovery symbol has no compatibility alias.

Handles are process-local and opaque. Do not serialize, guess, or retain them
beyond their session/callback. Stale handles fail before engine traversal.
Interest and reconciliation authority expires when its synchronous callback
returns. A bank callback is skipped if no bounded authority slot is available;
its registration remains enabled.

The first successful lifecycle session open establishes the service metadata's
owner thread. Telemetry also accepts the currently executing synchronous daily
callback thread. Other threads get `wrong_thread` before mutable metadata or
scratch is accessed. Lifecycle code closes telemetry after callback dispatch
drains; closing a campaign session releases its frontend capabilities. A stale
close retires bookkeeping without touching the engine.

Counted text arguments must be nonempty ASCII-compatible spans without embedded
NUL; the kernel copies them into local terminated storage. Interest state/POP
handles select immutable kernel-captured candidates. Prepare/apply ignore
caller copies of those candidates and reject duplicate POPs or membership
outside the selected state's captured range.

Telemetry subscriptions belong to a session, epoch, and thread. Entity IDs are
opaque, not encoded engine addresses. Drains consume available records; records
beyond output capacity, including all records in a zero-capacity drain, count
as dropped along with source overflow. Opening a new session retires expired
subscriptions without traversing stale objects or changing patches. Live close
may use the checked uninstall path. See the [telemetry native API](telemetry/native-api.md)
for observation identities, publication, and shutdown contracts.

## Checked mutations

All engine operations require the retained exact-executable verdict, applicable
signatures, correct phase/thread, valid object metadata, and operation-specific
preconditions. Mutation rechecks its boundary and verifies postconditions.
Defensive checks are not new evidence for an object's identity or semantics.

| Operation | Kernel contract | Consumer policy |
| --- | --- | --- |
| Interest payout | Callback-scoped authority, complete batch preflight, overflow/writable-span checks, native `CPop::GiveMoney`, immediate per-POP postconditions, pool conservation and clear-after-success. | Eligibility, weights, deterministic rounding, reporting, failure recovery. |
| Pause | Resolve current game/idler, validate RTTI and state `0/1`, check `TogglePause`, call only when needed, verify readback. | Lua queues requests; campaign runner chooses when to pause. |
| Speed | Verify native handler bodies and each one-index readback. | Desired speed and retries. |
| Quit | Verify idler vtable target and request-flag postcondition. | Drain telemetry and decide whether exit is allowed. |
| Observer | Validate player-control and AI/scheduler state; restore AI or change only the camera tag with readback. | View selection, watchdog, retries, switch transaction. |

Interest writes across POPs are **not atomic**. Complete preflight does not
reserve engine state; a later payment can fail after earlier writes succeed.
There is no verified rollback. Report partial mutation and do not retry blindly.
The older `DailyInterestAccess` batch and current `BankInterestAccess` pool
boundary are kernel-private; the plugin uses C authority handles. Current
payout and cleanup behavior is in the [interest guide](interest-fix.md), with
[boundary evidence](../mappings/evidence/interest-payout.md).

Campaign snapshots require readable, range-checked date/speed/pause fields.
Game-over observation additionally checks the native endgame branch, configured
end date, dialog vtable, and visibility. Unavailable state is not a zero date or
ordinary pause. Optional Windows process metrics have explicit availability.

## Frontend and observer lifecycle

Frontend constructor/destructor hooks capture the controller thread and
invalidate its generation before native storage is released. The runner gets
an opaque token and copied save basename/status. Each action rechecks token,
thread, vtable, GUI lookup target, and signal signatures before native work.
The kernel owns empty-string validation, prepared engine-string lifetime
transfer, save-flag writes, and readback; the runner owns filename policy and
state-machine decisions.

Observer snapshots copy tags, ordinals, control flags, scheduler counts, and
FOW. Malformed metadata or a missing in-game idler makes them unavailable.
`ReturnObserverCountryToAI` requires the expected human/no-AI state and verifies
scheduled AI after transition. `SetObserverViewCountry` requires a healthy AI
target and verifies unchanged human-control and scheduler counts.

The kernel captures the console manager, saves the asynchronous native `tag`
handler, installs/removes observer-safe `switch`, and invokes `debug fow` with
readback. Retained console state is bound to the captured session epoch. On
expiry it is discarded without dereferencing stale storage; old handlers become
inert. A command allocation is freed only after verified removal from a live
manager; otherwise it is deliberately retained to avoid a dangling engine
reference.

Campaign automation has one bounded registration. Frontend, annexation, and
console-capture callbacks run on hook paths and receive fixed-size copied
records. They must not allocate, block, perform I/O, retain record memory, call
engine services, unregister, or deactivate. Returning `disable` makes a callback
inert; self-deactivation returns `busy`. The runner copies notifications into
bounded storage; its owner timer performs policy and service calls.

Owner-thread deactivation disables callback sinks before waiting for acquired
callbacks, unregisters console capture, restores/removes commands through the
checked runtime, and invalidates the handle. Popup suppression and counters
belong to that handle and reset on observer-mode changes or deactivation.
Console commands use bounded event-service arguments/responses; automation owns
capture and checked native switching.

## Detour ownership

Pinned MinHook v1.3.4 decodes/relocates complete x86 instructions, coordinates
threads during enable/disable, and provides original trampolines. Smedley checks
executable identity and target bytes first. Once published, detours/trampolines
remain process-lifetime: another thread may already be executing them. Later
installation failure deactivates callbacks, poisons the group, and reports
`readback_failed`; normal deactivation also clears callbacks rather than freeing
reachable executable memory.

Call-site and branch replacements are not function-entry detours. Daily-event,
interest, heap/telemetry capture, and popup patches retain explicit expected-byte,
page-protection, instruction-cache, rollback, and continuation contracts.
Each has its own quiescence requirements; raw campaign patch installation does
not itself suspend other threads.

## Evidence and validation

[Mapping evidence levels](../mappings/README.md) distinguish runtime observations,
static callsites, and hypotheses. Byte matches and host tests do not establish
lifetime, thread affinity, safe mutation, or live-game acceptance. Changes need
evidence for the exact claim and supported executable—not just a successful
launch. Historical runs validate only their recorded implementation and scope.

| Boundary | Evidence |
| --- | --- |
| Save loading, observer, speed, pause, quit | [Campaign automation](../mappings/evidence/campaign-automation.md) |
| State pools and POP payment | [Interest payout](../mappings/evidence/interest-payout.md) |
| Observation fields and hooks | [Telemetry](../mappings/evidence/telemetry.md) |
| Province modifier removal | [Reconciliation](../mappings/evidence/province-reconciliation.md) |
| Copied Lua events and queued pause | [Scripting](../mappings/evidence/scripting.md) |
