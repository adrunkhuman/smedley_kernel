# Game-state boundary

Bundled plugins must separate gameplay policy from reverse-engineered engine
access. The boundary is internal C++, not a new public plugin ABI. It prevents a
plugin from silently accumulating its own offsets, calling conventions, and
foreign-object assumptions.

## Layers

| Layer | Owns | Must not own |
| --- | --- | --- |
| `smedley_kernel.dll` and mappings | Hook installation, raw engine headers, executable identity, signatures, session/controller state, bounded reads, copied snapshots, and checked mutations | Plugin gameplay policy or allocation choices |
| Kernel-private game state implementation | Typed non-owning references and engine adapters compiled only into `smedley_kernel.dll` | A separately linked plugin runtime or a stable C++ plugin ABI |
| Plugins | Feature selection, deterministic allocation, result publication, lifecycle, and failure policy | Raw offsets, engine ABI mirrors, native calls, or retained game pointers |

The static audit `tools/check_game_layering.py`, registered as the
`smedley_game_layering_audit` CTest, scans common C/C++ source and header
extensions in every first-party plugin production tree and excludes tests.
Existing raw integrations must be
listed as sources of their CMake target and registered with
`smedley_allow_raw_plugin_sources`; configuration rejects a registration that
the target does not own. Every other production source is checked for raw
headers, `memory::Map`, the verified `GiveMoney` RVA and wrapper names, and
named engine RVAs/field offsets, native call stubs, and game-object `void*`
declarations. Adding or expanding a raw adapter is an explicit reviewable CMake
change rather than an accidental include. This is a source-policy boundary, not
proof that arbitrary code contains no engine assumption; review and target
ownership remain required.

## Evidence promotion

Mappings record evidence, not API promises. `historical-unverified` and
`provisional` material is a research lead. `verified-static-callsites` supports
a stated static claim, but does not establish object lifetime, thread affinity,
or safe mutation. Promote a mapping only with the recorded executable identity,
signature/callsite review, and a narrowly scoped supported-game observation that
checks the claimed behavior. A successful byte match or process launch is not
runtime validation.

Defensive checks in the runtime are not new reverse-engineering evidence. They
check the kernel's retained executable-identity verdict, configured base,
expected or registered code bytes, callback phase and thread, memory protection,
snapshots, and postconditions. The kernel hashes the current process executable
before installing any hook; mutation remains unavailable unless that exact
identity passed. These checks do not independently prove object identity.

Supported-game run `c94800f7-74a8-4013-bfcc-c12e96776d52` exercised the final
checked runtime, retained executable-identity gate, and telemetry
`CPop::GiveMoney` hook for seven exact days. It produced 3,684 trace records with
zero gaps, drops, or writer failures and 24 successful recipient rows. Named
bank transfer `92,860` produced exact POP payout `92,860,000`; all 4,911 paid POP
postconditions passed. The campaign reached raw target `59883552`, exited through
the native bounded-run path, and the source save retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.
This validates the exercised path only, not arbitrary object identity or other
plugin migrations.

## References and snapshots

`Reference<Category>` values such as `CountryRef`, `PopRef`, and `GameStateRef`
carry a typed, non-owning address. They prevent accidental category mixing and
allow null checks and address comparison. They do not validate the address,
prove object identity, extend lifetime, grant read/write permission, or make a
foreign pointer safe to retain. Their explicit constructor is an unchecked
internal labeling operation. Resolve again in the callback that needs the
object, use the reference only synchronously in that dispatch, and pass copied
snapshots across callbacks, unload, queues, or worker threads.

`CurrentGameSession` pairs the observed current-game-state address with a
monotonic process-local epoch. The checked runtime advances the epoch when it
observes that address change. `DailyInterestAccess` captures both values and
rejects mutation if either changes, while its separate dispatch generation
still proves that the exact callback is active on the creating thread.

The epoch is stale-reference defense, not a native lifecycle claim. No verified
game-state constructor or destructor hook exists, and an unobserved replacement
that reuses the same address cannot be distinguished. This is why references
remain synchronous and non-owning even when their captured epoch still matches.

