# Game-state boundary

Bundled plugins must separate gameplay policy from reverse-engineered engine
access. The boundary is internal C++, not a new public plugin ABI. It prevents a
plugin from silently accumulating its own offsets, calling conventions, and
foreign-object assumptions.

## Layers

| Layer | Owns | Must not own |
| --- | --- | --- |
| Kernel and mappings | Hook installation, raw engine headers, executable identity, signatures, and engine adapters | Plugin gameplay policy or allocation choices |
| `smedley_game_state` and `smedley_game_runtime` | Typed non-owning references, bounded reads, copied snapshots, and the narrow checked interest mutation boundary | Plugin lifecycle, telemetry policy, file I/O, or allocation policy |
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

## Interest mutation

`DailyInterestAccess` is callback-scoped capability derived from the active
`DailyInterestEvent`. It is non-copyable and checks the current dispatch,
country, thread, and `AFTER` phase. Do not cache it, recreate it, or use it from
another callback.

The interest plugin performs a complete preflight before its first write:

1. It collects typed POP candidates and computes a deterministic allocation in
   plugin-owned storage.
2. It checks allocation conservation and invokes `PreparePopInterest` for every
   nonzero payout.
3. It calls `ApplyPopInterest` only after every preflight succeeds. Apply does
   not re-resolve POP identity or ownership; it rechecks access, signature, the
   bound address and amount, writable range, and preflight money snapshot, then
   verifies the expected POP-money postconditions.

Preflight does not reserve engine state and application is not transactional
across several POPs. A later POP can fail after earlier writes succeeded. There
is no verified rollback operation: the plugin reports `partial_mutation`,
disables further payouts, and must not retry blindly.

The runtime owns the verified `CPop::GiveMoney` adapter because its RVA, calling
convention, signature, and writable field span are engine evidence. Allocation
remains plugin policy: it defines eligibility, weighting, ordering, rounding,
telemetry, and whether a failure disables the feature. A generic game-state
service must not silently choose those gameplay semantics.

## Public boundaries

Do not duplicate a public C API merely to expose game objects. The copied daily
event C API and the Lua-facing copied APIs remain valid external boundaries:
they provide data and queued/constrained operations without lending object
pointers. New external capabilities should follow that model, with their own
version, ownership, lifetime, and thread contract.

The `game_state` C++ headers are internal source contracts for bundled plugins.
Their symbols, layouts, and source interfaces may change with the repository;
they provide no third-party binary or source compatibility promise.

## Raw-access inventory

This inventory starts from `498d674` and includes the checked interest-path
migration on this branch; it is not a claim of fresh supported-game runtime
validation.

| Plugin | Registered raw adapter sources | Raw access owned there | Concrete follow-up boundary |
| --- | --- | --- | --- |
| `interest_bug_fix` | None | None. Creditor and POP payout uses typed readers, copied snapshots, `CurrentGameSession`, `DailyInterestAccess`, and checked preflight/apply mutation. | Keep new creditor/POP engine facts in `smedley_game_state`; keep payout eligibility and allocation in the plugin. |
| `telemetry` | `plugin.cpp`, `economic_capture.cpp/.hpp`, `artisan_consumption_hook.cpp/.hpp`, `factory_consumption_hook.cpp/.hpp`, `factory_sales_hook.cpp/.hpp`, `pop_cash_flow_hook.cpp/.hpp` | Current-game/country/province ABI views; POP, artisan, factory consumption and sales hook RVAs; mapped field and stack offsets; engine object pointers; `memory::Map`; patch/trampoline code; x86 native hook assembly. | Move one verified hook or object conversion at a time to a named runtime adapter, preserving copied records, bounds, and hook evidence before removing each registration. |
| `campaign_runner` | `campaign_launcher.cpp/.hpp` | Frontend, menu, idler, console, country, and campaign object views; raw string/vector ABI arguments; controller pointers; mapped fields, vtables, RTTI, RVAs, signatures, hooks, and x86 native calls. | Promote each validated observation or command separately; completion means orchestration no longer includes raw layout or memory headers for that action. |
| `scripting` | `plugin.cpp` | Current-game/country views, idler RTTI and vtable inspection, pause RVA/signature validation, guarded memory reads, and the native pause call path. Lua allocator and userdata `void*` values in `scripting_runtime.cpp` are not engine objects. | Move the idler and pause operation into checked adapters while retaining copied Lua payloads and constrained queued operations; no Lua-visible object handle is required. |

No first-party production source outside this table may include `smedley/v2`,
`smedley/clausewitz`, `smedley/std`, or `smedley/memory.hpp`, access
`memory::Map`, or introduce game-object `void*` declarations without an explicit
CMake registration. The inventory describes code ownership, not fresh runtime
verification of every listed legacy adapter.
