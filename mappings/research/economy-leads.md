# Economy processing, goods and construction: historical leads

This original factual synthesis uses
[maxioten/Victoria2-Reverse-Engineering][upstream] at commit
`e299a0214d32274239402e8ee22039e1d02e101f`. The pinned checkout has no license
file. Only factual investigation leads are retained, not upstream prose,
diagrams, code, mirrored type declarations or patch recipes.

**Evidence level: `historical-unverified` for every new semantic claim.**
Upstream's own verification labels and profiler results are not adopted as
Smedley evidence. Existing stronger mappings retain their individual levels.
The small local static checks below confirm constants/instructions, not general
behavior, complete layouts or safe native calls.

Code/data addresses are preferred-base **VAs** at image base `0x00400000`;
subtract the base for an RVA. Object offsets require the named object's actual
base, not an arbitrary interior/serialization pointer. The
[mapping guide](../README.md) owns executable identity, ASLR and evidence rules.

## Scope and exclusions

Coverage includes all four `Economy/*.md` documents and the four text files in
`CheatEngineTableVersion`: internal data storage, type definitions, employee
life-needs calculation and POP project investment. The `.CT` table, scripts,
patch implementations, other Engine documents and wholesale mirror layouts are
excluded. This record retains mechanisms and selected field/address anchors,
not a duplicate of upstream's raw mapping inventories.
[AI/technology leads](ai-and-technology.md) cover economic planners and research
selection; [PMIA leads](ai-static-leads.md) retain separate plugin provenance.

No price-range changes, civilization/colonial unlocks, construction bypasses,
interest changes, multithreading replacements, RNG changes or new runtime
capabilities are enabled. An address match cannot authorize any of them.
Unidentified slots remain unidentified; unreadable state is unavailable, not
zero. A guessed field in an old mirror is not a supported layout contract.

## Conflicts and authoritative local boundaries

