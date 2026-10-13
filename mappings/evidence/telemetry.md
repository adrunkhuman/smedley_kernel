# Telemetry mapping evidence

This document records native fields, units, call boundaries and limits for the
English Windows HoD 3.04 executable specified in [AGENTS.md](../../AGENTS.md).
Addresses are **RVAs** unless explicitly labelled VA. Expected bytes and field
provenance belong to the [catalog](../v2game-3.04.toml); evidence levels are
defined in the [mapping guide](../README.md). Current capture and export
contracts belong to the [user documentation](../../docs/telemetry/README.md).

Evidence levels apply to individual claims, not whole objects. Historical
runtime correlations do not establish current-worktree acceptance. Unreadable
or malformed state is unavailable, not numeric zero.

## Established claims and limits

| Boundary | Established claim | Limit |
| --- | --- | --- |
| Raw dates | Runtime-observed 24 units/day and fixed 365-day calendar anchor | `CGameState+0xb0c` and surrounding game-state semantics remain provisional. |
| World/treasury snapshots | Provisional container and field observations | Country slots are not playable-country counts; scheduler is not a general AI definition; no money-supply identity. |
| Factory identity/finance | Narrow runtime UI/save/toggle correlations | No wholesale layout promotion; upgrade, profit and funding rules are incomplete. |
| Factory physical consumption | `verified-runtime` callsites and stock boundaries | Consecutive, complete boundaries are required. Requested inputs are not fulfilled purchases. |
| Producer sales | Direct inventory/proceeds correlations | Quality remains provisional; domestic/export attribution is unproven. |
| Artisan recipe consumption | `verified-runtime` coefficient × production account | Recipe switches break cross-date input/output identity. |
| RGO output/owner modifier | Narrow runtime/UI correlations and static arithmetic | Income is not precious-metal cash creation. |
| POP economy/GiveMoney | Narrow fields and native call ABI runtime verified | Does not cover every redistribution path. |
| POP identity/lifecycle | Consecutive ID and descriptive-key correlations | No permanent cross-transition identity or demographic cause classification. |
| Country metrics/military/diplomacy; province/needs | Provisional despite selected save correlations | Broader transitions, symmetry and UI/type coverage remain unresolved. |

## Delivery and lifecycle

Campaign save-selection observation is after filename `+0x590` and flags
`+0x5bc/+0x5bd` are written. Load completion is the first nonzero `+0x5bd` before
play dispatch; campaign entry separately requires `CInGameIdler` RTTI readback.
These narrow observed boundaries do not make loading and entry one phase. The
[automation evidence](campaign-automation.md) owns the native campaign-control
claims; [native API documentation](../../docs/telemetry/native-api.md) owns
publication and drain behavior.

## Dates and world snapshots

`CGameState+0xb0c` supplies provisional raw date. The runtime-observed day unit
is 24. Calendar encoding is
`raw = 24 * (365 * (year + 5000) + zero_based_day_of_year)`;
`1836.1.2` corresponds to raw `59883384`. This unit correlation does not upgrade
surrounding game-state semantics.

World observations read country slots `CGameState+0xadc`, AI scheduler `+0xa4`
and player control `+0xaec`. Native container observations include 272 country
slots with 271 scheduler entries and human control enabled, versus 272 scheduler
entries and no human control in observer mode. Slots and scheduler entries
represent different populations; these counts remain provisional, not general
country or AI classifications.

### Economic inventory

| Field/source | Evidence and interpretation |
| --- | --- |
| Treasury `CCountry+0xe78` | Provisional readable-slot sum; interest-boundary deltas independently runtime verified. |
| POP money `+0x180` | `verified-runtime` GiveMoney effects; world traversal completeness remains provisional. |
| POP savings `+0x250` | `verified-runtime` storage and 1000:1 relation to state scale; deposit/claim, not liquid money. |
| Country-bank `+0x20` | `verified-runtime` temporary interest destination, not bank cash. |
| Creditor interest `+0x10`, debt `+0x18`, paid byte `+0x20` | `verified-runtime` save/repayment correlation; distinct rate, principal and payment status. |
| State `+0x258/+0x260` | Narrow runtime savings/pool correlations; rounding drift and nonconservation prevent a broader conservation claim. State layout remains provisional. |