Readers return bounded snapshots instead of exposing containers or ABI mirrors.
The caller owns the snapshot, can inspect its flags, and must treat a failed or
partial read as unavailable rather than zero. Snapshotting is not a claim that
the game object remains valid after the reader returns.

Telemetry current-state, country, and province snapshots copy each requested
field through guarded spans and validate foreign vector/list metadata before
using it. Their availability flags are capture-group scoped: an unavailable
relation or province-production container suppresses that group only, not an
otherwise readable country or province snapshot. Telemetry records omit the
unavailable selected group and account it as invalid; they never substitute a
zero value.

## Interest mutation

`DailyInterestAccess` is callback-scoped capability derived from the active
`DailyInterestEvent`. It is non-copyable and checks the current dispatch,
country, thread, and `AFTER` phase. Do not cache it, recreate it, or use it from
another callback.

The interest plugin performs a complete preflight before its first write:

1. It collects typed POP candidates and computes a deterministic allocation in
   plugin-owned storage.
2. It checks allocation conservation and submits every nonzero payout to
   `ApplyPopInterestBatch` in deterministic order.
3. The checked runtime validates callback access and the native signature,
   snapshots every POP, checks every addition and writable range, and performs
   no write unless the complete batch preflight succeeds.
4. Immediately before the first write it rechecks callback access and the
   native signature. Each native call then requires immediate expected
   POP-money postconditions against its preflight snapshot.

Callback, session, signature, and memory-page checks are amortized across this
synchronous game-thread operation. The batch does not re-resolve POP identity
or ownership. The checked API exposes no raw mutation primitive; native plugins
remain trusted DLLs with arbitrary process access and are not sandboxed by this
source boundary.

Preflight does not reserve engine state and application is not transactional
across several POPs. A later POP can fail after earlier writes succeeded. There
is no verified rollback operation: the plugin reports `partial_mutation`,
disables further payouts, and must not retry blindly.

The runtime owns the verified `CPop::GiveMoney` adapter because its RVA, calling
convention, signature, and writable field span are engine evidence. Allocation
remains plugin policy: it defines eligibility, weighting, ordering, rounding,
telemetry, and whether a failure disables the feature. A generic game-state
service must not silently choose those gameplay semantics.

## Pause mutation

The campaign-control `set_paused(1)` operation owns the scripting pause engine
boundary. It first requires the
kernel's executable-identity verdict, then reads the current game-state and
idler pointers through guarded spans, validates the `CInGameIdler` RTTI name,
checks pause state `0` or `1`, checks the native pause prologue, invokes
`TogglePause` only from state `0`, and requires immediate state-`1` readback.
It retains no pointer. The scripting plugin owns only its one-slot request
state, maps the checked statuses to its existing Lua-worker result messages,
and unregisters its daily event handler before shutdown reporting.

The C daily-event callback is the game-thread scheduling boundary. The kernel
builds `SmedleyDailyEventV1` through checked readers and suppresses dispatch when
required fields or bounded container metadata are unavailable. The plugin never
receives a country or game-state reference while queueing Lua work. The event
record remains observation-only; pause uses the separate campaign-control table.

## Campaign runtime

The kernel-private game runtime owns the checked campaign runtime boundary. It returns a
copied `CampaignRuntimeSnapshot` only after executable identity, current
game-state/idler resolution, idler RTTI, readable date/speed/pause fields, and
their supported ranges pass. The snapshot is synchronous and non-owning; an
unavailable observation is not a zero date, speed, or pause value.

`SetCampaignPaused`, `SetCampaignSpeedIndex`, and `RequestCampaignQuit` return
explicit fail-closed statuses. Pause is the same generalized native transaction
used by the campaign-control API, so scripting and campaign automation share RTTI, signature,
and readback checks. Speed verifies both native handler bodies and every
one-index readback. Quit verifies the idler vtable target and its request-flag
postcondition. `SampleProcessMetrics` returns optional copied Windows process
CPU and memory values; unavailable counters remain absent.

`campaign_runner` retains benchmark decisions, timers, retries, logging,
telemetry, and drain/quit policy. It owns no idler pointer, campaign date,
pause/speed field, speed handler, quit vtable operation, or process-counter
access for those responsibilities.

