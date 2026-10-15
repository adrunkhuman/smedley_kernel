# Native trigger evaluation: purity and scope ownership

## Contract and provenance

A native trigger is not necessarily an observation. Evaluation can insert
variables, rebuild country data, update reference caches, mutate its input
scope, or initialize shared fallback objects. A read-only evaluator can still
depend on a CB defender or become stale before a cached result is consumed.
These are separate contracts, not one whitelist property.

This record retains selected findings from the **2026-10-15 trigger-whitelist
audit**, not a supported evaluator API or a parallel-execution implementation.

| Property | Evidence and limit |
| --- | --- |
| Target | English Windows Heart of Darkness 3.04, x86; SHA-256 `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648`, size `12294656` bytes. |
| Address convention | Preferred-image VAs at base `0x00400000`; subtract that base for RVAs and add the loaded module base at runtime. |
| Native findings below | `verified-static-callsites`: supported-image RTTI, raw vtable slots, instructions, caller pointer provenance and reachable callees were inspected. No live-game guard, race, behavior or performance verification. |
| Caller safety rules | Inferences from those mechanisms; the event/MTTH rule below additionally relies on the existing `historical-unverified` event-loop account. It is explicitly `provisional`, not a newly verified loop mapping. |
| Local audit source | `/home/adrunkhuman/Projects/v2/analysis/trigger-whitelist-audit-2026-10-15/`; source roles are listed below. This path is provenance, not a repository dependency. |

| Audit source | Claims retained here |
| --- | --- |
| `REPORT.md`, `audit-merged.json` | Integrated RTTI coverage, per-instance restrictions and distinction between purity, dependency and cache lifetime. |
| `audit-group-1.md`, `audit-group-2-helpers.md`, `audit-group-3.md`, `audit-overrides.json` | Virtual targets (including the guarded building-definition lookup), flags/variables, pointer-write destinations, initialization and reference-mode proofs. |
| `audit-containers.md` | Scope copying/assignment, selectors, iterators and incoming-seed ownership. |
| `audit-extra.md` | Non-suffix classes, modifier recursion, parser fallback and receiver-local MT evidence. |
| `native/`, `extra-native/` and supplemental raw disassembly | Executable-derived primary evidence for the instruction/callsite anchors below; intentionally not imported. |

