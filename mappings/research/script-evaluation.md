# Script evaluation and native UI research leads

This record helps kernel researchers locate the script machinery behind
triggers, effects, events, decisions, and casus belli (CB) checks. It is a
factual synthesis of external notes, not an engine API or an implementation
plan. **Every newly imported address, layout, ABI, and semantic claim below is
`historical-unverified`.** Existing Smedley evidence is linked separately and
retains its own claim-specific levels and limits.

## Provenance and interpretation

| Property | Value |
| --- | --- |
| Source | `maxioten/Victoria2-Reverse-Engineering` |
| Pinned commit | [`e299a0214d32274239402e8ee22039e1d02e101f`](https://github.com/maxioten/Victoria2-Reverse-Engineering/tree/e299a0214d32274239402e8ee22039e1d02e101f) |
| Reuse boundary | No license file was found in the pinned tree. This document restates factual leads in new language; it imports no source prose, code, assembly, or patch payloads. |
| Evidence limit | Documentary comparison only; newly imported semantics have no independent executable or runtime verification here. |
| Address convention | Unless explicitly called an RVA, addresses below are upstream preferred VAs, based at `0x00400000`. A runtime address would require the loaded module base plus the RVA, not the literal VA. |

The source's labels for disassembly inspection, measurements, inference, and
confirmation describe the author's evidence, not Smedley's validation. Even a
reported measurement remains `historical-unverified` here. The source's event
notes contain older hypotheses and later corrections; the distinctions below
preserve that uncertainty rather than combining them into an apparently
complete layout. The [mapping rules](../README.md#evidence-levels) govern any
later promotion. Exact executable identity and signatures alone would not
establish scope validity, ownership, threading, or safe mutation.

### Source coverage

These citations use immutable commit URLs and line anchors. Complete trigger
[vtable listings][trigger-vtables], [classifications][trigger-classes], keyword
maps and write-scan artifacts remain upstream research leads, not local datasets.

| Pinned source | Material synthesized here |
| --- | --- |
| [Engine/triggers-and-scopes.md][triggers] | Trigger dispatch, boolean containers, scope/value representation, flag lookup, effect ABI, limits of the write scan. |
| [Engine/decisions.md][decisions] | Decision layout, repeated eligibility checks, AI choice and deferred command execution, UI consumers. |
| [Engine/Event_System.md][events] | Loading, registries, candidate scans, MTTH, firing, immediate effects, option commands, delayed delivery, popup construction, corrections and unknowns. |
| [Engine/Casus_Belli_Evaluation.md][cb] | CB database, pairwise eligibility gate, attacker/defender scope binding, AI/UI/script callers. |
| [Engine/CEffectList_ParseEffect.md][effects] | Child-effect factory, parser dispatch, name resolution, assimilation and accepted-culture leads. |
| [Engine/flags.md][atlases] | Government-specific flag textures, cache lifecycle, D3D allocation and compression caveats; not script booleans. |
| [Engine/buttons.md][buttons] | Westernization callbacks and the tentative Play identification. |
| [Engine/utilities.md][utilities] | Seven helper addresses, competing names, and unresolved semantics. |

## System model and existing Smedley boundaries

The upstream model has three distinct stages: a loader turns script tokens
into objects; trigger objects evaluate conditions against a scope; effect
objects act on that scope. Events, decisions, and CB rules reuse this machinery
but supply different scopes and scheduling rules. Eligibility checks, choosing
an action, queueing it, and applying its effects are separate boundaries.
Timing one stage does not automatically measure the others.

Smedley's Lua scripts are a different system. They receive bounded copied daily
records on a worker and may queue supported operations; they do not receive
`CEventScope`, engine trigger/effect objects, or Victoria II's Lua state. See
[copied-state evidence](../evidence/scripting.md#copied-state),
[queued-pause evidence](../evidence/scripting.md#queued-pause), and
[ownership](../../docs/architecture.md#ownership). Nothing in this research
adds an evaluator, arbitrary effect execution, or raw engine access to scripts
or plugins.

| Existing Smedley record | Stronger or more specific boundary; where it stops |
| --- | --- |
| [Selected event option](../evidence/gfm-workload.md#selected-event-option-boundary) | RVA `0x00507e00` has narrow `verified-runtime` selected-country-event-option correlation and calls option executor RVA `0x004a61f0`. This is not general event dispatch, stable script-site identity, or a nested-reentrancy contract. RVA `0x004a8120` was not that boundary in the observed selected-option path. |
| [Automatic dispatch leads](../evidence/gfm-workload.md#automatic-event-dispatch-leads) | RVAs `0x0048d370` / `0x0048d6b0` have recorded byte matches and static ABI/dispatch evidence; runtime roles remain provisional. Their inclusive work includes setup/delivery, not just effects. |
| [Region iteration](../evidence/gfm-workload.md#region-scope-iteration-boundary) | `verified-static-callsites` distinguishes full filtered effect traversal from a first-match trigger. It does not establish source-site attribution or runtime trigger execution. |
| [Province reconciliation](../evidence/province-reconciliation.md) | Named modifier lookup, bounded list checks, owner/reform selection, and immediate removal have specific static/runtime evidence. This does not authorize arbitrary scope construction, reform writes, deferred drain, or general daily-callback mutation. |
| [Frontend lifecycle](../evidence/campaign-automation.md#frontend-lifecycle) and [native symbols](../v2game-3.04.toml) | Verified Single Player Play listener RVA `0x00372300`, checked GUI signals, controller lifetime, and campaign-entry readback are stronger than the upstream tentative Play label. They do not identify multiplayer or every UI action. |
| [Fixed-date completion](../evidence/campaign-automation.md#fixed-date-completion) | One day equals 24 raw date units within the existing runtime evidence. This resolves that unit locally, not the imported event frequency, parity, or MTTH behavior. |
| [Country-flag static lead](ai-static-leads.md#country-flags) | Independent PMIA disassembly corroborates country `+0x1b0`, lookup slot `+0x08`, and result boolean `+0x1c`. It does not verify the upstream trie internals, full object sizes, or thread safety. |

## Conditions and scopes

### Boolean dispatch and evaluation order

The [trigger notes][triggers] report 236 RTTI-identified classes. Their
classification is upstream analysis, not a local purity or safety finding.
Evaluation is at virtual slot 6, byte offset `+0x18`, with an x86 `__thiscall` scope argument
and `ret 4`. Top-level conditions such as event triggers, decision `potential`
and `allow`, CB `can_use`, and MTTH modifier conditions use embedded
`CAndTrigger` objects. The proposed `0x1c`-byte container holds a vtable, a
`0x18d` marker of unresolved meaning, and child-pointer vector metadata.

| Container lead | Evaluator VA; vtable VA | Reported rule |
| --- | --- | --- |
| `CAndTrigger` | `0x008b8260`; `0x00df9994` | Stop at the first failed child; an empty block passes. |
| `COrTrigger` | `0x008b88a0`; `0x00e2fb70` | Stop at the first passing child; an empty block fails. |
| `CNotTrigger` | `0x008b9240`; `0x00e2fba8` | Negate the OR of its children, not their AND; an empty block passes. |
| `CContextTrigger` | `0x008b94b0`; `0x00e2fbe0` | Evaluate nested conditions with constructed child scopes, including scope-changing forms such as owner and `FROM`. |

The source describes file-order evaluation without caching or reordering.
Cheap early rejection could therefore reduce later work, but that is not proof
that moving arbitrary conditions preserves behavior or that an external cache
is valid. Nested scope iteration makes cost dependent on visited entities, not
just child count. The source's nanosecond timings are not Smedley performance
evidence.

### Scope identity and copying

The newer [scope account][scope] identifies constructor VA `0x008a8650` and
copy helper VA `0x008a88d0`, with a proposed extent of `0x48` bytes.

| Scope offset | Imported interpretation and limitation |
| --- | --- |
| `+0x08` / `+0x0c` | Current country tag/key and country index. |
| `+0x10` | Province index when the caller supplies a province scope. |
| `+0x14` | POP pointer, initially the lazy null-POP object referenced through VA `0x01258a10`. Not authority to dereference a POP. |
| `+0x20` | Constructor's second argument, called a parent argument upstream. Its type/ownership is not established: decision code supplies `0x2d`, so it must not simply be modeled as an owned parent pointer. |
| `+0x24` | `THIS` scope reference, initially pointing to the scope being constructed. |
| `+0x30` / `+0x34` | `FROM` reference in the newer trigger analysis. Earlier event notes guessed a date-related meaning around `+0x2c/+0x30`; that guess is not a second compatible contract. |

A raw copy can leave `+0x24` referring to the original storage. The source
therefore warns that a copied scope may resolve `THIS` through the wrong
object. The existence of an engine copy helper is not evidence that arbitrary
byte copies relocate internal references or extend referenced lifetimes.
No constructor/copy invocation or replacement layout is supplied here.

The source's class grouping is 174 scalar, 20 country-reference, 40
pair-dependent, and two additional scalar-like helpers. Those are the author's
analysis categories, not proof that the scalar group is context-free or safe
on another thread. The supporting [class list][trigger-classes] and
[vtable list][trigger-vtables] remain external research artifacts.

### Values, script flags, and keyword registration

The source associates country-reference parser VA `0x008d3f50` with literal
tags and tokens `THIS` (`0x405`) / `FROM` (`0x3de`). Parsed flags are reported at
trigger `+0x18/+0x19`, with literal tag/index at `+0x1c/+0x20`. Generic numeric
and boolean parsers may accept those special tokens even when the eventual
evaluator does not use the flags. Parser acceptance is not semantic support.
Boolean parser VA `0x008b93b0` recognizes `yes` token `0x29f`; the `CIsAiTrigger`
lead stores a boolean at `+0x1c` and consults the game-state human-control map.
This does not override Smedley's narrower
[observer ownership checks](../evidence/campaign-automation.md#observer-ownership-and-view).

For `has_country_flag`, evaluator lead VA `0x008ccb90` looks up a string in
country `+0x1b0` through virtual offset `+0x08`. The proposed implementation at
VA `0x009e6c10` uses a case-insensitive ternary trie: node value `+0x00`,
character `+0x04`, and three branch pointers `+0x08/+0x0c/+0x10`. Separate
character-node allocation is a potential locality cost, not a measured Smedley
cache-miss count or a reason to replace flags. The PMIA overlap above only
corroborates the outer lookup boundary.

The same [trigger/effect notes][triggers] supply four script-flag mutation
leads, distinct from the texture subsystem discussed later:

| Operation | Executor VA; vtable VA |
| --- | --- |
| Set country flag | `0x008829e0`; `0x00e2ac88` |
| Clear country flag | `0x00882a40`; `0x00e2acc0` |
| Set global flag | `0x00882b60`; `0x00e2ad68` |
| Clear global flag | `0x00882ba0`; `0x00e2ada0` |

The clear-country path reportedly reads a flag-name string at effect `+0x20`
and country index from scope `+0x0c`. Allocation, flag lifetime, persistence,
and mutation preconditions are not established by those dispatch addresses.

Keyword registration lead VA `0x00c381c0` associates strings with numeric token
IDs. The pinned [decision keyword map][decision-keywords] and
[CB keyword map][cb-keywords] are useful cross-reference leads. Token strings,
IDs, and parser branches must be checked together; finding a string alone does
not identify a working effect or trigger class.

### Why the write scan does not prove thread safety

The [upstream scan][writescan-summary] reports following direct calls to depth
eight from 235 evaluator functions plus a decision virtual, covering 688
functions. Its absolute-address write inventory includes lazy singletons,
global-pointer stores, CRT/error paths, and thread-local exception state.
It explicitly excludes indirect calls into other object types and writes
through pointers. Finite call depth is another coverage limit.

Those omissions prevent a conclusion that all triggers are read-only or
thread-safe. Even an evaluator with no writes can race with mutable game state
or stale objects. The report's assumption that lazy objects already exist
mid-campaign is not an initialization or synchronization guarantee. It also
finds an RNG path through lazy null-POP construction; its separate observation
that no trigger class directly draws randomness must not become an unconditional
claim that evaluation consumes no random state. The [raw write scan][writescan]
is provenance for the hypothesis, not permission to parallelize evaluation,
snapshot engine objects by memcpy, or call evaluators from Lua workers.

## Effects: parsing is not execution

### Factory, child ownership, and dispatch

The [effect factory account][effect-factory] locates `CEffect::ParseEffect` at
VA `0x00876b30` (RVA `0x00476b30`). It describes `__thiscall`, two stack
arguments, and `ret 8`. The receiving object is the parent effect/block, not an
independent parser-owned list. Each new child is allocated, constructed,
appended, and then asked to parse its payload. The factory eventually stores
the keyword ID at child `+0x18`.

| Proposed effect field or virtual | Role; evidence limit |
| --- | --- |
| `+0x08/+0x0c/+0x10` | Child-list head/tail/count; node value/previous/next at `+0/+4/+8`, with a marker byte at `+0x0c`. This source example is not a universal engine-container definition. |
| `+0x14`, `+0x18`, `+0x1c` | Initialized byte, keyword ID, and initialized word respectively; unspecified fields remain unnamed. Subclass data begins at `+0x20` in the examples. |
| Virtual `+0x0c` | Block parser for effects with custom parsing. Such a parser must enforce its own opening-brace requirement. |
| Virtual `+0x10` | Child-effect factory. |
| Virtual `+0x1c` | Simple-value parser; receives a copied `0x104`-byte token (`0x41` dwords). |
| Virtual `+0x2c` | Execution lead: `__thiscall`, two stack arguments, `ret 8`. The second argument's general meaning is unresolved. |
| Virtual `+0x24` | Description/tooltip hypothesis; not a verified API. Other shared slots are not assigned semantics here. |

Engine effect addresses can reportedly be reused after a load. A pointer-keyed
annotation or cached identity would therefore need a verified invalidation
boundary; address equality alone cannot establish the same parsed object.
The upstream extension's side-table and detour design is not imported.

The parser wrapper exposes reader/stream `+0x1c`, an inferred diagnostic
location at `+0x24`, and current token type/text at `+0x228/+0x22c`.
The factory's roughly 110 keyword branches select constructors/vtables and
block-versus-value dispatch. Simple-value dispatch does not itself reject an
opening brace. Thus a keyword existing in the factory does not mean it accepts
an arbitrary nested block.

For unknown keyword IDs, the [fallback account][effect-fallback] describes
ordered name resolution: helper VA `0x004dc420` (then `0x004dacc0`),
`0x0057b0a0`, `0x005677b0`, POP-type lookup `0x0096dc20`, culture lookup
`0x004a2f70`, then `0x00973bd0`. Some results receive virtual acceptance checks.
Successful resolution constructs different effect types; identifying them as
specific tag, province, culture, or region scope operators remains conjecture.
A complete miss with a scalar value logs an error without adding a child;
a miss with a brace has a generic-object fallback. Consequently an unknown
name is not uniformly a hard parse failure.

### Concrete mutation leads

These mechanisms are retained to explain semantic differences, not to provide
native-call recipes. Neither is a supported Smedley mutation boundary.

| Keyword lead | Construction and execution leads | Reported behavior and uncertainty |
| --- | --- | --- |
| `assimilate`, token `0xa5b` | Object `0x24` bytes, vtable VA `0x00e2bae8`; value parser `0x0087c410`; executor `0x00894160`. | Boolean `+0x20` gates execution. The executor resolves the province from scope `+0x10`, walks every POP-type list, and changes POP culture to the province owner's primary culture. It reportedly ignores a narrower POP selection, does not merge POPs, and does not refresh accepted-status flags. |
| `add_accepted_culture`, proposed token `0x28f` | Object `0x28` bytes, vtable VA `0x00e2b9c8`; parser `0x00893390`; executor `0x00893490`. | Culture at effect `+0x20`, union selector at `+0x24`. Keyword-to-token identification was inferred upstream, not checked in the registration table. The union form walks the primary culture's group and adds non-primary cultures not already accepted. |

The [assimilation account][assimilation] associates province owner index
`+0x12c`, POP lists `+0x194` with `0x10` stride, POP next `+0x27c`, POP culture
`+0x6c`, and country primary culture `+0xe64` with that path. These are borrowed
field leads, not an approved traversal layout. In particular, the source
reports `assimilate` takes the simple-value path: supplying a block does not
create a supported block extension and may leave inner tokens for the parent
parser. Its modified implementation adds an accepted-status refresh that the
reported vanilla path lacks; that mod behavior is not imported as vanilla.

For [accepted cultures][accepted-culture], parser token `0x335` selects the
union form by token type. Other values go through the culture-name map, with a
reported null/default culture fallback instead of rejection. Getter VA
`0x00561180`, constructor `0x00561080`, and global `0x012587ec` identify that
singleton hypothesis; its full identity still needs checking. The implication
that a misspelled name can add the null culture is an unverified failure-mode
lead, not a tested vanilla guarantee.

Insertion helper VA `0x00522310` reportedly takes the country in `ESI`, a
stack culture pointer, and `ret 4`; ordinary `__thiscall` would be the wrong
model. It checks membership and appends to country `+0xe54/+0xe58/+0xe5c`,
without updating POP acceptance. Group reference culture `+0x50` and member
list group `+0x08` are inferred. No register stub or invocation is included.

### Name-map lookup family

The [lookup analysis][name-maps] proposes a family with the map in `EAX`, one
stack MSVC-string argument, `ret 4`, and an object-or-null result. It describes
a case-sensitive rolling hash using multiplier 61, bucket count at map `+4`,
bucket array at `+8`, chained object/next nodes, and length-aware byte
comparison. The reported object-name offsets distinguish instantiations:

| Lookup VA | Object-name offset; identification limit |
| --- | --- |
| `0x004dc420` | `+0x20`; object type unknown. |
| `0x0057b0a0` | `+0x2ac`; a country interpretation is only a source hypothesis. |
| `0x005677b0` | `+0x10`; object type unknown. |
| `0x00973bd0` | `+0x0c`; object type unknown. |
| `0x004a2f70` | `+0x18`; proposed culture lookup using global `0x012587e8`. |

The source uses inline-or-heap MSVC strings with length/capacity at
`+0x10/+0x14`. Those offsets do not validate arbitrary foreign strings.
Hidden register parameters must be recovered from callers before assigning
map identities. Smedley's separate
[modifier lookup](../evidence/province-reconciliation.md#modifier-lookup)
has a reviewed register ABI and lifecycle limits; it does not validate this
whole family.

## Event lifecycle

### Resource loading, objects, and registries

The [loading notes][event-load] identify loader VA `0x008a67b0`, header reader
`0x009a1440`, event constructor `0x008a5ac0`, and nested-block initialization
`0x008a5c70`. The proposed flow enumerates files through PhysFS from two search
roots, merges paths without duplicates, reads headers, allocates objects,
parses bodies virtually, and logs validation results. Calling the roots base
and mod directories remains an interpretation, not a verified mod-overlay rule.

Tokens `0x3ff/0x400` select manager vectors at `+0x00/+0x20`; the proposed
country/province keyword identities were not fully resolved in the earlier
notes. EOF is `0x13`. A `0x360`-byte wrapper owns a `0x114`-byte reader; the
three `0x104`-byte token slots start at `+0x20/+0x124/+0x228`. These are load-time
objects, not execution scopes despite shared terminology in decompiler names.

| Event-object lead | Proposed layout/mechanism |
| --- | --- |
| Extent and parser | `0x1c4` bytes; vtable VA `0x00e2c834`; virtual slot 3 parses the body. The constant `0x18d` is not a resolved type system. |
| Identity | Pair at `+0x08/+0x0c`, used by the object registry. Construction copies globals `0x01268fec/0x01268ff0`; their parsing role is inferred. |
| Evaluation/execution | Embedded condition at `+0x30`; nested object at `+0x4c`; immediate block at `+0x74`; option-pointer vector at `+0xb0`; MTTH at `+0xc0`. Nested block `+0x54` remains unidentified. |
| Strings | Seven members from `+0xdc` through `+0x184`, plus `+0x1a8`; names/localization need caller-specific interpretation. |
| Flags | `+0x1a3` controls fired-once recording/checks; the script name `fire_only_once` is inferred. Bytes `+0x1a1/+0x1a2/+0x1a4` affect UI suppression/variants; keyword identities remain unresolved. The constructor reportedly leaves `+0x1a4` to later initialization. |
| Diagnostics | Missing main name, secondary name, or options produce messages `0x9f/0xa1/0xa4` but reportedly do not remove the event. Unexpected header `0xc6`; per-file EOF message `0xca`. Logging is not successful validation. |

The [registry analysis][event-registries] uses `(type,id)` keys, with event type
`0x27`. Lookup VA `0x009c1d40` takes key pointer in `EAX` and table in `ECX`;
the proposed hash combines shifted type and ID before bucket reduction. Nodes
chain entries whose key is at `+0x08/+0x0c`. Resolver VA `0x009c16d0` selects
global `0x01258a94` for types above `0x1268`, otherwise `0x01258a98`.
The fired-once table is instead game state `+0xb64`. The described insertion
prepends without a duplicate check or rehash at that site; this is not proof
that no other path resizes the table or that duplicates are safe.

### Candidate scan and probability

The [later scan correction][event-scan-later] names a DayTick callsite at VA
`0x006861e8` for scan VA `0x008a7cd0` (`stdcall`, one argument, `ret 4`). This
is more specific than the earlier guessed monthly caller name, but remains
unverified here. It also corrects the original fixed-base/no-ASLR assertion.
The upstream daily-scan claims are:

1. Construct one stack scope, then reuse it while changing entity fields.
   Compute day parity from `(date - 0x029c55c0) / 24`; odd days select countries,
   other days provinces.
2. For countries, skip slot zero/invalid index and countries without provinces;
   fill country tag/index. Candidate vector begins at country `+0x08`.
   For provinces, skip unowned entries, fill scope province `+0x10`, and leave
   scope country fields at the constructor's placeholder/zero. Candidate vector
   begins at province `+0x14`. Province count comes through global
   `0x0125870c`, virtual `+0x24`.
3. For each candidate, evaluate its condition, compute probability if it passes,
   draw an MT value modulo one million, check fired-once membership after a
   successful roll, then fire the event.
4. Read candidate count once per entity but reload the vector begin pointer
   during traversal. Immediate effects can change state and the candidate list
   before the scan finishes. Reusing one immutable list snapshot would not
   necessarily preserve the reported behavior.

Where candidate lists are populated and `is_triggered_only` is filtered remains
unknown. A missing/zero MTTH yielding certainty is a reason that filter would
matter, not proof of where it occurs. No candidate-list cache or removal rule
is established.

The [MTTH analysis][event-probability] identifies VA `0x008ae380`. It proposes
base and factors scaled by 1000, with all modifier conditions visited rather
than stopping after the first match. Passing modifiers multiply the current
value with fixed-point rounding. For positive values over 1000, integer days
feed a per-million probability of mode times one million divided by days,
clamped from one to one million. Values at most 1000 produce certainty; a
negative result from an initially positive base has a special minimum-
probability branch. Exact signed arithmetic and rounding need local review.

Country mode 1 versus province mode 2, combined with alternating scans, led
the author to suspect different effective mean delays. That is not a verified
interpretation of gameplay MTTH or permission to compensate frequencies.
The reported MT state/index VAs `0x00f0ecf0/0x00f0f6b0`, engine pointer
`0x00f20c18`, and draw counter `0x00f20c14` are only RNG search leads.
Moving the fired-once check before the draw would change random consumption.

### Firing, immediate effects, options, and delayed events

The [firing analysis][event-fire] locates VA `0x008a8120`, proposed
`__thiscall` with event in `ECX`, game-state/scope stack arguments, and `ret 8`.
Its address is RVA `0x004a8120`: do not confuse it with Smedley's more narrowly
verified selected-option action RVA `0x00507e00`.

The source places fired-once insertion before recipient handling. An AI
recipient is inferred from the human-control map and presence of country AI.
It computes option weights using VA `0x008aeb00`, draws from a separate
state-based RNG helper VA `0x009b7610`, builds an option command at VA
`0x00907d40`, and queues it through the session/idler object. That object at
game state `+0xb24` must not be reduced to a generic window-manager pointer:
Smedley's [lifecycle evidence](../evidence/campaign-automation.md#frontend-lifecycle)
distinguishes frontend and in-game identities there.

The reported option-chance block has base/modifiers and a final multiplier,
scaled by 1000. Modifiers require all their conditions, and a nonpositive
running weight can stop further weight processing. The chooser reportedly
recalculates weights for accumulation after computing their total, uses an
integer cumulative-percent comparison, and falls back to option zero for a
negligible total. Exact thresholds, rounding, selection bias, and the assertion
that repeated evaluations are pure/deterministic have not been checked here.
They are not grounds to cache weights or replace RNG behavior.

The human-recipient branch checks local-country/UI readiness and event flags
before constructing/showing a window. The immediate block at event `+0x74`
is reported to execute after recipient processing, even if no window is shown.
Thus popup visibility, option selection, queued option execution, and immediate
effect execution are not interchangeable observations. Smedley's generic
[observer popup suppression](../evidence/campaign-automation.md#observer-ownership-and-view)
likewise does not suppress effects or AI choices.

For [effect-driven events][event-effects], parser VA `0x008e7f80` handles token
`0x344` into effect `+0x20/+0x24`, interpreted as event ID and delay. Other
branches resolve names or `this` and can report unresolved-name message
`0x16d1`; their exact object types remain unclear. Execution lead VA
`0x0088d370` resolves `(0x27,id)` in the event registry, reportedly consumes
two 15-bit LCG values for scope setup, fires immediately for nonpositive delay,
and otherwise schedules through VA `0x00540a70`. The LCG constants reported
are multiplier `0x343fd` and increment `0x269ec3`; no RNG implementation is
imported. VA `0x0088d460` is a description/tooltip lead, not event execution.
The more cautious existing country/province dispatch evidence linked above
remains authoritative for Smedley; automatic delivery and general scope
identity have not been runtime-correlated there.

### Event-window native leads

The [window account][event-window] locates constructor VA `0x008a8f30`,
proposed `ret 0x50`, with vtable `0x00e2cdb8` and an allocated `0xd0`-byte
object. It stores event/context information, constructs per-option widgets,
resolves named GUI elements, localizes text, and reports missing elements with
message `0x115`. Show-window lead VA `0x006480e0` and UI-readiness helper
`0x0068b610` remain incompletely identified.

Main/secondary text getters VAs `0x008a6010/0x008a60f0` use custom output
handling, with a third getter at `0x008a6070` and a localization hypothesis at
`0x008adbe0`. Another window helper, `0x008ab700`, is not analyzed sufficiently
to assign its full effects.

The source flags a traversal over player-country POPs in the window constructor
whose calls to VA `0x00963d10` appear to discard the result. It interprets the
callee as a weighted POP-size calculation, with cultural adjustments, but the
meaning is unresolved. Claims that the loop is redundant and the callee pure
are retained only as hypotheses; they do not establish that removal preserves
initialization, RNG, or other indirect effects. No patch bytes or instructions
for bypassing that loop are imported.

## Decisions: evaluation versus queued execution

The [decision notes][decisions] identify constructor VA `0x008743c0`, vtable
`0x00e29b54`, and keyword handler `0x00874ce0`. Name is at `+0x08`, `potential`
at `+0x24`, `allow` at `+0x40`, effect block at `+0x5c`, and MTTH-like
`ai_will_do` at `+0x110`. Keyword IDs `0x729/0x72a` distinguish the two
condition blocks. Manager global VA `0x01319730`, getter `0x00875040`, and
manager vectors `+0x0c/+0x28` identify the collection lead, not its lifetime.

The proposed daily AI routine VA `0x00821190` is reached from country frame
job callsite `0x0081ea9b`. It takes the AI object in `ECX`, constructs a scope
with argument `0x2d`, fills tag/index from AI `+0x30/+0x34`, and scans the
manager's `+0x28` list before `+0x0c`. Per candidate it reportedly:

1. Invokes the decision slot-6 predicate twice; the inspected 3.04 target
   VA `0x00a31490` is an always-true stub. This does not establish all virtual
   overrides or justify deleting the calls.
2. Tests `potential` before `allow`, sharing the current country scope.
3. Uses chance evaluator VA `0x00874ea0` and CRT `rand` VA `0x00ab047b` modulo
   100, rather than the event scan's Mersenne Twister.
4. On selection, constructs a `0xa0`-byte command through VA `0x00905b30`,
   including the decision name and scope copy, queues it through session
   virtuals, and returns after that choice. Effects run later in command
   processing, not at the eligibility check.

The producer still allocates, consumes RNG, and modifies a queue. Its separation
from effect execution does not make the whole routine read-only or safe to
parallelize. Likewise, a failed queue/delivery observation would not prove a
failed decision condition.

Other proposed consumers include diplomacy update VA `0x0085d270`, player
alert VA `0x005399c0` (including per-frame idler callsite `0x00654e48`), politics
view refresh paths `0x0046b3a0/0x0047d0f0`, additional UI paths
`0x006db2e0/0x006d2dd0`, and tooltip VA `0x0053a6d0`. The alert stops at an
available decision. UI refresh frequency is not daily AI frequency.

The source's TGC counts suggest many repeated checks of country-tag gates and
irreversible flags, but those patterns do not prove a flag can never change
through another writer, save edit, or mod. Caching eligibility or deleting
checks needs a complete dependency/invalidation contract. Smedley's
[GFM flag analysis](../evidence/gfm-workload.md#derived-state-flags) separately
distinguishes caches, policy classifications, deliberate pulses, and latches;
TGC observations do not transfer to GFM policy.

## Casus belli: pair-dependent eligibility

The [CB notes][cb] identify manager getter/load/find VAs
`0x0090cc40/0x0090cdc0/0x0090da40`, parsing `common/cb_types.txt` with separate
handling of `peace_order`. Type-pointer vector metadata is at manager
`+0x0c/+0x10`. Type vtable `0x00e33724` associates keyword handling
`0x0090a1e0`, validity `0x0090a7e0`, real-type stub `0x00a31490`, and `CanUse`
`0x0090a7f0`; null-type vtable `0x00e3374c` instead has a false real-type stub.
Whether null types occur in the manager vector is unresolved.

The important distinction is between owning a CB and a CB type permitting use:

| Stage | Imported behavior |
| --- | --- |
| Any-type search, VA `0x0092ede0` | Test real types and return on the first usable type; reported stack ABI is `stdcall` with relation/defender/attacker references. |
| Per-type gate, VA `0x0092eec0` | Type index arrives in `EDI`. Walk relation held-CB list `+0x50`, following node `+0x08`, matching held CB type index `+0x24`; otherwise require type `always` byte `+0x132`. |
| Scope setup | Outer/current country is attacker; a separate defender scope is referenced through outer `THIS` at `+0x24`. `FROM` is unset. This differs from a standalone country's self-referencing scope. |
| `CanUse` | Evaluate `can_use` without a reported cache. The inspected path does not consult `is_valid`, `allowed_*`, duration/months, or `is_triggered_only`; that is not a claim that those properties are unused elsewhere. |

AI callsites interpret relation as defender `+0xbe8` indexed by attacker.
Daily target scoring leads VA `0x0085db40` (war) and `0x0085e0e0`
(diplomacy), reached through AI daily/diplomacy update, scan other countries
with distinct strength/relationship gates. Their `IsAnyCBUsable` callsites
are `0x0085dcda/0x0085e5fb`. Country `+0xe4c` is the
[state-list count](../evidence/interest-payout.md#verified-traversal-and-mutation-boundaries),
not the upstream AI-score label. The meanings of country `+0x37c/+0x37d`, AI
`+0x19`, and the strength-ratio global remain unresolved; no generic
country-pair scheduling contract follows.

Other leads include monthly processing, AI planning, diplomacy UI validation,
and the script `casus_belli` evaluator VA `0x008d28c0`. Repeated CB triggers can
therefore nest further `can_use` work within event/decision evaluation.
Console VA `0x00420eb0` reportedly supports diagnostic CB descriptions; it is
not an exposed Smedley script capability. Prioritizing cheap attacker-only
checks is a source optimization hypothesis, not authorization to rewrite
conditions, memoize pairs, or alter iteration/RNG order.

## Flag textures, buttons, and ambiguous helpers

### Flag atlases are not script flags

The [atlas notes][atlases] concern `gfx/flags/flagfiles*.tga` and matching text
indices, assembled from country/government flag images and cached under the
user's game/mod directory. Their proposed builder/loader is VA `0x007e3900`.
The reported D3DX call chain distinguishes:

| Stage | Callsite VA and upstream interpretation |
| --- | --- |
| Individual image load | `0x007e55cc`: file-in-memory texture load, managed pool, format inferred from content. |
| Atlas allocation | `0x007e5ada`: A8R8G8B8, one mip, managed texture. |
| Save preparation | `0x007e629c/0x007e62cc`: allocate and copy a texture surface. |
| Cache encoding | `0x007e6367`: TGA encoder; contrasted with map-cache DDS encoding at `0x00803a52`. |
| Cache reload | `0x007e6492`: PhysFS-loaded content with unspecified format passed to D3DX. |

The author reports five vanilla 2048-by-1024 atlases versus eleven TGC
4096-by-2048 atlases. An uncompressed 32-bit image at the latter dimensions
contains 32 MiB of pixels; managed-texture system-memory residency and the
roughly 352 MiB aggregate are source explanations, not measured Smedley process
or GPU costs. Loader format detection is the proposed reason compressed DDS
content can load under a `.tga` name. The source's opacity observation applies
to its inspected TGC conversions only: it neither proves all mods lack alpha
nor that lossy DXT1 compression preserves all visual information. No cache
conversion, read-only-file workaround, asset import, or graphics patch is
included.

### Button leads and the stronger Play boundary

The [button list][buttons] names Westernization callback VA `0x006dfe80` and
clickability check `0x00541b90`, with a tentative Play callback
`0x00772300`. Upstream confirmation labels do not establish their argument
conventions, object identity, state transitions, or safe invocation. The
Westernization pair remains `historical-unverified` here.

For Play specifically, the same preferred VA converts to RVA `0x00372300`,
already identified in the [Smedley catalog](../v2game-3.04.toml) as the Single
Player listener. The [frontend evidence](../evidence/campaign-automation.md#frontend-lifecycle)
requires the captured controller/thread, registry lookup, native press/release
signals, load completion, and in-game RTTI readback. That is the usable narrow
boundary, not a direct call inferred from the older button name. The upstream
single-player/multiplayer distinction remains unresolved outside that boundary.

### Utility names remain competing hypotheses

The [utility list][utilities] is primarily a rename notebook. It offers no
complete callsite or runtime evidence; arrows between names must not be read
as verified call chains.

| VA | Retained factual lead and ambiguity |
| --- | --- |
| `0x0043ab60` | Memory-copy label later narrowed to zeroing a structure array, possibly 64-bit integer storage. Overlap behavior and element semantics are not established. |
| `0x00aad56b` | Size-validation versus allocation/operator-new helper; neither interpretation is a supported contract. |
| `0x0041a160` | Default/MTTH/event-constructor guesses conflict with the more contextual POP-type database constructor hypothesis in the event/effect notes. The latter is more specific, not independently verified. |
| `0x0096bbf0` | Null-POP string construction and AI-strategy-vtable references led to singleton/AI guesses. No stable class or ownership assignment follows. |
| `0x00ab4d81` thunk | Former stream-state label was revised toward multibyte/local-code-page state (`__ptmbcinfo`). It is not an established stream API. |
| `0x0047db00` | Buffer-resize label revised to a `CGoodsPool` copy-constructor hypothesis; not a generic byte-copy routine. |
| `0x004f50c0` | Proposed factory-level decay when inputs are absent; no formula, phase, or runtime correlation supplied. |

## Limits, exclusions, and verification status

This document preserves mechanisms and uncertainties, not executable changes.
The following material is deliberately excluded:

- Upstream assembly, C/C++ wrappers, pseudocode bodies, detours, vtable
  replacement recipes, stolen-byte assertions, code caves, and patch payloads.
  None is a supported Smedley operation.
- Proposed event-scan frequency changes, early fired-once skipping, and popup
  loop removal as recommendations. Their factual risks remain relevant:
  altered random consumption, delivery latency, probability saturation, and
  unresolved indirect side effects. Multiplayer parity alone would not prove
  vanilla equivalence.
- Unrelated AI/economy/network research, game assets, binaries, runtime data,
  and an exhaustive duplicate address catalog. Subsidiary keyword/class/scan
  artifacts are cited as source leads, not revalidated datasets.
- Transferring TGC counts or timing to vanilla, GFM, another campaign interval,
  or the current Smedley binary. For scale only, the source reports about
  529,000 daily decision-potential checks and roughly 23.5 ms of a 200 ms TGC
  day, about 11 ms/day for CB scans including 4.8 ms in `can_use`, and roughly
  5,000–6,600 event-trigger checks/day in its early TGC scenario. These are
  external workload observations with different counting boundaries, not
  additive costs or independently reproduced optimization evidence. Even the
  source's decision totals differ between dynamic and static counts.

Documentary validation establishes that the pinned paths contain the described
leads and that existing Smedley records narrow several overlapping claims.
It does not validate machine bytes, callable ABIs, object lifetimes, all parser
branches, thread safety, save behavior, or multiplayer determinism. Any future
engine use must pass the existing exact-executable, signature, phase, thread,
metadata, and operation-specific checks and obtain evidence for that specific
claim. No claim is promoted by this import.

[triggers]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/triggers-and-scopes.md#L1-L105
[scope]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/triggers-and-scopes.md#L30-L70
[writescan-summary]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/triggers-and-scopes.md#L72-L85
[trigger-classes]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/data/trigger-classes.txt
[trigger-vtables]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/data/trigger-vtables.txt
[writescan]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/data/trigger-writescan-2026-10-11.txt
[decisions]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/decisions.md#L1-L54
[decision-keywords]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/data/decision-keyword-map.txt
[cb]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Casus_Belli_Evaluation.md#L1-L81
[cb-keywords]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/data/cb-keyword-map.txt
[effects]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/CEffectList_ParseEffect.md#L1-L371
[effect-factory]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/CEffectList_ParseEffect.md#L59-L174
[effect-fallback]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/CEffectList_ParseEffect.md#L176-L197
[assimilation]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/CEffectList_ParseEffect.md#L199-L220
[accepted-culture]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/CEffectList_ParseEffect.md#L222-L295
[name-maps]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/CEffectList_ParseEffect.md#L297-L329
[events]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Event_System.md#L1-L634
[event-load]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Event_System.md#L66-L224
[event-registries]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Event_System.md#L148-L162
[event-scan-later]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Event_System.md#L609-L634
[event-probability]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Event_System.md#L226-L276
[event-fire]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Event_System.md#L287-L330
[event-effects]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Event_System.md#L377-L406
[event-window]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Event_System.md#L332-L375
[atlases]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/flags.md#L1-L37
[buttons]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/buttons.md#L1-L10
[utilities]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/utilities.md#L1-L36
