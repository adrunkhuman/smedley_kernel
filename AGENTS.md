# Project Guidance

## Smedley

Smedley is a native instrumentation, automation, and extension framework for
Victoria II: Heart of Darkness 3.04. It extends the original game; it is not a
replacement engine. Preserve vanilla simulation, UI, save, multiplayer, and mod
behavior unless the user explicitly enables a documented change. `master` is
the authoritative branch.

The supported target is the supplied English Windows executable:

| Property | Value |
| --- | --- |
| SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| File size | `12294656` bytes |
| Architecture | x86 |
| Preferred image base | `0x00400000` |
| Image size | `0x01092000` |

Reject any other executable before injection. A matching name, version string,
or plausible instruction sequence is insufficient. Resolve addresses relative
to the loaded module and expect ASLR.

Do not commit game executables, assets, saves, mods, screenshots, dumps, logs,
traces, installed artifacts, or other copyrighted or generated runtime data.

## Architectural Invariants

- `smedley_kernel.dll` solely owns interaction with Victoria II engine state:
  mapped addresses, native calls, foreign-memory access, hooks, checked
  mutations, engine-thread state, and narrow engine layouts.
- Plugins consume engine capabilities only through public, versioned C service
  tables. They may receive copied bounded records and opaque generation-bound
  handles; they must not import kernel internals, traverse raw engine objects,
  or define native calling stubs and engine offsets.
- No C++ ABI crosses a DLL boundary. Do not expose STL types, exceptions,
  ownership, compiler-specific classes, or internal C++ declarations between
  the kernel and plugins.
- Name and group kernel services by the capability they provide, not by the
  first plugin that needs them. Generic capability belongs in the kernel;
  plugin-specific composition and policy remain in the plugin.
- Observation and telemetry services may group related capabilities when the
  instrumentation facility itself is the stable product abstraction. Do not
  split a coherent facility into consumer-shaped one-off APIs.
- Scripts and Lua use supported copied state and queued operations. They do not
  receive raw engine pointers, layouts, or unrestricted mutation access.
- Native plugins are trusted code, not a sandbox. Plugin failure must be
  contained at the kernel boundary and must not silently leave a partially
  initialized campaign.
- Keep `DllMain` minimal. Initialize through explicit exports after the loader
  lock is released.

## Reverse Engineering

Every reverse-engineered address, field, function, calling convention, hook,
and semantic claim needs an evidence level recorded in `mappings/`:

| Level | Meaning |
| --- | --- |
| `verified-runtime` | Observed in the supported live game and correlated with visible or independently readable behavior. |
| `verified-current` | Bytes and structure match the supported executable and current code uses them successfully. |
| `verified-static-callsites` | Reviewed callers support the proposed semantics, but runtime behavior is not fully exercised. |
| `provisional` | A useful, explicit hypothesis that must not drive default mutation. |
| `historical-unverified` | Recovered from earlier work and not yet validated against the supported executable. |
| `historical-skeleton` | Layout or naming retained only as a search lead. |

`mappings/` is the source of truth for provenance, expected bytes, reviewed
callers, runtime probes, uncertainty, and known limits. A byte match proves only
that expected code exists. It does not prove a name, type, calling convention,
object identity, lifetime, thread requirement, side effect, or safe mutation
boundary. Never promote historical or provisional evidence to supported
behavior without the required static and runtime validation.

Unknown or unreadable state remains unavailable. Do not infer a numeric zero or
semantic value from a failed read. Telemetry carries the applicable mapping and
quality status.

## Unsafe Engine Boundary

Before any engine read, call, hook, or write:

1. Validate the exact executable and applicable mapping signatures.
2. Establish the object identity, lifecycle phase, owning thread, and required
   readable or writable span.
3. Check field-specific and operation-specific invariants.
4. Fail closed before the first side effect if any precondition is uncertain.

### Foreign Engine Objects

- Use narrow kernel-private layouts for verified boundaries. They are borrowed
  views, not public APIs or general-purpose C++ object models.
- Keep borrowed engine-layout views separate from locally owned native-call
  arguments. Do not give borrowed layouts copy, destruction, or ownership
  behavior that could release engine memory.
- Validate foreign string and container metadata from bounded raw snapshots
  before using lengths, elements, iterators, methods, or virtual calls. Readable
  memory alone does not establish a valid object.
- Do not retain raw engine pointers across lifecycle transitions without a
  verified invalidation boundary such as a destructor hook or observable phase
  change. Clear retained references before native storage is released.