The [Maxioten import](script-evaluation.md#why-the-write-scan-does-not-prove-thread-safety)
already records trigger dispatch, shallow THIS aliases, ternary flag lookup and
why a depth-limited direct-call write scan cannot establish thread safety.
**New here** are independent supported-image inheritance/slot checks, resolved
virtual targets, concrete pointer-write counterexamples, exact reference-mode
exceptions and guarded initialization paths. The imported claims retain their
original levels unless a specific claim is independently established below.
No runtime binding, hook, executable signature or loader change is added.

## Inventory follows inheritance, not class spelling

The audit found **251 concrete primary-vtable CTrigger-derived classes**,
excluding abstract `CTrigger`. The earlier suffix-filtered scan covered 235:
16 derived class names do not end in `Trigger`. The imported report's separate
236-class count is historical, not the complete concrete inheritance closure.
Raw primary vtable `+0x18` slots were checked for all 251 classes; secondary
interface tables must not be counted as independent evaluator receivers.

Useful omissions include `CIsNotPayingDebt` (`in_default`, evaluator
`0x00901eb0`), `CWorkAvailable` (`work_available`, `0x008e5490`), and
`CIsIndependant` (native spelling `is_independant`, `0x009010c0`). Most notably,
`CMTTHModifier` is a concrete recursive AND, sharing evaluator `0x008b8260`:
it forwards the borrowed scope to children through virtual `+0x18` at
`0x008b8286`, short-circuits on false, and passes an empty vector. Its owner
parser recognizes `modifier` at `0x008af236` and installs its vtable at
`0x008af26c`; `factor` is metadata, not another condition. Purity therefore
requires the actual children, not just the wrapper's class.

This is an inventory correction, not approval of all 251 classes. Generic
trigger dispatch is `0x008b33e0`; dynamic definition names and owner-specific
parsers also create nodes. Script keys cannot be recovered reliably by turning
RTTI names into snake case. No duplicate per-class catalog is maintained here.

## Confirmed writes during evaluation

| Trigger | Mechanism and primary anchors | Boundary / limit |
| --- | --- | --- |
| `check_variable`, `0x008da5c0` | Country `+0x1dc` is the CVariables receiver. A lookup miss calls `0x0047d7b0` at `0x008da620` with zero as the initial value. Insertion dispatches through receiver virtual `+0x18`, target `0x00a5c650`; list/tree implementations `0x00a5c690` / `0x00a5c820` update links and counts. Tree stores include `0x00a5c86b`, `0x00a5c8b0`, `0x00a5c8c6`. | Missing is not merely read as zero: it creates a zero-valued variable. The store at `0x008da61a` is argument-stack scratch, not the decisive shared write. An existing-key specialization would need its own lookup/locale contract. |
| `can_create_vassals`, `0x008d7d40`; `is_possible_vassal`, `0x008d8030` | Calls at `0x008d7d62` / `0x008d8052` reach `0x00519a40`. It resets country-owned vector end `+0xe30` to begin `+0xe2c` at `0x00519a84`, then appends via `0x004d61d0` at `0x00519bb3`, potentially reallocating. | This is a per-call rebuild, not a cold singleton path. Warmup does not remove the writes. `is_possible_vassal` compares stored target index `+0x20`; its evaluator does not honor the ordinary THIS/FROM parser flags. |
| `has_faction`, `0x008ff670` | POP branch sets ESI to POP `+0x220` at `0x008ff6d8` and calls `0x00967f50` at `0x008ff6e1`. That resolver writes `[ESI]` at `0x00967f7f` or `0x00967f8f`, including null results. | Existing rebel hash lookup `0x009c1d40` is read-only; its caller's POP reference-cache update is not. A country-present branch bypasses this cache path, but is not a class-wide approval. |
| `can_build_in_province`, `0x00902790` | If the building parameter exists and current country is absent, stores at `0x009027bb` / `0x009027c1` copy FROM into input scope `+0x08/+0x0c`; `0x009027ce` / `0x009027d4` can substitute THIS. No restoration follows. | Later eligibility checks do not make earlier scope writes observational. A country-present restriction avoids these stores, not all input-validity or dependency obligations. |

Selected byte anchors in the supported file: `0x00519a84 = 89 4b 04`
(store to vector end), `0x00967f7f` and `0x00967f8f = 89 06`
(store to borrowed reference cache). These identify the instructions; they are
not sufficient signatures or authorization to invoke the functions.

## Resolve getters and initialization separately

### Flags and variables share an actual ternary lookup

Country constructor stores at `0x004f6c6f` and world constructor store at
`0x0067f811` establish primary CFlags vtable `0x00df817c` at country `+0x1b0`
and world `+0x64`. Its `+0x08` slot is `0x009e6b10`, which reaches ternary
search `0x009e6c10`; the secondary table `0x00df81a0` is not that receiver.
Country constructor `0x004f6cb5` installs CVariables table `0x00dfbb60` at
country `+0x1dc`. Its lookup slot `+0x04` targets `0x00641ba0`, which resolves
the string buffer and dispatches through `+0x08` to the same ternary search.
This closes the lookup virtuals; it does not remove the variable miss insertion.

The search itself does not insert. Character folding calls CRT `tolower`
`0x00aaedb6`. With `DAT_00f1fc74 == 0`, the inspected ASCII fast path is
register-only. The other route reaches locale refresh and a CRT thread-data
flag write at `0x00aaec86`, with further uncertified callees. Thus the audited
`has_country_flag` / `has_global_flag` subset requires the original CFlags
receiver and that stable C-locale condition. No live locale guarantee follows.

### Constant virtuals are closed only for the native receiver set

| Receiver | Primary vtable VA | Slot | Target |
| --- | --- | --- | --- |
| CPop / CNullPop | `0x00e36428` / `0x00dfd2e8` | `+0x20` | true / false |
| CState / CNullState | `0x00dfd78c` / `0x00dfd7f8` | `+0x24` | true / false |
| CBuilding / CNullBuilding (definitions) | `0x00dfdac0` / `0x00dfdaf0` | `+0x24` | true / false |

Here true is `0x00a31490`, bytes `b0 01 c3`; false is `0x0058cfd0`, bytes
`32 c0 c3`. RTTI receiver closure and raw slots, not getter-like names, support
these claims. Runtime province/state building objects are different hierarchies
from building definitions. Unknown or replaced vtables invalidate the proof.
Child Evaluate dispatch is not a validity getter and still needs recursion.

### An apparent getter may construct a shared null object

| Path | Shared write / sufficient exclusion |
| --- | --- |
| Fresh scope `0x008a8650`; region scope evaluation | Null-POP publication at `0x008a86c8` / `0x008c5e2d`, global `0x01258a10`. Require a fully constructed stable null-POP before those paths. |
| Majority culture `0x009692c0` | Out-of-range result calls `0x00561180`; publication at `0x005611c3` to `0x012587ec`. Constructor `0x00561080` also adds the null culture to a shared group list. Exclude fallback or establish that object beforehand. |
| Majority religion `0x00969210` | Manager publication through `0x00567740` to `0x0125ea24`; missing selected entry can publish null religion through `0x005676d0` to `0x0125880c`. A warm manager alone does not cover the null fallback. |
| Goods `0x004b9520`; building manager `0x004dc340` | Publish `0x01258728` / `0x0125ce80` (stores `0x004b9556` / `0x004dc36e`). Building lookup miss can also construct null building through `0x004dacc0`, global `0x01258730`. |
| POP traversal / type lookup | `0x00423700` publishes type manager `0x0131bb34` at `0x0042372e`; missing type can publish null-POP-type `0x01258a2c` through `0x0096bb70`. Null-POP-type is not the scope's null-POP. |

Nonzero is not enough: construction, stability and receiver identity must be
established before evaluation. Empty children do not generally bypass scope
construction or manager lookup. Do not initialize engine objects on workers
merely to satisfy a purity inspector. These examples are not an exhaustive
guard list or permission to admit whole classes.

## Scope copies retain aliases and differ in seed behavior

| Helper / caller | Mechanism |
| --- | --- |
| Fresh constructor `0x008a8650(0)` | THIS `+0x24` points to self; valid byte `+0x1c` is set; seed `+0x40` becomes 1. Dword `+0x44` is not initialized on this route. Construction can initialize null-POP. |
| Copy helper `0x008a88d0` | EAX destination, ECX source; copies primary fields and shallow references through `+0x3c`, resets `+0x40` to 1, and does not copy/initialize `+0x44`. Iterator destinations inspected in the audit are stack-local. |
| Assignment `0x00504750` | EAX destination, ECX source; transfers fields `+0x04` through `+0x44`, including both seed dwords, without copying the vtable, resetting the seed or rebinding THIS. |
| Context evaluator `0x008b94b0` | Constructs output on the stack at `0x008b94c0`; passes original input in EAX at `0x008b94cd` and output in ESI at `0x008b94d0` to selector `0x00876240`. Ordinary selector assignment is input-to-local-output. |

Consequently, copying a scope is not a deep snapshot, and changing its primary
country does not make THIS refer to that country. It can retain a defender-bound
alias. THIS/FROM selectors also retain POP/state fields; do not invent a
normalization that clears them. Country branches of AnyCore/AllCore overwrite
the **local** FROM country with the original current country before clearing
current country: stores `0x008d17b1/0x008d17c3` and
`0x008d1d18/0x008d1d2a` target stack scope `+0x2c/+0x30`. Province branches
retain inherited FROM. Source-level decompilation alone missed these stores.

### Random selection writes the incoming seed

The audit distinguishes 17 deterministic context-selector forms from `ally`
(token `0x3e8`) and `local_enemy` (`0x717`). In the latter two, selector EDI is
the **incoming** scope, not the new local output:

- `ally`: `0x008764dd` supplies EDI `+0x40`; `0x008764e6` calls `0x009b7910`.
- `local_enemy`: `0x00876747` supplies EDI `+0x40`; `0x0087674a` calls the same
  helper when candidates exist.
- `0x009b792a`, bytes `89 02`, stores through that seed pointer.

A local output does not make these branches pure on borrowed input. One ally
still advances the seed; an empty ally list is not an exemption (native code
divides by its size). Local enemy's empty-candidate branch skips the draw.
A nested iterator may instead supply a private input scope, but admitting that
case requires proving ownership, alias separation and no escape or concurrent
reader. Boolean AND/OR/NOT alone provide no such separation. Deterministic
selectors still require initialization guards and pure concrete children.

### An MT callee is not necessarily a global RNG draw

On the fallback-constructor route through `0x009b7500`, instruction
`0x009b7518`, bytes `8d 44 24 40`, supplies a **stack-local MT receiver** in
EAX to `0x009b7610` at `0x009b7537`. Its counter store at `0x009b765c` and
twist helper `0x009b7700` write that receiver. The callee address alone does
not prove global RNG advancement. These cold paths are already rejected for
singleton/list writes; local RNG does not make them pure. Conversely, the
explicit incoming-seed stores above are mutations regardless of global RNG.

## THIS, FROM and rebel dependencies are instance-specific

The reference parser `0x008d3f50` sets trigger bytes `+0x18` for THIS (token
`0x405`) and `+0x19` for FROM (`0x3de`). That common parser does not imply that
all evaluators honor those bytes or resolve FROM identically.

| Evaluator / form | Actual dependency and conservative restriction for defender-independent reuse |
| --- | --- |
| THIS resolver `0x008a8dc0` | Reads `scope+0x24`, preferring its country, province owner, real-POP owner, then state representative owner before stored reference. Writes through ESI are caller outputs: e.g. `0x008f45a1` / `0x008f45b9` pass stack addresses. A pointer store is not automatically a scope write. |
| `this_culture_union`, `0x008f4580` | In addition to THIS `+0x18` and FROM `+0x19`, **trigger byte `+0x24`** selects `this_union` (parser `0x008f4520`, token `0x91f`). Require all three bytes zero; checks are at `0x008f458c`, `0x008f45b3`, `0x008f45ea`. Trigger `+0x24` is not scope `+0x24`. |
| `in_sphere`, `0x008f9c70`; `is_sphere_leader_of`, `0x008fa2f0` | FROM reads **THIS's FROM**, `[scope+0x24]+0x30`. In contrast, culture-union and prestige/industrial/military score FROM paths read current `scope+0x30`. Track both provenance paths; clear the instance reference flags for conservative literal-only reuse. |
| `controlled_by`, `0x008ca2e0` | Flags `+0x18/+0x19` clear is insufficient: literal REB with owner-mode byte `+0x24 == 0` reads scope rebel reference `+0x38/+0x3c`. Require owner mode or a non-REB literal as well. |
| `is_core`, `0x008bde30` | Rebel selector byte `+0x20` is separate from THIS/FROM. The audit excludes that entire branch from its guarded purity subset; conservative defender independence also excludes it. |
| `brigades_compare`, `0x00900bc0` | `0x00900bd6–0x00900c01` obtains current FROM, else THIS country, else THIS.FROM before testing current country. Literal-looking parameters do not eliminate the pair dependency. |

Scope transformations must propagate country/province/POP/state, THIS, FROM,
rebel and news provenance rather than inherit a scalar class label. A
reference-dependent predicate can be read-only and still be unusable in an
attacker-only CB prepass. Purity also does not make a POP/province predicate
meaningful in a country-only scope.

## Registered tokens can still be dropped by the trigger parser

| Spelling | Native route in `0x008b33e0` |
| --- | --- |
| `treasury`, token `0x2d0` | Registered at `0x00c3fa34`, string `0x00e23d5c`. Range checks `0x008b372b–0x008b3735` route it to generic fallback `0x008b5c49`. |
| `pop_militancy`, token `0x86b` | Registered at `0x00c4f0c6`, string `0x00e1e810`. Dispatch `0x008b4bc4` uses byte table `0x008b6794`, entry `0x27`; jump table `0x008b66f4` routes it to `0x008b5c49`. |

Neither has a fixed trigger case. Fallback tries loaded technology, ideology,
issue, POP-type, region and goods definitions, then context selectors
(`0x008b5c49–0x008b5f25`). Neither spelling matched an eligible definition in
the audited vanilla/TGC data. Selector failure leads to the diagnostic branch
`0x008b5fa2–0x008b602b`; `0x008b5fed` loads `Unknown trigger-type `, and return
at `0x008b604f` occurs **without appending a node**. No always-false replacement
or implicit money/militancy alias is created. Native `money` (`0x4f0`) and
`militancy` (`0xa68`) are distinct trigger cases.

Thus source-node counts need not match native trigger-tree counts. Definition
search covered the supplied vanilla data and TGC main-mod snapshot
`bda780249420efbcdd2b1e0ec905934a602c4c29`, not optional submods. This is static
parser/data evidence, not a captured runtime diagnostic. Effects are a different
parser domain; these findings do not reject the same spellings as effects.
Different definitions or a modified executable could change fallback
resolution. No script repair is proposed here.

## Consumer safety and remaining limits

| Permission | What must be established independently |
| --- | --- |
| Speculative / worker evaluation | Closed read-only path, correct scope and receiver types, guards established before work, stable reachable world/definitions/lifetimes, and compatible numeric/locale thread state. |
| Reuse across CB defenders | No dependency on the varying defender through current fields, THIS/FROM, rebel/news context or transformed-scope aliases. Native pair setup is described in the [CB research](script-evaluation.md#casus-belli-pair-dependent-eligibility). |
| Reuse over time | Dependencies remain unchanged until consumption, or results are invalidated/rebuilt on every relevant mutation. Purity and defender independence do not certify a whole-day cache. |

**Event/MTTH safety inference (`provisional`).** The existing
[event-loop account](script-evaluation.md#candidate-scan-and-probability)
places trigger success before MTTH modifier evaluation (`0x008a7cd0` then
`0x008ae380`), followed by RNG and possible firing. If a caller has speculative
results for later candidates, it must discard them after a successful trigger
**before MTTH processing**, unless the reached modifier trees are also proven
pure. Trigger purity alone does not cover modifier evaluation: a modifier can
invoke the same mutating predicates. This is a caller constraint inferred from
that imported ordering, not a new native-loop or runtime observation. Pure
modifiers would remove only this invalidation reason; firing/immediate effects
and any other intervening mutation still require separate invalidation.

Unclosed is not synonymous with impure. For example, `in_default` has a closed
no-matching-debt subset (`0x00511160–0x00511195`), but a matching debt reaches
local long-string allocation and possible manager initialization. Warming the
manager does not certify allocator/new-handler callbacks. Five party-loyalty
and news string-copy closures likewise remain unresolved. `casus_belli` and
`war_score` reach instance-specific nested CB trigger trees, not merely
constant virtual getters. None receives blanket approval here.

No live initialization, FPU/locale guard, cache invalidation, native-equivalence,
multiplayer, save or performance test was performed. A frozen-world assumption
is a precondition of the static proof, not evidence that the engine supplies
such an interval. Any future kernel access still needs the
[engine-boundary checks](../../docs/architecture.md#ownership) and
[claim-specific mapping validation](../README.md#evidence-levels).
