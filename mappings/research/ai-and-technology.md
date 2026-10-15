# AI scheduling, decisions and technology: historical leads

This is an original factual synthesis of the AI and technology research in
[maxioten/Victoria2-Reverse-Engineering][upstream], pinned to commit
`e299a0214d32274239402e8ee22039e1d02e101f`. No license file was found in that
checkout. No upstream prose, reconstructed code, diagrams or patch recipes are
imported here.

**Evidence level: `historical-unverified` for every new semantic claim below.**
Upstream confidence labels, verification summaries and reported measurements
are not Smedley evidence levels. The limited local byte checks described below
establish locations only. These are investigation anchors, not callable APIs,
supported layouts, safe hooks or authority to change vanilla gameplay.

All numeric code/data addresses here are preferred-base **VAs**, with image base
`0x00400000`; subtract that base for an RVA. Object offsets are relative to the
specific object named. Never use a VA as a file offset or assume fixed runtime
loading. The supported executable identity and validation rules remain in the
[mapping guide](../README.md) and [catalog](../v2game-3.04.toml).

## Scope and evidence precedence

Coverage includes all eight `AI/*.md` files, `AI/Ai_function_inventory.json` and
`Technology/technology.md`. This synthesis retains proposed roles, mechanisms
and selected address anchors rather than duplicating the upstream function
inventory. The `AI/bodies/*.json` implementation records, other Engine documents,
binaries, plugins, scripts and gameplay patches are not imported.