Country ordinals span 1 through `country_count - 1`; linked state/province/POP
storage is described in [interest evidence](interest-payout.md#verified-traversal-and-mutation-boundaries).
Complete inventories require unique identities and complete native traversal.
These components can be different sides or phases of the same value. They do
not form an additive money-supply identity or establish bankruptcy effects.

## Country and province candidates

These remain **provisional**. Country identity is a normalized three-character
tag. The examples are narrow save correlations, not general layout verification.

| Family | Sources / units | Correlation | Limits |
| --- | --- | --- | --- |
| Metrics | `CCountry`: plurality `+0x1a8`, diplomatic points `+0x65c`, exhaustion `+0x680`, leadership `+0x7d0`, research `+0xe3c`, prestige `+0xea0`, ranks `+0x1404..+0x1410`, infamy `+0x1430`. Scalars /1000 except leadership /32768. | PRU raw prestige `50055`, plurality `25059`, diplomatic `5000`, research `32603`, leadership `270728`; overall/military/industrial rank 4, prestige rank 5. | Zero keys may be omitted in saves; rankings lack a serialized counterpart. |
| Military | Unit list `+0x7b4`, leadership `+0x7d0`, mobilized `+0x120`, rank `+0x1408`, scheduled vector `+0x15dc`; world wars `CGameState+0xb3c`. | PRU 12 units, no active/scheduled mobilization, leadership `270728`, rank 4; world wars 3. | Unit/war/combat storage and durable identities unmapped; no composition, participants, battles, casualties or movement. |
| Diplomacy | Flags `+0xcf4/+0xcf5`, overlord `+0xcf8`, vassals `+0xd38`, allies `+0xd58`, guarantees `+0xd78`, neighbors `+0xd88`, spherelings `+0x1418`, leader `+0x1428`. | PRU no overlord/leader, 15 spherelings, 5 allies, 0 vassals/guarantees, 21 neighbors. | Change-over-time and symmetry unestablished; relation objects/influence unmapped. |
| Province daily | `CProvince`: ID `+0x58`, owner `+0x128`, controller `+0x130`, colonial `+0x190`, life rating `+0x1a4`, infrastructure `+0x2b8`. | Province 1 RUS/RUS, colonial 2, life 20; 425 FRA/FRA, life 40; 549 PRU/PRU, infrastructure `160` versus serialized railroad 1. | Infrastructure-to-building conversion is not established. |
| Province production | Construction list `+0xd8`, building vector `+0x118`. | Berlin has 3 building slots and 0 constructions; save names fort/railroad and omits naval base. | Definition slots, not active-building/factory count; factories are state-level. |

## State factories

### Established fields and identity

Narrow runtime/save/UI correlations identify state list `CState+0x60` as factory
storage: Brandenburg has four factories, Nordrhein one and Schlesien two on
1836.1.2. RTTI `CStateBuilding` and constructor RVA `0x000f2bc0` establish a
`0x220` payload; list-node storage is separate, as detailed below.

| Field | Narrow runtime meaning / units |
| --- | --- |
| Definition `+0x18`, level `+0x20`, employees `+0x128` | Definition key, factory level and employed count. Glass/Ammunition/Small Arms/Paper correspond to UI order, level 1 and employee counts 1998/1698/1848/2431. |
| Output `+0xd8` | /32768 quantity; Small Arms 0.410 on 1836.1.2 and 0.389 on 1836.1.3. |
| Employment `+0xf0` | 16-byte `CPopEmployment` records: POP pointer `+8`, assigned count `+12`; type-key splits agree with UI/save. |
| Stock `+0x28` | Goods pool, distinct from requested inputs. |
| Recipe `CBuilding+0x12c` | Production type; output good `+0x80`, base output `+0x88`. |
| Finance `+0x150/+0x158/+0x160/+0x168/+0x170` | Budget/spending/sales/paychecks/investment; nonnegative /32768000 amounts. Expense signs are presentation, not stored negative values. |
| Injection money `+0x210`, days `+0x218` | Int64 money and int32 remaining days, distinct from project pointer `+0x1d0` and subsidy `+0x1d8`. |
| Subsidized `+0x180`, closed `+0x188` | Flags corresponding to paused UI toggles. Native close clears remaining injection and days; broader close effects are not established by flag correlation alone. |
| State ID `CState+0x0c` | Serialized state identity (Brandenburg 750); `+0x08=47` is object-type discriminator, not region ID. |
| Region `CState+0x250 -> CRegion` | RTTI/contiguous size `0x108`; key `+0x18=PRU_549`, display `+0x34=Brandenburg`. Split-owner portions can share region key/pointer. |

English localized names include Windows-1252 Württemberg/Westpreußen; stable
ASCII region keys are distinct from localized names. Upgrade projects use
embedded `CPopProject` at state `+0x1c8` with external goods storage. A Small Arms
upgrade duration of 730 days agrees with its definition; progress, material
acquisition and policy/ownership/funds/level/project-dependent availability
remain unmapped. Vanilla paycheck leftover factor 0.25 (GFM 0.3) does not
establish paycheck cadence.

### Injection fields and node extent

All addresses in this subsection are preferred **VAs**, not RVAs.

| Claim | Primary static evidence (`verified-static-callsites`) |
| --- | --- |
| Injection money `+0x210`, days `+0x218` | Writer VA `0x004f3ef0` receives the serialization subobject at complete object `+8`. Named token `injected_money` (`0x6c4`) reads subobject `+0x208/+0x20c` at VAs `0x004f4346/0x004f434c`; `injected_days` (`0x6c5`) reads subobject `+0x210` at VA `0x004f4394`. |
| `+0x1d0` is a `CGoodsProject*`, not money | Load path allocates `0xc0`, calls RTTI-identified constructor VA `0x00860390` and stores the pointer at complete object `+0x1d0`. Close VA `0x004f5750` invokes its deleting destructor and nulls the pointer. |
| `+0x1d8/+0x1dc` is an int64 subsidy amount, not days | Finance VA `0x004f4b30` stores both halves at VAs `0x004f4d0a/0x004f4d10` in the subsidized funding branch. |
| Payload `0x220`, node allocation `0x230` | Value-copy helper advances by `0x220` at VA `0x004c0a97`. VA `0x004cf426` allocates `0x230`; the unchanged base reaches the factory copy constructor, then links/byte marker are initialized at VAs `0x004cf453/0x004cf459/0x004cf463`. Previous/next/marker are `+0x220/+0x224/+0x228`, outside the payload. Marker lifecycle and trailing padding remain unclaimed. |

Injection money at `+0x210` uses /32768 scale (`verified-runtime`). Native
tranche function VA `0x004f56b0` divides remaining money by remaining days,
subtracts that tranche from `+0x210`, and decrements `+0x218`. On the final day
it consumes the whole remainder. This countdown does not establish general
funding policy or authorize direct mutation.

### Input demand versus consumption

Requested pool `CStateBuilding+0x80`: RVA `0x000dd470` copies/scales recipe,
adds scaled maintenance, subtracts `+0x28` stock and clamps zero. Daily RVA
`0x00084060` stores it before settlement `0x00082ff0`. Loaded-goods count at
global RVA `0x00e587f4` bounds pool indices: zero within the registry means an
empty slot; out-of-registry values can be stale (`0xff`/`0xee` at ordinal 48).
An observed 48-good registry is not a universal mod limit.

**Requested inputs are not fulfilled purchases.** Under shortages, cement
request value exceeds spending by 0.09938/0.41326 currency and canned food by
0.23857. Fully supplied requested-value/spending differences below 0.00009 do
not prove universal fulfillment. Request-as-purchase inventory equations are
therefore invalid.

Physical boundaries (`verified-runtime`): daily callsite
`0x000845fd -> 0x00082ff0` exposes post-consumption stock at factory `+0x28`
before purchases. Primary/secondary calls
`0x0008369d/0x000836aa -> 0x0007dc20` add delivered pools:

- Closing stock = pre-purchase stock + primary delivery + secondary delivery.
- Consumption = previous closing stock − current post-consumption stock.
- With skipped purchases, opening stock is the previous post-consumption stock.

Spending + opening stock value − closing stock value is only a provisional
monetary consumption estimate. Pricing both stock endpoints at current prices
avoids inventory revaluation, but transaction spending has a different price
basis. Physical stock boundaries do not need that monetary assumption.

### Finance, inventory and merges

Finance callsite `0x00088710 -> 0x000f4b30` takes the live factory as its first
argument; the 64-bit amount matches sales income `+0x160`. Output is `+0xd8`,
closing unsold quantity `+0x1f8`, opening unsold caller local at hook-stack
`+0xc4`. Narrow runtime correlations support:

- Budget delta = sales + investment − spending − paychecks.
- Realized sold quantity = opening unsold + produced − closing unsold.
- Proceeds = finance call amount, independently of physical sold quantity.

Output/income/price-based sold-output ratios range from 0.14 to 2.16. Inventory
carryover invalidates inferring sales from same-day or one-day-shifted output.

Ownership transfer has narrow `verified-runtime` topology consequences. With
no duplicate type, transfer of SAX provinces 558/559/560 to PRU preserves
recipient state 757, removes source state 907 and moves the unchanged level-1
fabric node immediately, without a daily tick. Region key `SAX_558` is unchanged.
With equal-level duplicate fabric nodes, the recipient node survives with its
subsidy flag and budget 1000.00; the source node with budget 931.91 is removed.
This establishes the recipient winner only for that equal-level case. Different
levels and merges above eight distinct types remain unresolved.

## World market

`CGameState+0xbcc -> CWorldMarket`; goods pools use zero-based mod-load ordinals
and /32768 values. The following fields have narrow runtime/save correlations,
not wholesale world-market layout verification:

| Offset | Save field |
| --- | --- |
| `+0x008` / `+0x060` | `supply_pool` / `last_supply_pool` |
| `+0x120` | `worldmarket_pool` |
| `+0x178` / `+0x1d0` | `demand` / `real_demand` |
| `+0x280` / `+0x2d8` | `price_pool` / `last_price_history` |
| `+0x434` / `+0x4f4` | `actual_sold` / `actual_sold_world` |

For Small Arms ordinal 1, serialized price 37.01001, supply 40.24960, demand
320.48798 and real demand 25.16080 distinguish price, offered quantity, demand
and realized demand. Sold 20.36069 and world sold 4.80011 are global totals,
not producer- or country-attributed sales.

## Artisan production

Narrow runtime/save correlations place the economy at `CPop+0x1d4` and engine ID
at `CPop+0x0c`. Offsets below are relative to the economy object.

| Offset | Field |
| --- | --- |
| `+0x00` / `+0x58` | Stock / recipe-need goods pools |
| `+0xb0` | Active production type; output good `+0x80`, base output `+0x88` |
| `+0xb8` / `+0xc0` / `+0xc8` | Last spending / current production / percent afforded |
| `+0xd0` / `+0xd8` / `+0xe0` | Domestic/export clearing values / leftover output |
| `+0xe8` / `+0xf0` / `+0xf8` | Throttle / needs cost / production income |

Quantities/fractions use /32768; money /32768000. Gross output = factor × base
output >>15. Recipe inputs `65536/65536/163840` with factor `23565` produce
consumption `47130/47130/117825`; prior closing stock `47132/47132/117830`
less residual `2/2/5` gives the same physical consumption (`verified-runtime`).
This does not assume recipe continuity across dates.

Settlement caller `0x00086bff -> 0x00083aa0` holds POP in EBX; primary/secondary
additions `0x00083fca/0x00083fda` hold POP in EDI/economy in ESI.

**False consumption boundary:** `0x00086b81 -> 0x000dd3c0` copies/scales recipe
and subtracts stock from a **temporary requirement pool** via `0x0007dca0`,
not artisan stock. Stock itself remains unchanged at that boundary.
Sales require consecutive POP/province/country/output identity; recipe changes
break the chain.

## RGO production

Narrow runtime/UI correlations identify `CProvince+0x1ac` as capacity: Berlin
549 fruit `195625`, Görlitz 687 coal `60000`; farmer/labourer employment sums are
`110896/54730`. Runtime vector global RVA `0x00e58728`, accessor `0x000c25b0`,
uses static index `province_id * 0xb0`; an eight-byte prefix precedes live
`CStateEmployment`. Observed vector length 3249 is not a mod-independent bound.

| Record offset | Meaning / scale |
| --- | --- |
| `+0x08` / `+0x0c` / `+0x1c` | Production type / goods / province identity |
| `+0x38` / `+0x40` | Efficiency / throughput, /32768 |
| `+0x58` | Employed workers |
| `+0x80` | Income, /32768000 |
| `+0x88` | Base size, /32768 |
| `+0x90` / `+0x98` / `+0xa0` | Domestic/export clearing values / leftover quantity, /32768 |

RVA `0x000de350` multiplies efficiency, throughput, recipe
`CProductionType+0x88` and record size `+0x88`, shifting 15 bits after each
multiplication. Berlin fruit: UI base 11.20 × efficiency 1.90 × throughput
0.9703 ≈ 20.648; raw output `676588`. Görlitz coal: 7.20 × 1.45 × 0.5472
≈ 5.713; raw output `187206`. Native raw results agree with the UI.

Owner modifier UI RVA `0x00340f8d`: province `+0x188 -> CState`, production
type `+0xf0 -> +0x28` chooses owner type in state `+0x118` population array;
divide by total RGO capacity state `+0xc8`. Berlin `4275/457875` gives raw
`305` (0.0093), Görlitz `848/60000` gives `463` (0.0141), matching UI
contributions. These are narrow runtime/static correlations.

**Precious-metal income is not cash creation.** Metz province 412,
`precious_metal_mine`/ordinal 17, has gross raw output `262128` (7.9995) and
income `7385056000`. Income matches serialized `last_income=225374.02344` at
save /32768, not gross output × vanilla cash rate 0.5. Cash-output valuation
requires the active-mod rate (vanilla 0.5, GFM 1.0), not non-tradeable market
price or RGO income.

## Producer sales and GDP limits

Sold = opening + produced − closing; revenue is independent. RGO/artisan opening
stock requires a prior closing boundary and identity continuity. Missing
settlement/shutdown phases cannot be treated as complete intervals.
Domestic/export sequential clearing does not establish a producer-attributed
split. Artisan export values are nonnegative but can be `32931`–`118028`, so
an upper bound of `32768` is false despite the fixed-point scale.

Physical consumption and producer inventory support narrow offline value-added
accounts, not current in-game `country.economy` interval completeness. Runtime
support for that newer interval account remains unestablished.

## POP snapshots, identity and needs

| Source | Evidence level / meaning |
| --- | --- |
| Money `+0x180`, savings `+0x250`, interest flow `+0x210`, total flow `+0x218` | `verified-runtime` narrow GiveMoney/storage effects; see [interest evidence](interest-payout.md#verified-traversal-and-mutation-boundaries). |
| ID `+0x0c`, size `+0x58`, employment `+0x60`, province `+0x64`, type `+0x68` | Current in-process ID; size/employment/province/type remain candidates. Candidate IDs come from province `+0x58`, type `+0x28`. |
| Consciousness `+0x118`, militancy `+0x120`, literacy `+0x128` | Candidate 48.15 (/32768) rates. |
| Life/everyday/luxury `+0x130/+0x138/+0x140` | Provisional bounded satisfaction, not demand or spending. |
| Culture pointer at `+0x6c`, religion pointer at `+0x70` | Narrow static/save correlation; constructors RVAs `0x00554a40/0x00554f40`, keys culture `+0x18`, religion `+0x10` (not extra serialized string `+0x2c`). |

Satisfaction raws `32768/24672/30` correspond to full life satisfaction,
everyday `0.7529296875` and luxury `0.0009155273`; serialized everyday/luxury
are `0.75293/0.00092`. Size `4275`, consciousness `98337`, literacy `22938`,
militancy `0` correspond to serialized `4275/3.00101/0.70001/0`. These
correlations do not promote the broader layout.

Descriptive dimensions cannot replace individual POP ID: among 23,429 unique
serialized IDs, province+type+culture+religion produces 22,610 keys with 329
duplicate keys, all artisans. Adding recipe still leaves 126 duplicate keys.
Consecutive IDs can persist, but new-ID causes and permanent identity are unknown.

### Migration-like counterexample to durable ID

A French/Catholic craftsmen cohort (type candidate 7), size 65, has the following
observed identity sequence:

| Raw date | Province / ID | Identity consequence |
| --- | --- | --- |
| Through `59883744` | 412 / 27606 | Original ID present. |
| `59883768` | 420 / 28227 | Original absent; same complete dimensions/size at a new ID. |
| `59883792` | 458 / 28266 | Prior ID absent; same dimensions/size at another new ID. |

Both transitions conserve the 65 observed people but replace ID. Matched pairs
are not universal proof of migration: the native migration boundary and
promotion/demotion/split/merge/deletion/save-reload/ID reuse semantics remain
unestablished. Only surviving IDs support observed scope changes.

## POP cash-flow accounting

`CPop::GiveMoney` RVA `0x0055a5f0` has the
[verified native ABI and narrow field effects](interest-payout.md#verified-traversal-and-mutation-boundaries).
Presentation callers establish the indexed flow meanings:

| Index | Meaning | Index | Meaning |
| --- | --- | --- | --- |
| 0 | Needs | 4 | Events |
| 1 | Welfare | 5 | Projects |
| 2 | Salary | 6 | Bank |
| 3 | Expenses | 7 | Interest |

Posted amounts and actual pre/post money deltas differ under engine clamping.
Equal-and-opposite individual residuals can cancel at country scope; type-level
residuals can likewise cancel. This supports redistribution outside the observed
calls, not a claim that every residual indicates missing cash flow.
Promotion/demotion/split/merge/direct redistribution are not proven to pass
through GiveMoney. Taxes/tariffs lack independent field/call attribution here
and cannot be inferred from needs/expenses.

## Historical performance constraints

These **direct-creditor** implementations predate the state-pool design and the
C-service port. Comparisons use the same vanilla save, observer mode, speed 5,
3,650 days, 30-day economic sampling, queue capacity 8192 and country filter
`ZZZ` (suppresses country detail, not global work). Pre-batching is revision
`f057bf5`; batched is `d8458bd`.

| Performance | Pre baseline | Pre fix | Batched baseline | Batched fix |
| --- | ---: | ---: | ---: | ---: |
| Elapsed s | 246.917 | 2074.719 | 222.658 | 314.257 |
| Days/s | 14.7823 | 1.75927 | 16.3928 | 11.6147 |
| Process CPU s | 325.781 | 2184.234 | 294.672 | 389.734 |
| Peak working set bytes | 1471287296 | 1489620992 | 1473703936 | 1479155712 |
| End working set bytes | unrecorded | unrecorded | 1381883904 | 1393106944 |
| End private bytes | 1593921536 | 1565233152 | 1590743040 | 1604603904 |
| Fix callback s | — | 1629.137 | — | 72.554 |

Workload size and collection costs needed to interpret those comparisons:

| Peak/cost | Pre baseline | Pre fix | Batched baseline | Batched fix |
| --- | ---: | ---: | ---: | ---: |
| Countries | 271 | 271 | 271 | 271 |
| Provinces | 2359 | 2359 | 2360 | 2348 |
| POPs | 22963 | 23044 | 23059 | 23074 |
| Collection median µs | 103560 | 103693 | 28950 | 29205 |
| Collection max µs | 111399 | 124363 | 35310 | 34712 |

Pre-batching elapsed/CPU ratios are 8.40×/6.70×; batched ratios are
1.411×/1.323×. Pre-batching callback time is 78.5% of fix elapsed time;
batched callback time is 79.2% of the 91.599 s elapsed increase. Recipient
traversal, allocation, mutation and postconditions remain material costs.
These measurements do not establish current performance, portability across
machines/saves/mods, state-pool payout cost or comprehensive money-supply effects.