## Frontend runtime

The kernel-owned game-state implementation owns the checked frontend/main-menu boundary. It
transactionally installs the supported constructor and scalar-deleting-destructor
hooks, preserves their displaced prologues, and invalidates a capture before
native storage can be released. A bundled runner receives only a generation-bound
`FrontendControllerToken`, never a controller address. Every action rechecks the
token, captured thread, exact controller vtable, bounded GUI lookup target, and
native signal signatures before calling the engine.
Each entry signature is checked before MinHook installation. The detour calls
MinHook's relocated original trampoline. Published frontend detours remain
process-lifetime and rollback deactivates their callbacks; freeing a published
trampoline could race an in-flight detour.

`FrontendSaveSnapshot` copies the selected basename and request/completion flags.
The runner keeps filename policy and state-machine decisions; the runtime owns
canonical empty-string validation, prepared engine-string lifetime transfer,
save-flag mutation, and readback. Stale, cross-thread, malformed, or unsupported
capabilities fail closed without signal dispatch or writes. The existing mapping
claims remain `verified-runtime`; host tests cover only invalid metadata and API
failure paths.

## Observer runtime

The kernel-owned game-state implementation owns the checked observer engine boundary.
`ObserverStateSnapshot` and `ObserverCountrySnapshot` copy normalized tags,
ordinals, existence, human-control, AI/scheduler, country-count, scheduler-count,
and FOW state. They retain no country, AI, game-state, or container pointer.
Malformed vectors, tags, control entries, scheduler entries, FOW bytes, or a
missing in-game idler make the observation unavailable; plugins must not treat a
failed observation as an empty country or disabled FOW.

`ReturnObserverCountryToAI` re-resolves the copied country synchronously,
requires the expected human-controlled/no-AI precondition, validates the native
transition signature, and requires a restored scheduled AI on readback.
`SetObserverViewCountry` requires a healthy AI target, writes only the camera
tag, and requires unchanged human-control and scheduler counts on readback.
These operations do not choose targets, retries, or watchdog actions.

The runtime registers its kernel-local console-initialization event handler,
extracts the raw manager there, and reports only a copied capture status.
It validates and saves the native asynchronous `tag` handler, installs/removes
the observer-safe `switch` command, copies command arguments and results, and
validates/invokes `debug fow` with FOW readback. Retained manager use is bound to
the captured `CurrentGameSession` epoch; a session change discards the manager
without dereferencing stale storage and makes old handlers inert. A command
record is reclaimed only after verified removal from a live manager; otherwise
the runtime intentionally retains the small allocation rather than risk freeing
engine-referenced storage. Runner callbacks receive no console object.

## Binary ownership and public boundaries

The `game_state/src` implementation is compiled directly into
`smedley_kernel.dll`. No plugin or separate static runtime target receives a
second copy of its hook queues, session epoch, controller captures, callback
state, or mutation capabilities. `smedley_engine_ownership_audit` enforces this
build boundary.

Plugins acquire engine capabilities through versioned C service tables. The
daily event API provides copied daily snapshots. The campaign-control v1 API
provides a copied campaign snapshot and checked pause, speed, and quit
operations. The scripting plugin uses only these C engine-service boundaries.

## Domain C APIs

The kernel exports three additional independently discoverable v1 tables. They
are domain boundaries, not a general game-object API. Each discovery record and
every caller-supplied output record requires its exact `struct_size`, `version`,
and zero reserved fields. All records use fixed-width fields; arrays are caller
owned and bounded by the documented capacity. A failed or partial read supplies
no invented zero data.

