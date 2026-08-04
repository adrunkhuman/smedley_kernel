# Project Guidance

## Scope

This repository is the public development base for Smedley, a native
instrumentation, automation, and extension framework for Victoria II: Heart of
Darkness 3.04. `master` is the authoritative branch.

Smedley extends the original game. It is not a replacement engine. Preserve the
game's established simulation, UI, save format, multiplayer behavior, and mod
compatibility unless a user explicitly enables a documented change.

The project has two audiences:

- Players and modders use the launcher, built-in tools, profiles, telemetry,
  scripts, and documented plugin settings without writing C++.
- Native contributors use C++, reverse engineering, mappings, and plugins to
  expose more of the engine and implement fixes or optimizations.

The bundled framework must be useful before third-party plugins are installed.
Observer mode, campaign automation, diagnostics, structured telemetry,
profiling, and safe launch configuration are first-party product behavior.

## Upstream And History

The public upstream is
[`ZombieFreak115/smedley_kernel`](https://github.com/ZombieFreak115/smedley_kernel).
Keep it as the `upstream` remote. This fork is a continuation of that work, not
an unrelated rewrite.

An older private fork and its local checkout contain experimental campaign
loading, observer, console, mapping, and economy-tracing work. Treat that tree
as an audit source. Port a change only after reviewing its assumptions and
retaining its evidence. Do not copy build output, caches, undocumented offsets,
or code merely because it ran once.

Prefer focused commits on `master`. The project does not require pull requests,
but each commit must leave the checked-in build and documentation coherent.
Do not rewrite published history.

## Supported Game

The initial compatibility target is the supplied English Victoria II: Heart of
Darkness 3.04 Windows executable.

| Property | Value |
| --- | --- |
| SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| File size | `12294656` bytes |
| Architecture | x86 |
| Preferred image base | `0x00400000` |
| Image size | `0x01092000` |

Reject an unsupported executable before injection. A matching version string,
file name, or plausible instruction sequence is not sufficient. Keep addresses
relative to the loaded module and expect ASLR.

Do not commit Victoria II executables, assets, saves, mods, screenshots, crash
dumps, generated traces, or other copyrighted/runtime material. Tests may use
paths supplied by the developer or small synthetic fixtures.

## Evidence

Every reverse-engineered address, field, function, calling convention, hook,
and semantic claim needs a recorded evidence level:

| Level | Meaning |
| --- | --- |
| `verified-runtime` | Observed in the supported live game and correlated with visible or independently readable behavior. |
| `verified-current` | Bytes and structure match the supported executable and current code uses them successfully. |
| `verified-static-callsites` | Reviewed callers support the proposed semantics, but runtime behavior is not fully exercised. |
| `provisional` | A useful, explicit hypothesis that must not drive default mutation. |
| `historical-unverified` | Recovered from earlier Smedley work and not yet validated against the supported executable. |
| `historical-skeleton` | Layout or naming retained only as a search lead. |

Byte matches prove that code exists. They do not prove a calling convention,
object lifetime, thread requirement, side effect, or safe mutation boundary.
Record provenance, expected bytes, callers, runtime probes, and known limits in
`mappings/`. Never present a historical class layout as a supported API.

Telemetry must carry the applicable mapping and quality status. Emit an unknown
or unavailable field as absent. Do not turn an unreadable value into zero.

## Product Boundaries

### Launcher

The graphical launcher and CLI must use one shared launch core. The UI must not
reimplement executable validation, mod discovery, profile parsing, process
creation, injection, or diagnostics.

A launch profile may select:

- The game installation and executable.
- Ordinary Victoria II `.mod` descriptors.
- Smedley plugins and plugin configuration.
- Game settings that Smedley can read and write without discarding unknown
  values.
- A save, observer settings, telemetry, profiling, and run controls.

Keep a no-injection launch path. A player must be able to launch the original
game or recover from a broken plugin without editing files by hand. Preflight
must detect unsupported executables, missing modules, invalid manifests,
dependency conflicts, architecture mismatches, and unsafe combinations before
the child process resumes.

Do not make the GUI the source of truth. Profiles and plugin settings use
documented files that the CLI can consume. Keep automation possible without a
desktop session.

### Plugins

Native plugins are arbitrary executable code. Show that trust boundary in the
launcher and documentation. Do not claim sandboxing for a DLL.

Move toward a versioned, narrow plugin ABI. Do not add more public ABI surface
based on MSVC STL containers, `std::function`, ownership across DLL boundaries,
or undocumented compiler settings. Existing ABI behavior may remain while a
compatible migration path is designed and tested.

Plugin manifests must have stable IDs, versions, architecture and API
requirements, dependencies, conflicts, capabilities, and compatibility data.
The loader must reject incompatible or duplicate plugins cleanly. Plugin
failure must not silently produce a partially initialized campaign.

Ordinary mod authors should not need C++. Prefer stable script, Lua, data, or
Clausewitz trigger/effect bridges over one-off native plugins once the required
engine boundary is verified. Keep C++ available for advanced contributors.

### Observer And Automation

Observer mode means no country remains human-controlled. A viewing country is a
camera and UI perspective, not a hidden player country. Choose a living country
from current game state when the user does not request one. Recover when the
viewing country disappears.

Campaign automation is an explicit state machine. Wait for observable native
state transitions instead of fixed sleeps. Prefer native frontend operations to
synthetic input. Mouse and keyboard automation is acceptable only as a
documented fallback when no verified native route exists.

Separate these concerns even when one plugin implements them:

- Selecting and loading a save.
- Entering a campaign.
- Returning player countries to AI.
- Selecting speed and pause state.
- Suppressing or handling modal UI.
- Running until a date or condition.
- Saving, collecting results, and exiting cleanly.

Do not hardcode Jan Mayen or another tag as the default observer perspective.
Do not report success until player control, AI scheduling, date advancement,
and requested visibility are verified.

### Telemetry And Profiling

Log decisions and transitions, not only periodic snapshots. Where mappings
permit, answer both what happened and why it happened: candidates, scores,
rejections, selected actions, effects, and outcomes.

Use a stable, versioned structured envelope. Records need a run ID, monotonic
sequence, wall time, game date when verified, event type, category, entities,
mapping identity, quality, and typed payload. Never use process pointers as
durable entity identifiers.

Instrumentation must remain bounded:

- Check category and entity filters before expensive extraction.
- Avoid allocation, formatting, blocking I/O, and filesystem flushes in hot
  game callbacks.
- Buffer records and make dropped data explicit.
- Keep partial traces readable after a crash.
- Measure and report instrumentation overhead.
- Keep high-volume capture opt-in.

JSON Lines is the initial portable interchange format. CSV is an export for
stable record families, not the canonical high-volume stream. Add a binary
format only when measurements establish that buffered JSON Lines is a material
bottleneck.

Do not log guessed AI reasoning. A state snapshot is not a decision trace.
Instrument candidate generation and scoring before claiming to explain an AI
choice.

### Fixes And Optimizations

Ship gameplay changes as independently selectable, documented plugins. Default
behavior remains compatible with the original game unless the project has a
clear, reviewed reason to change it.

Profile before optimizing. Source counts and large event files identify
investigation targets, not runtime bottlenecks. Compare identical starting
saves over fixed simulation intervals and record throughput, CPU time, memory,
event behavior, and resulting state.

Preserve deterministic ordering and random-number consumption in compatibility
mode. Treat broad parallelization of POP, economy, AI, or military systems as
experimental until replay and multiplayer behavior are understood.

The exact supported executable is already Large Address Aware. Do not advertise
another 4 GB patch or message-pump tuning as a speed optimization. Existing
experiments found speed 5 effectively unpaced and found no gain from changing
the speed-5 application-service interval.

## Hook Safety

- Verify the executable before resolving any target.
- Check expected bytes immediately before patching.
- Decode or otherwise establish complete overwritten instructions.
- Preserve registers, flags, stack alignment, and calling convention.
- Replay displaced behavior exactly unless the hook explicitly replaces it.
- Coordinate threads while changing executable memory.
- Check `VirtualProtect` and other Win32 results.
- Flush the instruction cache after code changes.
- Install related hooks transactionally and roll back on failure.
- Track hook ownership and reject conflicting patches.
- Run UI and frontend operations on the game UI thread.
- Check phase, runtime type, readable memory, and invariants before dereferencing
  a game object.
- Do not retain raw game pointers across lifecycle transitions unless ownership
  and lifetime are proven.
- Keep observational hooks non-mutating until their semantics are verified.

## Foreign Engine Objects

- A code signature validates a code location, not the identity, layout, or
  lifetime of data reached through that code. Never use a matching hook
  signature as the sole justification for an object-field mutation.
- Before accessing a field through a mapped offset, establish the supported
  executable, current object identity, lifecycle phase, owning thread,
  readable or writable span, and field-specific invariants. A readable pointer
  is not proof that it names the expected object.
- Validate foreign container and string metadata from a bounded raw snapshot
  before calling methods such as `size()`, `c_str()`, iterators, or virtual
  functions. Treat malformed but readable metadata as invalid.
- Treat ABI mirror types as non-owning views unless ownership is explicitly
  established. Do not add general destructors or copy semantics that could
  release engine-owned memory.
- Do not placement-construct over a live engine object unless a native mutation
  function is unavailable, the previous state is proven not to own resources,
  and all potentially failing preparation completes before the old lifetime
  ends.
- Mutation must fail closed. Validate preconditions before the first write,
  perform the smallest possible mutation, verify semantic postconditions, and
  stop automation if identity or state changes during the operation.
- Retained engine pointers require a verified invalidation boundary, such as a
  destructor hook or observable phase transition. Clear them before native
  storage is released.
- Separate borrowed engine-layout views from locally owned argument objects.
  Never use an ABI container mirror as a general-purpose C++ container.

Make `DllMain` minimal. Perform initialization through an explicit exported
entry point after the loader lock is released. Catch plugin failures at the
kernel boundary and preserve a useful diagnostic whenever safe recovery is
possible.

## GitHub Issues

Use issues as the persistent project plan and reverse-engineering notebook.
Write for maintainers who scan before they read:

- Titles describe the required outcome or observed problem.
- Each open issue body is the current contract. Preserve requirements, exact
  values, paths, links, acceptance criteria, explicit exclusions, and unchecked
  work.
- Separate current evidence, required work, acceptance, and later scope.
- Use short paragraphs, compact checklists, and small tables only when they
  improve scanning.
- Keep comments as terse chronological records of commits, runtime evidence,
  failures, decisions, and remaining limits.
- Keep checklist state honest. Close an issue only when merged behavior and
  verification are visible, or when a concrete blocker is recorded.
- Prefer precise domain terms over generic progress language.

Use `bug` or `enhancement` for type. Add only useful area labels such as
`launcher`, `kernel`, `plugins`, `telemetry`, `automation`, `mapping`,
`performance`, `documentation`, and `research`.

## Documentation

The root README is for users first. Explain what Smedley does, its trust model,
the supported game, installation, launching, profiles, built-in tools, examples,
and current limitations before contributor internals.

Keep detailed contracts in `docs/` and reverse-engineering evidence in
`mappings/`. Include examples for both ordinary modders and native contributors.
Do not make users infer current behavior from issue history or source code.

Comments preserve non-obvious rationale, ABI requirements, verified offsets,
and engine quirks. Do not narrate C++ syntax. Keep evidence near the mapping or
wrapper that depends on it.

## Naming And Ownership

- Use one canonical term. Name code for its domain responsibility, not its
  mechanism or history. Avoid vague `Util`, `Helper`, `Manager`, `Data`,
  `Info`, `Common`, and `Core`; reserve `Core` for non-I/O implementation below
  a named product boundary.
- `plugins/` contains loadable plugins and code exclusively owned by them.
  Shared game readers belong in `game_state/`. Preserve stable external
  IDs and candidate, raw, and evidence names.

## Structure

- Co-locate tests with the code they cover. Keep dependencies target-scoped and
  give each production target one owner.
- Keep generated, input, handwritten, and evidence material distinct.

## Tests And Probes

- Automated test DLLs belong under the test tree and use the
  `*_test_plugin` name.
- Probes are opt-in and state their exact claim, restoration, and removal
  condition. Do not retain exploration merely as history.
- Do not merge intermediary debug hooks, event APIs, CSV fields, or mappings
  into `master` unless they have a durable product contract and production
  validation. Remove them after the investigation they support.

## Durable Documentation

- Document steady-state behavior, not the sequence of a change, and state
  positive ownership.
- `docs/` records current contracts, `mappings/` records evidence, and GitHub
  issues own future work. Keep evidence IDs and hashes in `mappings/`.

## Refactoring

- Establish boundaries before broad renames. Do not add private compatibility
  aliases or mix unrelated renames, behavior changes, and mapping claims.
- Split by responsibility, not size.

## Code And Build

The injected runtime currently requires MSVC x86 because it uses x86 inline
assembly and must match Victoria II's process architecture. Do not imply x64 or
cross-platform runtime support.

Preserve the established C++ style unless changing a file comprehensively:
four spaces, K&R control-flow braces, PascalCase types and functions,
snake_case variables and property accessors, and underscore-prefixed private
members. Prefer explicit ownership and small native interfaces in new code.

Generated files must be reproducible. Declare generators and dependencies in
the build, identify generated output, and provide a verification command. Do not
hand-edit generated sections without updating their source model.

Do not commit `build/`, IDE state, caches, Python bytecode, installed game
plugins, generated runtime logs, traces, saves, screenshots, or dumps.

## Verification

For host-side changes, run the x86 Release build and tests with the Visual
Studio 2022 toolchain. The supported local CMake executable is:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" `
  -S . -B build -A Win32 -DV2_GAME_DIR="C:\path\to\Victoria 2"
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" `
  --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Before any runtime test:

1. Validate the executable identity and all active mapping signatures.
2. Build x86 Release artifacts from the commit being tested.
3. Use a disposable save or preserve the source save unchanged.
4. Record enabled mods, plugins, settings, and mapping version.
5. Watch the game and Smedley logs for hook, parser, plugin, and lifecycle errors.

A successful process launch does not prove injection, plugin initialization,
campaign entry, observer correctness, telemetry completeness, save integrity,
or mod compatibility. Verify the behavior changed or the requested record was
produced.

For a mapping or mutation, retain the narrowest reproducible probe. For an
optimization, retain a baseline and patched benchmark from the same starting
state. For a launcher change, test both Smedley and no-injection launch paths.