- Prefer copied records and opaque handles outside the immediate checked
  boundary. Validate handle generation and session ownership on every use.
- Mutation must validate all preconditions before writing, perform the smallest
  possible change, verify semantic postconditions, and stop automation if
  identity or state changes during the operation.
- Do not placement-construct over live engine state unless no verified native
  mutation exists, prior ownership is understood, and all fallible preparation
  completes before the old lifetime ends.

### Hooks And Patches

- Prefer MinHook function-entry hooks. For any patch, verify expected bytes at
  the target immediately before modification.
- Establish complete overwritten instructions and preserve registers, flags,
  stack alignment, calling convention, and displaced behavior unless the hook
  explicitly replaces it.
- Coordinate threads, check every protection operation, and flush the
  instruction cache after executable-memory changes.
- Install related hooks transactionally, roll back failures, track ownership,
  and reject conflicting or unknown active bytes.
- Keep observational hooks non-mutating until their semantics are verified.
- Run UI and frontend operations only on the verified game UI thread.

## Ownership And Structure

- Give each engine capability and production source one owner. `game_state/`
  contains kernel-owned implementation; `plugins/` contains plugin-owned policy
  and integration.
- Add general engine capability at the narrowest reusable kernel boundary.
  Do not create a kernel API merely to mirror one consumer's workflow.
- Split code by responsibility, not file size. Keep dependencies target-scoped
  and co-locate tests with the behavior they cover.
- Do not add compatibility layers, aliases, or duplicate implementations without
  a concrete persisted-data, shipped-API, or external-consumer requirement.
  Delete superseded implementations once migration is complete.
- Keep generated inputs, generated outputs, handwritten implementation, and
  mapping evidence distinct. Generated outputs must be reproducible.
- Probes are opt-in, state the exact claim and restoration condition, and are
  removed when their investigation ends. Do not retain exploratory hooks,
  fields, mappings, or APIs as history.

## Hot Paths

- Keep hook and game-callback work bounded and non-blocking.
- Do not allocate, format, perform filesystem I/O, flush, or wait on contended
  locks in hot callbacks.
- Filter before expensive extraction. Use fixed-capacity copied records or
  opaque handles with explicit lifetime contracts.
- Bound queues and traversals. Report drops, truncation, invalid samples, and
  other loss explicitly; never silently convert loss into valid data.
- Preserve deterministic ordering and random-number consumption unless an
  explicitly experimental mode documents otherwise.

## Verification

- Build the injected runtime with MSVC for x86 Release and run the complete
  CTest suite. Follow `BUILDING.md` for commands and supported tooling.
- Run ownership, layering, DLL-boundary, and mapping validation relevant to the
  changed boundary.
- Host tests do not establish live-game behavior. For engine mappings, hooks,
  mutations, injection, campaign control, or other runtime-sensitive changes,
  test the exact built commit against the supported executable.
- Before a runtime test, validate executable identity and active signatures,
  use a disposable save or prove the source save remains unchanged, and record
  enabled plugins, mods, settings, and mapping version.
- Inspect game and Smedley logs for hook, parser, plugin, and lifecycle errors.
  A launched process alone does not prove injection, plugin initialization,
  campaign entry, observer correctness, telemetry completeness, native exit, or
  save integrity. Verify the specific behavior or record under test.
- Retain the narrowest reproducible runtime evidence for new mappings and
  mutations. Compare identical saves and intervals for performance claims.

## Domain Guidance

Keep this file limited to cross-cutting invariants. Read the authoritative
domain document before changing that area:

| Area | Source |
| --- | --- |
| Build, install, and validation tooling | `BUILDING.md` |
| Contribution workflow and issue reporting | `CONTRIBUTING.md` |
| Repository C++ style | `STYLING.md` |
| Plugin ABI, manifests, services, and examples | `docs/plugin-development.md` |
| Engine ownership and service boundaries | `docs/game-state-boundary.md` |
| Launcher, profiles, and automation options | `docs/launcher.md` |
| Telemetry schemas, capture, health, and exports | `docs/telemetry.md` |
| Scripting API and limits | `docs/scripting.md` |
| Mapping catalog and evidence | `mappings/README.md` and `mappings/` |

Document steady-state behavior in `docs/`, evidence in `mappings/`, and future
work in GitHub issues. Do not duplicate detailed domain contracts here.