| Header | Discovery symbol | Scope |
| --- | --- | --- |
| `smedley/campaign_runtime_api.h` | `SmedleyGetCampaignRuntimeApiV1` | Campaign copied state plus checked pause, speed, quit, frontend-save, and observer operations. `SmedleyCampaignSession` and `SmedleyFrontendController` are opaque generation-bound handles. |
| `smedley/campaign_automation_api.h` | `SmedleyGetCampaignAutomationApiV1` | Campaign automation lifecycle, copied frontend/annexation/console-capture notifications, console/tag-switch control, popup suppression readback, and optional copied process metrics. `SmedleyCampaignAutomation` is opaque and session/epoch bound. |
| `smedley/interest_pool_api.h` | `SmedleyGetInterestPoolApiV1` | State/POP pool snapshots, prepare, payout, and cleanup. Each operation takes the `SmedleyBankInterestAuthority` supplied only to its synchronous bank-interest callback. |
| `smedley/telemetry_game_api.h` | `SmedleyGetTelemetryGameApiV1` | World, market, country, province, POP, and factory copied snapshots plus bounded hook subscriptions and drain records. `SmedleyTelemetrySession` and `SmedleyTelemetryHookSubscription` are opaque handles. |

Authority and session handles are process-local, opaque values. They must not be
serialized, guessed, or retained beyond their callback/session. The interest
authority is invalid immediately when its callback returns. Stale authority,
session, controller, and subscription values fail closed without engine
traversal. Hook records have at most 64 signed raw values; `drain_hooks` reports
loss explicitly and never returns engine addresses. Capability installation and
uninstallation stay inside the checked runtime.

Session reads and hook operations are thread-affine. Telemetry sessions may be
opened and read from a synchronous daily-event callback; process-owner lifecycle
code closes them after callback dispatch has drained. Closing a campaign session
releases its frontend capabilities.
Text arguments are counted byte spans: they must be nonempty ASCII-compatible
text with no embedded NUL, and the kernel copies exactly that span into local
NUL-terminated storage before calling the checked runtime. Interest state and
POP handles select immutable kernel-captured candidates only. Prepare and apply
ignore caller copies of candidate data; apply rejects duplicated POPs and POPs
outside the selected state's captured contiguous membership range.

The game-service metadata tables have one process-local lifecycle owner thread.
The first successful lifecycle session open establishes it. Telemetry reads also
accept the currently executing synchronous daily-event callback thread; unrelated
threads receive `wrong_thread` before mutable metadata or reader scratch is
touched. Every controller records
its owning session, epoch, and thread. A stale close only retires ABI bookkeeping
and never calls the engine. Hook subscriptions are similarly bound to a live
telemetry session, epoch, and thread. Hook entity IDs are per-subscription
opaque generation-bound correlation IDs backed by a kernel-only address map,
never engine addresses. A drain consumes all available
kernel records; any records beyond the caller buffer increment `dropped` rather
than being silently discarded. `subscribe_hooks` takes the owning telemetry
session; drain and unsubscribe revalidate that session before reading a queue or
changing hook state.

Opening a later telemetry session retires subscriptions from an expired session
without traversing its stale game objects or changing hook patches. Explicit
close on a live session may use the existing checked uninstall path. The same
fail-closed expiry policy retires campaign frontend ABI handles without invoking
their stale engine tokens. A bounded bank-interest authority binding is required
before a callback runs; if no binding slot is available, that callback is skipped
without disabling its registration. A zero-capacity hook drain still consumes all
selected queues and reports every consumed record and source-overflow counter as
`dropped`.

The campaign automation table has one bounded registration. Installation activates
checked frontend and campaign hooks and kernel-owned console capture; it exports
no controller, console manager, native handler, or game pointer. Frontend,
annexation, and console-capture callbacks receive fixed-size copied records on
hook paths. They must not allocate, block, perform I/O, unregister, deactivate,
retain record memory, or call engine services; returning `disable` makes that
callback inert. Explicit deactivation is owner-thread-only, disables C callback
sinks before waiting for already-acquired callbacks, unregisters C console
capture, restores/removes commands only through the checked runtime, and
invalidates the opaque handle. Self-deactivation from a callback returns `busy`.
The C automation handle owns shared process-lifetime hooks, captures, observer
mode, popup suppression, and console registration. One console-capture event
registration performs console mutation once and dispatches one notification to
the active consumer. Console command handling stays
in bounded `event_services`: automation owns console capture/status and checked
native tag-switch, while the event service supplies copied arguments and a
bounded copied response. Process metrics use availability bits instead of
invented zero values. Popup suppression and its counter are scoped to a live
automation handle and reset by observer-mode changes or deactivation.