Existing [Player Military AI leads](ai-static-leads.md) retain their separate
provenance. [Telemetry](../evidence/telemetry.md),
[fiscal/credit](../evidence/fiscal-credit-boundaries.md) and
[interest payout](../evidence/interest-payout.md) own stronger Smedley claims.
Agreement with an upstream name does not upgrade either source; disagreement
does not downgrade local evidence. Economy-specific conflicts are collected in
[economy leads](economy-leads.md#conflicts-and-authoritative-local-boundaries).

| Boundary | Interpretation to retain / limit |
| --- | --- |
| Country `+0xe44/+0xe48/+0xe4c` | Smedley establishes state-list head/tail/count. **`+0xe4c` is not an AI strength score**, despite the [upstream field label](https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Symbols/structs.csv#L40). A comparison involving it may compare state counts, not strategic power. See [state traversal](../evidence/interest-payout.md#verified-traversal-and-mutation-boundaries). |
| World `+0xb3c` | Smedley's [military observations](../evidence/telemetry.md#country-and-province-candidates) identify wars, not a generic special-modifier list. Upstream's later `CURRENT_WARS` label also fits that distinction; the scoring function still needs separate review. |
| Province `+0x128/+0x12c`, `+0x130/+0x134` | Owner key/index and controller key/index are distinct. Upstream shorthand sometimes calls an index a province ID or a key an owner pointer. Do not propagate those types. |
| Civilization fields | Technology paths read country `+0x12d0`; some budget/relation paths read `+0x67c`. Similar prose names do not make these interchangeable. A byte read does not establish a four-byte Boolean layout. |
| Focus population | The focus notes use province `+0x1a8` as population, then describe it as plurality-like. Do not transfer country `+0x1a8` plurality semantics onto a province. |

## AI work is queued separately from country daily updates

The [queue analysis][jobs] places enqueueing near the end of daily tick VA
`0x006859c0`. It walks world `+0xa4`, selects countries with provinces and
appends country tag/index jobs to world `+0xb84`. Each bucket header occupies
16 bytes; the proposed bucket count is `(end at +0xb88 - begin at +0xb84)/16`,
with country-index modulo bucket count selecting a bucket. Single-player's
reported one bucket is an observation, not a universal bound.

| Anchor | Proposed contract and limits |
| --- | --- |
| Idler callsite `0x006557c4` → `0x006874c0` | Drain queued AI work while bucket zero is nonempty. Upstream proposes a stdcall entry taking the bucket array; ABI is not independently established here. |
| Frame worker `0x006874c0` | Pop jobs; recheck province ownership and country `+0x208` AI pointer; dispatch `0x0081e9e0`. Stop at empty bucket or a reported 20 ms elapsed-time limit. Constant VA `0x00e456c8`, timer global `0x01258a8c`, virtual offset `+0x14` are search anchors. |
| Time advance `0x00682bd0` | Pending buckets reportedly prevent the next day from starting. The queue is therefore a pacing dependency, not merely background work. |
| Country job `0x0081e9e0` | Reaches decision worker `0x00821190` and king updater `0x0084b690`. Do not confuse either with the kernel's country-daily event. |
| King updater `0x0084b690` | Country index at AI `+0x38`; approximately 15-day stagger or dirty byte `+0x1bd` selects `0x00851440`. Also updates child lists `+0x44/+0x214`, folds child values from `+0x13c` into `+0x1c8`, calls `0x00827f30`, and runs `0x0084ef30` eight times. |
| Target scan `0x0084b0f0` | Visits country indices including tags without provinces. Reads relation `country+0xbe8[other]`, especially `+0x34`; tests REB, country `+0x4a4`, capital continent `+0x16c` and relation `+0x3c < -50000`; appends through `0x004d61d0`. |

A wall-time quota can interleave queued commands and render/UI work differently
across machines. This is a reason not to replace scheduling or parallelize the
jobs, not an established desynchronization result. Upstream profiler timings
are workload-specific and are not Smedley performance baselines. In particular,
a tag without provinces need not be absent from a target scan just because it
cannot receive an ordinary queued country job.

## Agent identity, timers and requests

The [minister registry][registry] and [movement analysis][movement] propose a
subscriber → agent → peon → plan hierarchy. Common plan fields are timer
`+0x30`, context `+0x34`, country index `+0x38` and readiness byte `+0x3c`.
Common virtual entries include serialization `0x009a29f0/0x009c1a30`, tick
`0x009c1390`, initialization `0x00825cf0`, readiness `0x00825d40`, and countdown
update `0x00826480`. Slot numbers below are zero-based: slot 9 is `+0x24`,
slot 10 is `+0x28`. These shared offsets do not establish complete derived
layouts or uniform slot-11 behavior.

| Proposed class | Vtable VA | Execution anchor VA |
| --- | --- | --- |
| `CAIArmy` | `0x00e26374` | `0x00828030` |
| `CAIBudgetMinister` | `0x00e2657c` | `0x0082e970` |
| `CAIForeignMinister` | `0x00e274fc` | `0x00833930` |
| `CAIKing` | `0x00e27714` | Slot 9 `0x0084b690`; slot 10 `0x0084b0f0` |
| `CAIAdmiral` | `0x00e27924` | `0x00821dc0` |
| `CAINationalFocus` | `0x00e27ccc` | `0x00853fc0` |
| `CAIPoliticsMinister` | `0x00e282d4` | `0x00855980` |
| `CAIProductionMinister` | `0x00e284dc` | `0x008569a0` |
| `CAIResearch` | `0x00e286c4` | `0x00859610` |
| `CAIMilitaryPlan` | `0x00e27eb4` | Slot 10 points to stub `0x004010c0`, not `0x008555f0` |
| `CAINavalBlockadePlan` | `0x00e280c4` | Slot 10 points to `0x00855780` |

The registry's tentative MilitaryPlan assignment disagrees with the later naval
section. Local reads of cells `0x00e27edc` and `0x00e280ec` independently confirm
the two target words above (byte-checked pointer values only). This does not
verify the behaviors implied by class/method names.
Likewise, the old supposed fleet table `0x00e26178` is rejected by the movement
notes as string data; a `CAIFleetRequest` is not proof of a `CAIFleet` agent.

The request-table sequence is a useful RTTI search region: value `0x00e27b50`,
money `0x00e27b74`, manpower `0x00e27b98`, diplomat `0x00e27bbc`, merchant
`0x00e27be0`, army `0x00e27c04`, fleet `0x00e27c28`, barrack `0x00e27c4c`,
port `0x00e27c70`. These are request objects, not native command interfaces.

Two other registry leads must remain narrow:

- Strategy table start is reported as `0x00e27b18`, not the preceding locator
  word at `0x00e27b14`. Dispatch `0x0085ae90` handles consideration IDs such as
  `0x720/0x336/0xcc/0x134/0x2a1`; evaluator `0x0085b460` logs demand/supply,
  with an outcome at object `+0x150`. Exact gameplay meanings remain unknown.
- MTTH evaluator table `0x00e2cecc` leads to `0x008aebd0` and `0x008ae8a0`;
  ID `0x3be` selects a `+0x1c` subobject. An espionage type-name/getter at
  `0x00c59e20`, global `0x013f3624`, is reported without ordinary callers or
  an instantiated minister table. This supports a dormant-system lead, not
  proof that no indirect espionage-related code can execute.

## Army and naval decisions have distinct evaluation and commit stages

The [military notes][movement] separate movement from threat checks:

| Anchor VA | Proposed mechanism / limit |
| --- | --- |
| `0x00828030` | Calls target evaluation `0x0082d0f0`, then movement `0x00828060`. |
| `0x00828060` | Filters managed-list `+0xac` through `0x005cc3e0`; gets list head through `0x0082da70`; tests connectivity through `0x0051b200`; reaches walker `0x00828320` and later `0x008290f0/0x00828550/0x00829ab0`. Not a small isolated move-order function. |
| `0x0082da70` | Returns the first managed-list item when count `+0xb4 > 0`; no ranking or best-stack selection is implied. |
| `0x0051b200` → `0x0048f6e0` | Area `province+0xe8` shortcut and reachability search; negative transit costs reportedly reject traversal. Scratch vectors do not establish a returned path. |
| `0x0082da90` | Busy fields `army+0x1b4/+0x74/+0x70/+0x104`, local hostile strength and neighboring provinces can produce true. Neighbor records reportedly occupy `0x24`; type 4 is skipped. True is not simply “safe to move.” |
| `0x0082c1d0` → `0x0082d340` | Threat-gated commit path, separate from movement's connectivity loop. Commit rejects an already-sieging army with the same target. |
| `0x005dc8b0` | Scans province army list `+0xec`, ownership, relation state and REB cases; accumulates army `+0x40`. “Strength” here must not be assumed to be soldier headcount. |
| `0x005cc3e0` | Shared order-eligibility predicate with army in EAX according to upstream; reads relation `+0x70`. Its writer and exact semantics remain unidentified; “actionable” is not a durable field name. |
| `0x0084b0f0` → `0x0084dfc0` | Country-owned king/mobile-force path includes REB suppression and own-army list `+0x214`, with commit `0x0082c4d0`. It is not generally a rebel-side controller. |
| `0x00821dc0` | Admiral uses list `+0x68`, unlike army `+0xac`, with shared eligibility checks and timer paths including 30. List element identity remains unresolved. |
| `0x00855780` | Blockade-plan worker uses army-like list/count offsets; subordinate virtual targets remain unproven. |

Relation `+0x34` is reported to receive values `0,1,4,5,6` from a command setter
near `0x005bae94`; treat it as an enum candidate, not an at-war Boolean. Neither
that enum nor relation `+0x70` has a complete name/value/lifecycle contract.

The military define loader around `0x00430450`, lookup `0x0042e110`, offers
configuration anchors: cavalry/support/special proportions at define-object
`+0xa8/+0xac/+0xb0`, escort ratio `+0xb4`, army tax-base fraction `+0xb8`,
blockade range `+0xc0`. Navy tax-base `+0xbc` is a weaker upstream lead. The
loader interleaves lookups and stores; the nearest following store can belong
to the preceding lookup. No new composition or recruitment policy is enabled.

[PMIA candidates](ai-static-leads.md#candidate-functions) overlap recruitment,
construction, RNG and sphere work. For example its sphere RVAs
`0x004363f0/0x00437330` correspond to VAs `0x008363f0/0x00837330` below. Its
construction RVAs `0x004582d0/0x00458460/0x00458560` correspond to production
phase VAs `0x008582d0/0x00858460/0x00858560`. Address agreement is corroboration
of a lead, not independent validation of every role. PMIA's “colonial planner”
for VA `0x0084d1d0` and the function map's peace-composer description remain an
unresolved naming conflict. The existing daily event owns RVA `0x001085ae`;
these documents do not authorize a second patch there.

## Diplomacy scores are not universally probabilities

The [relation analysis][relations] distinguishes opinion core `0x0083ec10`,
raw-score wrapper `0x00841640`, and early-out wrapper `0x008416b0` that otherwise
returns `50 - score`. Core output uses an accumulator pointer and optional
reason text; a tooltip string does not establish a quantitative rule.

The source's later instruction-level account is more useful than its initial
22-block interpretation. Candidate hard assignments include ±1000 for
subordination/sphere conditions, held-core counts and negative relations.
Later reason anchors distinguish `HELD_CORES` at `0x0083f657`,
`NEGATIVE_RELATIONS` at `0x0083f7c9`, GP-alliance limits around
`0x0083fb3f/0x0083fc9f`, and a +50 desire-for-GP-ally path at `0x0083fe32`.
Do not retain the earlier blanket “colonial-rank gate” label for those blocks.

Cumulative leads include base scorer `0x00834150`, pair-derived factor
`0x004fb6a0` (not established as fresh RNG), +20 additions, military-ratio
terms and war adjustments through `0x00832dd0`. The later account assigns
**−100 in both casus-belli directions**, not −100/−10; the war sum is added
only when negative, not unconditionally. Exact condition names and complete
formula remain unverified. Base scorer sphere terms are reported as +25/−25,
not a universal +1000 sphere preference. Threshold globals
`0x01317d3c/0x01317c5c/0x01317d08/0x01317d28/0x01317d84` are runtime-populated
storage; file zeros or absent raw bytes cannot supply their active values.

The command scorer `0x0083db20` dispatches on a virtual action ID at `+0x20`.
The source finds related switches in `0x006153a0`, `0x00820df0`,
`0x00942540`, `0x00947210`, `0x00949c20`. Similar index tables do not establish
identical surrounding UI behavior. The final flag can threshold a score at 50
into 0/100; the proposal worker `0x00820df0` instead reportedly rolls against
acceptance and changes request state from pending 1 to rejected 2/accepted 3.
Calling an evaluator need not be observational if it consumes RNG or builds
state.

| Action-ID leads | Scoring target VAs / distinction |
| --- | --- |
| Alliance `0x332`; cancel alliance `0x8ca` | `0x00841640`; `0x008416b0` |
| Ask/cancel-ask access `0x8c6/0x8c7` | `0x008419c0`; `0x00842a00` (reported 0/100 list-membership result) |
| Give/cancel-give access `0x8c8/0x8c9` | `0x00842a50`; `0x008430d0` (reported `50 - ask-access score`) |
| Call ally `0x8cb`; increase/decrease relation `0x8d5/0x8d6` | `0x00841900`; `0x00844050/0x00844140` |
| Peace action `0x81e`; declare war `0x38d`; subsidies `0x470` | `0x0083de70`; acceptance shortcut; inline amount/state checks |
| Add/remove sphere `0x897/0x898` | `0x00911760` Boolean-style gate, not opinion core |
| Crisis offer `0x9fa`; back side `0x9fb`; back down `0xa02` | `0x00843a10`; `0x00843b90`; `0x00843170`. Do not infer class identity from an evaluator's provisional “peace” name. |

Command factory entry `0x0090e850`, not mid-function `0x0090e880`, also handles
IDs absent from that scorer: opinion changes `0x8b0/0x8b1`, cancel subsidies
`0x8c1`, discredit/expel/ban `0x8c2/0x8c4/0x8d4`, add wargoal `0x8dd`,
gunboat `0x8e4`, fabricate CB `0x931`. This is not permission to construct or
queue those foreign objects.

### Crisis, influence and the separate acceptance gates

The [crisis/sphere analysis][crisis] places foreign-minister dispatch at
`0x00833930`, stagger helper `0x0083de00`, broad strategy `0x00833a40`, crisis
pass `0x0083b3d0`, sphere pass `0x008363f0` and hostile action selection
`0x00837330`.

Crisis evaluator `0x00838eb0` has these useful, qualified arithmetic leads:

- A count penalty at `0x008396d8` is −30 per positive count, not a rule that
  starts only above two wargoals. A match bonus at `0x00839f0a` is +100.
- The two 1000-seeded calls at `0x00839eb0/0x0083a680` go through base scorer
  `0x00834150`; they are not proof of unconditional +1000 final bonuses.
- The early-crisis term around `0x0083abb0` uses signed `2 * (x / 1000) - 50`
  and applies a negative result. The implied boundary is roughly raw 25000,
  not temperature 50. The input child object's `+0x10` is only temperature-like:
  initializers `0x0094bd40/0x0094bdb0` can put the `---` placeholder there, and
  the live-value writer is unresolved.
- An alleged +1000 sphere term and generic infamy subtraction are not
  established inside this evaluator. The upstream overview diagram is not a
  supported formula or proof of crisis escalation behavior.

Sphere counter-action constructors are discredit `0x008327b0`, expel
`0x008328d0`, ban `0x008329e0`, decrease opinion `0x00832b00`; candidate callsites
are `0x00837da5/0x00837c4c/0x00837b50/0x00837efd`. Reported influence costs and
40/65-point action triggers lack a full predicate/defines contract here and
must not become hardcoded policy.

Evaluator `0x00843170` ends at `0x00843a01`; `0x00843a10` and `0x00843b90`
are separate functions. The source's later `0x00843b90` account describes a
score-dependent random 0/100 result, not a deterministic warscore-plus-exhaustion
formula. UI gate `0x009476d3` compares that result with 50 (`<50` rejects).
The `<=50` branch at `0x008440d0`, inside `0x00844050`, is another gate, not
part of `0x00843170`. These distinctions preclude treating one “peace threshold”
as the complete acceptance algorithm.

## Budget, production and politics: planners, not fiscal contracts

[Budget research][budget] identifies timer reset of 1–3 at `0x0082e970`, then
tax/tariff evaluation `0x00831a60` and spending/debt evaluation `0x0082f430`.
Treasury `+0xe78/+0xe7c` agrees with the narrow local fiscal mapping. The raw
comparison constant `0x01f40000` is **32768000**, or 1000 at /32768 scale;
the source's decimal conversion and older “32000” gloss are wrong. A positive
high-dword shortcut is also reported, so do not reduce the branch to an
unsigned low-word comparison for all signed values.

The source interprets later branches as tax/tariff changes constrained by party
policy at country `+0x364`. It identifies debt-related snapshots
`+0x1448/+0x1450/+0x1458`, country-unit iteration `+0x7b4`, scaling/clamping
and order allocation. It explicitly withdraws its former slider cut-order,
1000-baseline and synchronous-bankruptcy stories: there are reportedly no
direct country-field stores in the reviewed `0x0082f430` body. Its constants
and comparisons do not replace [native credit/default evidence](../evidence/fiscal-credit-boundaries.md#loan-lifecycle).
The double at `0x00e45c28` is referenced here as well as by market pricing;
a global constant need not have one domain-specific meaning.

[Production and politics leads][registry] offer these narrower entry points:

| Planner | Useful anchors and unresolved interpretation |
| --- | --- |
| Production `0x008569a0` | Cached gate `+0x68`, flags `+0x6c..+0x74`, slots `+0x78/+0x7c/+0x80`; phases `0x00858560/0x00858460/0x00858670/0x008582d0/0x008589b0/0x00859180`. Candidate enumerators `0x00857430/0x00857530`, scorer `0x008571a0`, setups `0x008577b0/0x00857e60`, shared tail `0x004da790`. Slot pairing is stronger than exact building-type names. |
| Politics `0x00855980` | Cache `+0x64`, brain `0x00855a90`, civilization-dependent subobjects country `+0x850/+0x860`; phase `0x00855ed0`, count pair `0x00542bc0/0x00543470`, enactor `0x00855d10`, main pass `0x00856470`. Order constructors `0x00591be0/0x005910d0/0x00588bb0/0x0058d450` are search anchors, not proven reform APIs. |
| Shared RNG/container support | `0x009b7610/0x009b7700` draw/refill candidates; `0x004dad40/0x004dada0` lazy registry and 0x1ff-capacity setup candidates. Capacities and RNG use do not establish safe reentrancy or permission to reorder phases. |

The registry describes date/cache gates using world `+0xb0c`, constants and
month tables `0x00f1027c/0x00f10280`. “Year”, “month” and “day-change” labels
are not consistently distinguished across its summaries. Retain the branch and
cache offsets; do not promise an exact planner cadence beyond independently
established date units in [telemetry](../evidence/telemetry.md#dates-and-world-snapshots).

## National-focus scoring is per candidate and order-sensitive

The [focus analysis][focus] identifies province scorer `0x00564770`, with
province in EDI, stack focus definition, output-score pointer and country-pair
context. The out-parameter, not EAX, is the meaningful result. Definition
anchors are rail component `+0x54`, eligibility field `+0x5c`, promotion vector
`+0x7c/+0x80`, and string-related `+0xf4`. Province anchors include state
`+0x188`, population `+0x1a8` and clergy count `+0x1bc`.

| Branch VA | Proposed score mechanism / important limits |
| --- | --- |
| Population factor `0x0056485c..0x00564908` | Converts population through scale/ceiling into a factor subtracted from 1000; large populations can make it negative. Exact constants must be rechecked before using a formula. |
| Rail branch before `0x00564c44` | Rejects colonial/nonindustrial states; starts from 2000 plus 1000 per integer pair of factories, then requires a usable province in state `+0x48`. Candidate rejection magnitude is −100000. |
| Soldier `0x00564d1b` | Uses country arrays `+0x1338/+0x12e8`, `SOLDIER_FRACTION/WEIGHT`, population factor, and country field `+0xe40` cases 2/5. Field names and exact ratio denominators need review. |
| Administrator `0x00564ec3` | Population share, country metric `0x00532ea0`, `ADMINISTRATOR_WEIGHT`; reports tenfold adjustments and a capital-ID comparison. Not a complete administrative-efficiency formula. |
| Educator `0x00565106` | Shortfall from `MAX_CLERGY_FOR_LITERACY`, weighted by `EDUCATOR_WEIGHT` and population factor. |
| Industry worker `0x00565254` | Scans state factories, level/closed checks, employee-type records and worker shortages; combines shortage with the incoming score before population scaling. |
| Capitalist `0x00565450` | `CAPITALIST_FRACTION`, population factor, civilization and factory/capital checks; rejects candidates with large negative penalties. The source's “plurality-like province value” is not adopted. |

Type-dispatch candidates are promoted-type flags `+0x42` soldier, `+0x3db`
administrator, `+0x41` educator, `+0x50` worker, `+0x3da` capitalist. A focus
with several entries is not evaluated by a generic sum or maximum: branches
can replace the accumulator, worker logic can reuse it, and colony penalties
can repeat. Entry order can therefore matter.

Logs near `0x00564b13/0x0056579f` divide by 1000 for presentation. That is not
proof of a per-focus weight. The `NATIONAL_FOCUS_DIVIDER` loader at
`0x00434eca..0x00434eeb`, destination define-object `+0x108`, is a separate
lead with no established consumer. AI focus execution also has scheduler
`0x00853fc0` and helpers `0x00854620/0x00854920`; do not confuse these with the
province score routine.

## Technology separates display, availability, actions and AI selection

The [technology analysis][technology] concerns independent gates, not one
universal research lock. No branch changes, research unlocks or experiments
from that document are imported.

| Anchor VA | Proposed role / field anchors |
| --- | --- |
| `0x00569920` | AL availability result; technology ID `+0x2e4`, prerequisite object `+0x2ec`, date requirement `+0x340`, state vector at research object `+0x2b8`. Not itself a direct civilization check. |
| `0x005699b0`, `0x005467e0` | Blocking-reason and technology-text construction. Text/display is not permission to start research. |
| `0x007a9700`, `0x007a9950` | Technology-list and entry-state paths, also callers of availability. Changing a visual state does not establish action eligibility. |
| `0x007aa580` | UI state path; civilization branch at `0x007aa751`. |
| `0x007aa890` | Action processing including start; selected technology `view+0x5c`, country research object `+0xbcc`, state vector `+0x2b8`; civilization branch `0x007aaa83`; availability call `0x007aab6f`. |
| `0x00859610` → `0x00859710` | AI scheduler and selection brain. Timer/cache `+0x30/+0x68`, current-selection gate country `+0x4cc`; independent civilization read/branch `0x0085974d..0x00859755`. |
| `0x005379e0` | Classification uses country `+0x12d0/+0x31/+0x1404`; proposed outputs 0 GP, 1 secondary, 2 civilized, 3 uncivilized. Classification is not demonstrated to be the direct research gate. |

The [registry][registry] adds candidate selection machinery inside
`0x00859710`: availability filter `0x00569920`, scoring/fixed-point helpers,
weighted random choice and constructor `0x0058ad00` for a `0x48` allocation.
Technology notes call the latter “system initialization”; retain the narrower
order-construction lead rather than assuming global initialization. The brain
ends at `0x00859d24`; list operations beginning `0x00859d30` and later
`0x00859ef0` must not be folded into its body. Availability's reported callers
include both UI and AI, so changing it could affect more than the intended gate.

## Additional AI leads and limits

The upstream [function map][function-map] supplies further proposed roles:

| Area | Mechanism anchors |
| --- | --- |
| Army | Force estimators `0x00820370/0x008205c0/0x00820760`; candidate materialization `0x00823890/0x00823e70`; target selection `0x008290f0`; coordination `0x00829ab0`; request builders `0x0082dc50/0x0082e190`. |
| King/peace | Peace-plan builder `0x0084a5c0`, committer `0x0084aca0`, latches `+0x238..+0x23b`, sorted/search helpers `0x008486e0..0x00849c20`, force-context refresh `0x00851440`. |
| Ministers | Scoring tables `0x00852fc0/0x008534f0`, focus/politics/production/research workers, strategy dispatcher `0x0085ae90`. |

The map also includes generic container, arithmetic and trampoline routines;
address-region membership does not make them AI-specific. The [JSON
inventory][inventory] lists coordinates and entry bytes, not function roles or
native calling contracts. Neither source establishes exhaustive discovery or
reliable full-body bounds.

In the [supported executable](../v2game-3.04.toml), table cells match the targets
discussed above, including king slot 9/10, budget slot 10 and research slot 10.
Those matches do not validate full-body reconstructions, profiler attribution,
field identities, ownership, thread requirements, RNG consequences or safe
mutation. All new behavioral leads remain `historical-unverified`.

[upstream]: https://github.com/maxioten/Victoria2-Reverse-Engineering/tree/e299a0214d32274239402e8ee22039e1d02e101f
[jobs]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/AI/Ai_daily_jobs.md
[registry]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/AI/Ai_ministers_registry.md
[movement]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/AI/Ai_military_movement.md
[relations]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/AI/Ai_relations.md
[crisis]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/AI/Ai_diplomacy_crisis_soi.md
[budget]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/AI/Ai_budget_economy.md
[focus]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/AI/Ai_national_focus.md
[technology]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Technology/technology.md
[function-map]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/AI/Ai_function_map.md
[inventory]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/AI/Ai_function_inventory.json