| Upstream lead or conflict | Smedley interpretation / decision |
| --- | --- |
| `0x006859c0` described as monthly economy work | The source's later daily-path correction identifies a daily tick; discard its stale monthly name. Local [date evidence](../evidence/telemetry.md#dates-and-world-snapshots) establishes 24 raw units/day and a 365-day calendar, not the source's speculative leap-year interpretation. |
| `0x0068bf00` treated as repeated gameplay economy work | Later source analysis places it in load finalization. Do not call its five passes the daily market algorithm. |
| VA `0x00523400` called an interest calculator | [Fiscal evidence](../evidence/fiscal-credit-boundaries.md#origination) identifies loan origination with principal/treasury effects. A rate calculation in that path does not make it an observation-only interest helper. |
| Country `+0xe4c` called an AI score; world `+0xb3c` called special modifiers | Respect state-list count and world-war storage respectively; see [AI conflict boundaries](ai-and-technology.md#scope-and-evidence-precedence). Neither label should enter economy interpretation. |
| POP size `0x2a8` in old storage notes | [Interest evidence](../evidence/interest-payout.md#verified-traversal-and-mutation-boundaries) establishes payload/value-copy extent `0x278`, separately allocated node `0x288`, next `+0x27c`. **No contiguous POP stride of `0x2a8` or `0x288`.** |
| Factory linked-next at `+0x224` treated as part of the building | Local payload is `0x220`; node allocation is `0x230`, with previous/next/marker `+0x220/+0x224/+0x228`. A traversal link is not an owned field in a copyable factory value. |
| Factory `leftover` at `+0x200`, maintenance fraction `+0x208` in storage notes | Local [factory sales evidence](../evidence/telemetry.md#finance-inventory-and-merges) identifies closing unsold output at **`+0x1f8`**. Do not replace it with `+0x200`, or adopt the neighboring maintenance label without tracing its base and consumers. |
| Factory injection/project fields | The source storage notes actually agree on injection money `+0x210` and subsidy `+0x1d8`, leaving `+0x1d0` unknown. Local [serialization evidence](../evidence/telemetry.md#injection-fields-and-node-extent) identifies project pointer `+0x1d0`, subsidy amount `+0x1d8`, injection money `+0x210`, days `+0x218`. Do not assign injection to the project pointer. |
| World-market mirror names `actual_sold_world` at both `+0xb8` and `+0x4f4` | Local [world-market evidence](../evidence/telemetry.md#world-market) supports `actual_sold +0x434` and `actual_sold_world +0x4f4`. `+0xb8` remains an unadopted lead, not an alias. |
| Broad /32768 money labels or “percent sold” fractions | Scale is field-specific. Factory finance and artisan/RGO income use /32768000; injection and treasury use /32768. Artisan clearing values can exceed 32768; a fraction-like name does not imply a [0,1] bound or producer-attributed export share. |

### Factory disagreement is partial, not a uniform layout shift

The [storage notes][storage] agree with many local complete-object offsets:
definition `+0x18`, level `+0x20`, stock pool `+0x28`, requested pool `+0x80`,
output `+0xd8`, employment records `+0xf0`, employees `+0x128`, finance
`+0x150..+0x170`, subsidy/closed flags `+0x180/+0x188`, and injection `+0x210`.
They leave `+0x1f8` unknown but name `+0x200` as leftover. Moving the entire
layout by eight bytes would break the agreeing fields; there is no basis for
that repair.

There is a real **interior-pointer** distinction in the serializer: the existing
Smedley evidence places serialization subobject at complete factory `+8`.
Local disassembly of writer VA `0x004f3ef0` independently confirms reads at
`0x004f4346/0x004f434c` from serializer-relative `+0x20c/+0x208`, and at
`0x004f4394` from `+0x210`. Those displacements are consistent with complete
injection `+0x210/+0x218`; they do not relocate leftover to `+0x200`. The
displacements are locally byte-checked only; field names/base identity remain
owned by the existing serialization record, not newly inferred from these
instructions alone.

Likewise, factory-construction and focus notes following `+0x224` are compatible
with a list node surrounding a `0x220` value; they do not establish a larger
factory payload. Smedley's allocator/value-copy evidence resolves that extent.
The exact upstream origin of the `+0x200` label remains unknown, so that label
and the adjacent `+0x208` interpretation are excluded rather than silently
“corrected” into a new supported mirror.

## Daily market work differs from load reconstruction

The later [daily-path account][daily] supersedes stale names in the
[larger market document][market]. Proposed daily chain:

| Anchor VA | Mechanism and boundary |
| --- | --- |
| `0x00683ab1` → `0x006859c0` | World advance reaches daily tick. Tick callsite `0x00685f7e` reaches economy update `0x00489990`. |
| `0x00489990` | Coordinates world-market pass `0x00484060`, distribution `0x004808d0` and postprocess `0x00488080`. Exact phase semantics remain unverified here. |
| `0x00484060` | Country/state/factory traversal reaches factory market worker `0x00482ff0`; per-country demand worker `0x00487410`, then country POP worker `0x00485e40` at callsite `0x0048468f`. |
| `0x00484309` → throttle | Source reports a render opportunity inside market work; bounded callbacks must not assume the whole pass is an uninterrupted observation phase. Throttle candidate VA `0x00685620`. |
| `0x00520150` | Economic-distribution/statistics work has load/UI/diplomacy/event callers as well as reported daily invocation through statistics functor `0x0068f4c0`. It is not exclusively the goods-settlement worker. |
| `0x0066321d` → `0x0068bf00` | Reported load-finalization reconstruction. Includes `0x00520150`, list rebuild `0x0068d250`, supply `0x00482930`, distribution `0x004808d0`, preparation `0x0068d950`, five `0x00489990` passes with arguments 0–4, entries pass `0x0068dc70`. Meaning of those five arguments is unresolved. |

The source's load/daily distinction is a useful static lead, not a new runtime
acceptance claim. Its TGC-era timing samples are not portable benchmarks and
are deliberately not imported as expected Smedley timings.

### POP traversal and shared work constrain concurrency

The [daily account][daily] describes country province-index vector
`+0x9d8/+0x9dc`, world province table `+0xacc`, market POP-type vector
`+0x804/+0x808`, and per-province typed list headers at `+0x194`, 16 bytes each.
The next POP is read through node `+0x27c`; this is pointer traversal, not adding
a constant to reach a neighboring value. This agrees with the narrower local
POP traversal contract without promoting the surrounding objects.

Country POP worker `0x00485e40` reads size `+0x58`, type `+0x68`, other economic
state and artisan pointer `+0x1d4`; artisan requirement builder `0x004dd3c0`
and three needs-purchase calls through `0x00485390` are leads for life/everyday/
luxury work. Distribution calls `0x0095eb00` at `0x00481860`; daily province
work calls `0x004b9c60` at `0x00685b09`. None of those names proves a complete
POP cash-flow boundary.

Source-reported obstacles to naive parallelization include:

- Reused scratch storage around `0x013f24b0` and `0x013f24f4..0x013f25a8`.
- Per-state RNG chain `0x004a46e0` → `0x004a5170` → `0x009b7950`.
- Clamped goods accumulation and order-sensitive integer rounding.
- Distribution writes to candidate receiver fields `+0x8d8/+0x900` indexed by
  eight-byte elements and `+0x13e8/+0x13ec`; receiver identity still needs
  reconstruction. Helper `0x00969760` reportedly performs additional shared
  updates. Do not assign these offsets to a country merely from their values.

These are investigation constraints, not proof that the existing economy is
single-threaded or that private accumulators alone would preserve behavior.
The source also reports a native parallel statistics path; preserving RNG draw
order, lifecycle, callback authority and arithmetic ordering requires separate
analysis. No threading implementation is imported.

## Supply, real demand, price bounds and goods pools are different quantities

[Market research][market] places supply comparison at `0x00482930`, multiplier
helper at `0x00482610`, and price calculation in the `0x00482bxx` region. That
region notation is not an exact function entry. `0x00487410`, especially indexed
addition at `0x004877b1`, is described as a **real-demand** path, despite an
older supply-and-demand name. It must not be treated as production or fulfilled
consumption. Local [market evidence](../evidence/telemetry.md#world-market)
separately distinguishes supply, demand, realized demand and global sales.

The later correction identifies two scaled price-bound constants:

| Constant VA | Locally read double | Qualified interpretation |
| --- | ---: | --- |
| `0x00e45c30` | `6554.10009765625` | Candidate minimum factor: integer truncation to 6554, then /32768, approximately 0.2 of base price. |
| `0x00e45c28` | `163840.5` | Candidate maximum factor: integer truncation to 163840, then /32768, factor 5. |

The double values were independently read from the supported PE; that confirms
stored values, not the full price algorithm. Source arithmetic anchors include
load `0x00482c46`, conversion `0x00b31c36`, multiplication `0x00ac9f20` and
15-bit shift. The same maximum constant is referenced by budget AI; it is not
safe to assume a global-data edit affects only market prices. Proposed wider
bounds and previous patch experiments are explicitly excluded.

[Stockpile notes][stockpiles] and [type notes][types] suggest a compact goods
pool: per-good byte lookup at holder `+8`, int64 value vector begin/end/capacity
`+0x48/+0x4c/+0x50`, candidate total extent `0x58`. The 64 bytes between `+8`
and `+0x48` are lookup storage, not 64 inlined quantities; comments saying
“8 bytes of good indices” conflict with that extent. Global VA `0x012587f4`
is the loaded goods count. Actual registry length and valid lookup encodings
must be checked; neither a 64-byte table nor an observed 48-good registry
establishes a universal mod-independent goods bound.

| Helper VA | Proposed operation / limit |
| --- | --- |
| `0x0047dc20` | Add indexed pool values into a destination pool; instructions at `0x0047dc56/0x0047dc59` are accumulator anchors. Market meaning depends on caller. |
| `0x0047dca0` | Subtract source goods from destination: skip absent source entries; subtract existing destination values or create a negative entry. Do not assume it mutates the artisan/factory named in a caller's shorthand. |
| `0x0047e3e0` | Scaled dot product, used well beyond market work. Not a price-only function. |
| `0x0047de60`, `0x0047d9e0` | Range scaling and clamp candidates. |
| `0x004dd470` | Recipe/request scaling and clamp candidate, source reports 99999 cap. Local factory mapping establishes stock subtraction/request production, not fulfilled input purchases. |
| `0x0043a880`, `0x0054c600` | Generic vector-resize and sort candidates; do not expose them as economic services. |
| `0x00ac02a0`, `0x00ac9f20` | Signed 64-bit divide/multiply support. Their presence establishes arithmetic machinery, not units or category. |

The stronger [physical input boundaries](../evidence/telemetry.md#input-demand-versus-consumption)
remain authoritative: requested inputs are not purchases, and consumption
requires complete consecutive stock boundaries. Similarly artisan requirement
builder `0x004dd3c0` subtracts stock from a temporary requirement pool, not
necessarily from live artisan stock. Sales, inventories, deposits and claims
cannot be summed into a money-supply identity.

### Additional market and report storage remains unclassified

The old [world-market type mirror][types] offers search offsets outside the
narrow local mapping: per-country candidate pools `+0x110/+0x3f4/+0x404/+0x414/
+0x424/+0x4e4/+0x7d4/+0x814`, UI-demand candidate `+0x228`, common-market
current/prior pools `+0x724/+0x77c`, per-type pools `+0x714`, scalar vector
`+0x7f4`, and another pool vector `+0x824`. Pointer slots `+0x330/+0x334` and
pools `+0x344/+0x39c/+0x54c/+0x664/+0x6bc` have no adopted contract. This
incomplete declaration has gaps and duplicate semantic names; it is not a
reproducible C layout and no size or ownership conclusion is imported.

The [market document][market] also points to report-slot selector
`0x00f20bfc`, candidate `0x70`-byte report spacing and storage around
`0x012624a8..0x012624f4`, including `0x012624e8`. Alternating snapshots are a
hypothesis, not established double-buffer ownership. Source reads near country
`+0xe78..+0xe88` do not classify report categories as taxes, wages, gold,
interest or monetary aggregates. In particular local fiscal evidence leaves
country `+0xe80` unidentified.

## Construction has several independent permission layers

[Factory-construction research][construction] separates button availability,
actual action validation, state/type permission and a second-country predicate.
No bypasses or instruction replacements are imported.

| Function VA | Proposed role and anchors |
| --- | --- |
| `0x0052e960` | Button-level permission, AL result; country civilization `+0x12d0`, state colonial status `+0x84`, factory count `+0x68 < 8`, country permission fields including `+0xb20/+0xb60`, helper `0x0052fb40`. Eight is a predicate constant here, not a universal storage capacity. |
| `0x0052c9b0` | Immediate-construction gate combining `0x0052e960`, type/state gate `0x0052ca30`, and cost/funds candidate `0x004da720`. Button enablement alone does not establish action validity. |
| `0x0052ca30` | Independent civilization check, state factory list `+0x60`, definition/level accesses `+0x18/+0x20`, node-next `+0x224`, country research object `+0xbcc` then `+0x2fc`, state `+0x130`, general production predicate `0x004d04b0`. |
| `0x0052fb40` | Compares different countries: initiator `+0x31`, associated country `+0x31/+0x12d0/+0xb60` and related record `+0x34`. Exact identities/policy are unresolved; it is not simply another copy of the initiating country's civilization gate. |

The independent checks explain why a UI experiment cannot prove full factory
construction support. Existing local [upgrade evidence](../evidence/telemetry.md#established-fields-and-identity)
identifies project storage but leaves progress, materials and general
policy/ownership/funds/level-dependent availability unresolved. These source
leads do not close those gaps.

## Text-file leads beyond the existing economic mapping

The [storage notes][storage] are an offset notebook, not a checked object model.
Use the following only to choose a bounded investigation; fields already owned
by Smedley evidence are linked rather than promoted again.

| Object | Extra anchors / uncertainty |
| --- | --- |
| Goods definition | Base price `+0x30`, candidate /32768. Total object size remains unknown. |
| Country fiscal/policy | Sphere-market share `+0x138`; industrial/military scores `+0x198/+0x1a0`; suppression `+0x15a0`; colonial maintenance/points `+0x15c0/+0x15c4`. Tax-setting vector `+0xa0/+0xa4/+0xa8`; setting owner `+0xc`, value `+0x10`. Spending pointers proposed at `+0x1234/+0x1238/+0x123c`, with education/admin/social/military entries. Identity, duplicate-pointer roles and lifecycle are unverified. |
| State | Owner ordinal candidate `+0x5c`, colonial status `+0x84`, population-by-type vector `+0x118/+0x11c/+0x120`. Notebook's `+0x258` unknown is superseded by local savings evidence, not renamed by this import. |
| Province/template | Population/employment/strata counters `+0x1a8..+0x1c8`; template pointer `+0x5c`, RGO-size candidate template `+0x2c`, name storage `+0xe4` with metadata `+0xf4/+0xf8`. Complete province extent and adjacent counters are not established here; local `+0x1ac` capacity evidence overrides “population copy.” |
| Production type | Input pool `+0x24`, output good `+0x80`, base output `+0x88`, bonus vector `+0xd8/+0xdc/+0xe0`, owner employment descriptor `+0xe8`, departments vector `+0x108/+0x10c/+0x110`, desired-worker vector `+0x118/+0x11c/+0x120`, workforce `+0x128`. Bonus/effect activation and workforce units remain unverified. |
| Employment descriptor | Candidate size `0x20`; POP type `+8`, multiplier `+0x10`, desired amount `+0x18`, effect selector `+0x1c`. Proposed selector 0 input, 1 output, 2 throughput needs caller review. Distinct from 16-byte `CPopEmployment`. |
| Building definition | Owner descriptor `+0x40`, construction-goods pool `+0x60`, construction time `+0xb8`, production type `+0x12c`. String metadata is borrowed foreign state, not an owned native string. |
| POP type | ID `+0x28`, sprite `+0x38`, strata `+0x3c` (0/1/2 poor/middle/rich), leadership `+0x48`, research contribution/optimum `+0x60/+0x68`, MTTH-like subobjects `+0x90/+0xac`, life-needs price `+0x320`, slave byte `+0x3d9`. |
| POP | Demographic-delta candidates `+0x158..+0x178`, previous size `+0x5c`, accepted-culture candidate `+0x4c`, saved-random candidate `+0x258`. Names do not establish causal migration/promotion/assimilation accounting or durable identity. Local [POP evidence](../evidence/telemetry.md#pop-snapshots-identity-and-needs) owns measured identity and needs limits. |

The notebook also gives regiment/ship/army offsets, but these are outside this
economy import and are not adopted as military layouts. Repeated integer 397,
vtable-like pointers, “always null” observations, broad modifier scale /100000,
unknown padding and inferred full sizes are excluded as general contracts.
RGO `0xb0` indexing, artisan fields, factory finance, creditor/bank fields and
POP cash-flow indices already have narrower local records:
[telemetry](../evidence/telemetry.md),
[fiscal/credit](../evidence/fiscal-credit-boundaries.md),
[interest](../evidence/interest-payout.md). In particular, interest pooling does
not imply a proven native state-to-POP consumer; the existing narrow negative
evidence must not be weakened into either “always pays” or “never pays.”

### Employee life-needs calculation is not actual payroll

The [employee calculation listing][employee] starts at VA `0x004ef1b0`.
Candidate receiver is a `CStateEmployment` **subobject**, with employment vector
begin/end `+0x10/+0x14`, 16-byte records, POP pointer `+8`, employed count `+0xc`.
It skips POP types with slave byte `+0x3d9`, reads type life-needs price
`+0x320/+0x324`, and sums scaled employed-count × type-price products into an
ESI output accumulator. Arithmetic anchors are division at `0x004ef202`,
multiplication `0x004ef217` and accumulation `0x004ef223/0x004ef225`.

The count normalization includes a 30-bit shift and denominator `0xc3500000`,
followed by a 15-bit product shift. Preserve those truncation boundaries when
reviewing units; do not simplify the listing into a floating payroll formula.
This calculation is a cost estimate, not proof of paid wages, minimum-wage
policy, cash transfer, or all employee classes' treatment. The same relative
employment offsets occur at different complete-object positions in factories
and RGO records.

### POP project investment combines cash and a conditional savings path

The [investment listing][investment] has entry VA `0x00485d50`; the heading's
`0x00485d53` is an interior floating-point load, not the entry. It takes POP in
EAX according to the source, zeroes a returned-amount out-parameter, tests money
`+0x180/+0x184`, multiplies money by an argument coefficient and shifts 15 bits.
A negated amount is passed with cash-flow index 5 before the amount is added to
the result. That is consistent with the locally identified projects slot, not
a new mapping of every project-funding path.

A later comparison reads savings `+0x250/+0x254` against globals
`0x0125b9f0/0x0125b9f4`. On the selected branch it sets a caller byte flag,
resolves POP province `+0x64` then state `+0x188`, calls an unnamed bank helper
at `0x00485e24` and adds that helper's result. The target helper's identity,
withdrawal amount, reserve effects and full conservation contract are not
established by this excerpt. The threshold double at `0x00e45a70` is described
approximately in the source; its comment does not prove the comparison's
currency unit. Local savings' 1000:1 relation to state scale remains the
[authoritative unit boundary](../evidence/interest-payout.md#state-pool--pop-missing-native-consumer-and-verified-scale).
Decompiler-generated argument types and relocated/discontinuous branch text
in both listings are not copied as native signatures or executable code.

## Thread and accounting leads do not justify a replacement implementation

The [market notes][market] identify a generic worker shell, not a validated
parallel economy entry: creator `0x00a7aed0`, thread entry `0x00a7b0c0`, wrapper
`0x00a7b090`, cleanup candidate `0x00a7ae80`, table `0x00e439f8`.
The wrapper compares thread ID with object `+0xc`, then dispatches through the
object at `+0x18`, virtual offset `+4`. Handle `+8` and control `+0x1c` are
leads only. The expensive worker body and its ownership remain unidentified;
finding `CreateThread` is not proof that market work may be moved to it.

Similarly, report arrays and shared accumulators do not create a transaction
ledger. [Fiscal evidence](../evidence/fiscal-credit-boundaries.md#conservation-and-residuals)
owns receipt/loan/principal identities and residual limits. Taxes, trade,
stockpiles, savings claims, bank interest and POP transfers use different
boundaries and sometimes different scales. No comprehensive money supply,
default write-off, GDP or producer-attributed trade identity is established by
these imported leads.

## Instruction-level evidence and limits

In the [supported executable](../v2game-3.04.toml), the two price constants and
serializer displacements match the values noted above. The factory settlement
slice at VAs `0x00483689..0x004836aa` loads complete factory `+0x28` and calls
pool-add `0x0047dc20` at `0x0048369d/0x004836aa`, agreeing with the existing local
delivery record. These are bounded corroborations of constants/instructions,
not runtime verification or wholesale approval of upstream layouts. All
unresolved source behaviors remain `historical-unverified`; existing local
mapping levels and negative-evidence limits are unchanged.

[upstream]: https://github.com/maxioten/Victoria2-Reverse-Engineering/tree/e299a0214d32274239402e8ee22039e1d02e101f
[daily]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Economy/economy-daily-path.md
[market]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Economy/economy-market.md
[construction]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Economy/factory-construction.md
[stockpiles]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Economy/stockpiles.md
[storage]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/CheatEngineTableVersion/internal%20data%20storage.txt
[types]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/CheatEngineTableVersion/Type%20definitions.txt
[employee]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/CheatEngineTableVersion/calculate_total_life_needs_price_for_all_employees.txt
[investment]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/CheatEngineTableVersion/pop_invest_in_projects.txt