The initial telemetry game table is not yet a full replacement for the bundled
telemetry plugin. It exposes the existing basic world/market/country/province,
POP, factory, and hook capture subset, but does not yet expose the detailed
country diplomacy/politics groups, POP identity/needs/artisan snapshots, RGO,
factory inputs, country-economy/creditor snapshots, or the plugin's daily-event
scheduling surface. The campaign automation table covers the remaining
engine-facing runner surface.

Do not duplicate a public C API merely to expose game objects. The copied daily
event C API and the Lua-facing copied APIs remain valid external boundaries:
they provide data and queued/constrained operations without lending object
pointers. New external capabilities should follow that model, with their own
version, ownership, lifetime, and thread contract.

The `game_state` C++ headers are kernel-local implementation contracts. Plugins
consume only versioned C service tables.

## Detour ownership

The kernel uses pinned MinHook v1.3.4 for function-entry detours. MinHook decodes
and relocates complete overwritten x86 instructions, coordinates threads while
enabling or disabling a detour, and supplies the callable original trampoline.
Smedley still verifies the exact supported executable and expected target bytes
before creating each detour. Once enabled, a detour and its original trampoline
remain process-lifetime because another thread may already be executing the
detour before its tail jump. A later installation failure deactivates callbacks,
marks the hook group poisoned, and reports `readback_failed` instead of freeing
reachable executable memory. Normal deactivation also clears callbacks without
removing published detours.

Call-site replacements are not function-entry detours. Daily event hooks,
interest boundaries, heap capture, telemetry capture calls, and message popup
dispatch branches retain their exact checked byte patches because they replace a
specific call or branch and may have several verified continuation targets.
They keep explicit page-protection, instruction-cache, and rollback contracts
rather than pretending to be MinHook function entries. Each hook documents its
own quiescence requirements; installing a raw campaign patch does not itself
suspend other threads.

## Raw-access inventory

This inventory starts from `498d674` and includes all checked first-party plugin
migrations on this branch. The migration-specific supported-game observations
are recorded in the corresponding mapping evidence files.

| Plugin | Registered raw adapter sources | Raw access owned there | Concrete follow-up boundary |
| --- | --- | --- | --- |
| `interest_bug_fix` | None | None. State-pool and POP payout uses typed readers, copied snapshots, `CurrentGameSession`, callback-scoped `BankInterestAccess`, and checked discard/payout/clear mutation. | Keep new bank/state/POP engine facts in the kernel-owned `game_state/` implementation; keep payout eligibility and allocation in the plugin. |
| `telemetry` | None | None. The telemetry module consumes typed `PopRef`/`FactoryRef` hook records plus copied current-state, country, and province snapshots. Current-state resolution, checked object snapshotting, hook patch/trampoline code, and thread-quiescence/unload draining live under `game_state/`. | Keep capture selection, filtering, aggregation, and publication in the telemetry module; put any new engine read, hook, offset, or native call in the kernel-owned implementation. |
| `campaign_runner` | None | None. The runner retains observer target/policy decisions, switch parsing, retries, state transitions, logging, and telemetry. The kernel-owned implementation owns frontend, console capture/command replacement/removal, checked copied callback arguments, annex/message hooks, popup counters, mappings, and native calls. | Keep future engine access in the kernel-owned implementation; runner inputs and observations remain copied. |
| `scripting` | None | None. Its C daily handler copies `SmedleyDailyEventV1`, queues plugin-owned `EventSnapshot` values, and maps campaign-control results to its established worker log semantics. Lua allocator and userdata `void*` values in `scripting_runtime.cpp` are not engine objects. | Keep new engine facts in kernel C services; retain copied Lua payloads and constrained queued operations. |

No first-party production source outside this table may include `smedley/v2`,
`smedley/clausewitz`, `smedley/std`, or `smedley/memory.hpp`, access
`memory::Map`, or introduce game-object `void*` declarations without an explicit
CMake registration. The inventory describes code ownership, not fresh runtime
verification of every listed legacy adapter.
