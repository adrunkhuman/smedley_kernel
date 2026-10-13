# Telemetry mapping evidence

Current [user contracts](../../docs/telemetry.md) are separate from this evidence.
The supported target is the English Windows HoD 3.04 executable specified in
[AGENTS.md](../../AGENTS.md). All runs below are **historical validation of their
recorded implementation**, not verification of the current worktree. No runtime
run was performed for this documentation cleanup. Where a run has no commit,
mod set or reproduction command recorded, that provenance remains unavailable.

## Established claims and limits

| Boundary | Evidence level / established claim | Limit |
| --- | --- | --- |
| Raw dates | Runtime-observed 24 units/day; fixed 365-day calendar anchor | `CGameState+0xb0c` and emitted dates remain provisional. |
| World/treasury snapshots | Provisional container/field observations | Slots ≠ playable countries; scheduler ≠ general AI definition; no money-supply identity. |
| Factory identity/finance | Narrow runtime UI/save/toggle correlations | No wholesale layout promotion; upgrades/profit/funding rules incomplete. |
| Factory physical consumption | `verified-runtime` callsites and reconciled stock boundaries | Exact executable, complete bounded capture and consecutive boundaries required. |
| Producer sales | Direct inventory/proceeds evidence | Emitted quality remains provisional; domestic/export attribution unproven. |
| Artisan recipe consumption | `verified-runtime` coefficient × production account | Recipe switches break cross-date input/output identity. |
| RGO output/owner modifier | Exact UI/static/runtime correlations below | Income is not precious-metal cash creation. |
| POP economy/GiveMoney | Narrow fields and call ABI runtime verified | Hook does not cover every redistribution path. |
| POP identity/lifecycle | Consecutive ID and descriptive-key correlations | No permanent cross-transition identity or demographic cause classification. |
| Country metrics/military/diplomacy; province/needs | Provisional despite selected save correlations | Broader transitions, symmetry and UI/type coverage remain needed. |
| Coordinated drain | Historical bundled completed path accepted | Runtime timeout retry not exercised. |

Kernel-owned `game_state/` services perform foreign reads and hooks; plugins use
versioned C tables, copied records and opaque session identities. The old
`PopRef`/`FactoryRef` adapter description is superseded. Guarded spans and
bounded metadata checks reject unavailable groups, not infer zero or promote
layout quality. Ownership migration smoke preserves earlier evidence levels.

## Validation fixture and reproduction

Many vanilla runs used unchanged `benchmark.v2`. Where explicitly recorded,
its preserved source SHA-256 was
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.
Do not infer that hash verification for runs whose notes did not report it.
Historical `interest_fix` labels below identify artifacts predating the rename
to `interest_bug_fix`.

The retained verification/export commands are:

```powershell
smedley_trace validate run.jsonl
smedley_trace summary run.jsonl
smedley_trace assert-benchmark run.jsonl --completed --days 365
smedley_trace assert-benchmark timeout.jsonl --failed timeout
smedley_trace factory-value-added run.jsonl factory-va.csv --country BEL
smedley_trace producer-sales run.jsonl producer-sales.csv --country FRA
smedley_trace pop-cashflow run.jsonl pop-cashflow.csv --country FRA
smedley_trace pop-stock-lifecycle run.jsonl pop-stock.csv --country FRA
smedley_trace country-gdp run.jsonl gdp.csv --country PRU
smedley_trace country-gdp run.jsonl gdp.csv --country FRA --gold-to-cash-rate 0.5
```

The [current capture recipe](../../docs/telemetry/trace.md#country-gdp) reproduces
the required data shape, not the exact historical run. Runtime reproduction
requires executable/signature validation, recorded plugins/mods/settings,
exact built revision, disposable or hash-preserved source save, and log/trace
inspection. Generated traces, saves and process data are not repository inputs.

## Delivery and lifecycle

Campaign save-selection observation occurs after filename `+0x590` and flags
`+0x5bc/+0x5bd` are written. Load completion is first nonzero `+0x5bd` before
play dispatch; campaign entry separately requires `CInGameIdler` RTTI readback.
These are narrow observation boundaries, not proof that loading and entry are
one phase. Final observer configuration waits for AI/view postconditions.

| Historical probe | Supporting evidence | Limit/counterexample |
| --- | --- | --- |
| July 31, 2026 observer `f2d403e1-82f8-46b8-8832-a136c822d38a` | Save selection → load → entry → speed/pause → delayed ENG-view observer configuration; 515 days, 1039 ordered records, no gaps/drops/write failure; responsive; mean enqueue/format 0.217 us. | One observer scenario, not current performance acceptance. |
| Initial non-observer probe (ID unrecorded) | 272 slots, 271 scheduler entries, human control true. | Back-to-back lifecycle drops and missing terminal event isolated nonblocking ingress contention. |
| Reliable-ingress repeat `154d1e12-79bf-494c-bb32-0332bbdf0f25` | Same world values; exact one day, all 12 expected records, no gaps/drops/write failure; source hash preserved. | Supports separate reliable lifecycle ingress, not losslessness on full queues. |
| Drain/native quit `4d3d4e44-b4e5-4d7f-b10a-b50bd5b96cfb` | Valid trace ended `benchmark.completed` sequence 10, summary 11; accepted/written 10/10, dropped 0, write failure false; process exited, source unchanged. | Completed bundled drain only; no runtime timeout-retry test. |
| Ownership migration `01edd2fb-4c64-468b-8db9-efeefecc7b05` | Exact supported executable, unchanged benchmark, observer automation, three FRA/all-country daily rules; factory consumption/sales, artisan consumption and POP cash flow together. 3 exact days, target 59883456; 13308 ordered records, no gaps/drops/write/family-invalid failures; factory 61, artisan 2083, aggregate cash flow 11132 accepted. Paused, drained, native exit, source hash preserved. | Smoke of moved boundaries; no mapping promotion or current-worktree acceptance. |

## Dates and world snapshots

`CGameState+0xb0c` supplies provisional raw date. July 31, 2026 GFM lifecycle
probe: 153 dated progress records, 152 transitions, delta 3648 = 152 × 24.
Calendar encoding is
`raw = 24 * (365 * (year + 5000) + zero_based_day_of_year)`; benchmark date
`1836.1.2` anchors raw `59883384`. Monthly/yearly use calendar periods; weekly
uses first eligible date plus seven-day intervals. Unit evidence does not
upgrade surrounding game-state semantics.

`world.daily` reads country slots `CGameState+0xadc`, AI scheduler `+0xa4` and
player control `+0xaec`. Observer automation independently uses these containers.
Ten-day vanilla observer run `c1a299d3-9172-40fd-bceb-b5547048d481` emitted ten
world and ten JAN treasury records: 272 slots, 272 scheduler entries, no human
control, matching logged scheduler growth 271 → 272. All 40 records were
healthy and exact-target. Non-observer counterexamples are in the delivery
section. Counts remain provisional container observations.

### Economic inventory

The kernel's bounded traversal (see [interest payout](interest-payout.md)) scans
country ordinals 1 through `country_count - 1`. Complete scans require clear
traversal flags and unique POP identities. Limits: 512 countries, 4096 provinces,
100000 POPs. Structural incompleteness leaves health only; credit-specific flags
suppress credit without invalidating independently complete holdings/capacity.

| Field/source | Evidence and interpretation |
| --- | --- |
| Treasury `CCountry+0xe78` | Provisional readable-slot sum; interest-boundary deltas independently runtime verified. |
| POP money `+0x180` | GiveMoney behavior runtime verified; world traversal completeness provisional. |
| Savings `+0x250` | Runtime storage and 1000:1 relation to state scale; deposit/claim, not liquid money. |
| Country-bank `+0x20` | Runtime-verified temporary interest destination, not bank cash. |
| Creditor interest `+0x10`, debt `+0x18`, `was_paid` | Save correlation and 12 exact repayment calls with debtor/creditor identity. `_candidate_raw` suffix retained for schema compatibility, not current narrow field quality. |
| State `+0x258/+0x260` | Savings correlation runtime verified with rounding drift; `+0x260` nonconserved/provisional. |

These components can be different sides/phases of the same value; no additive
money-supply identity, bankruptcy or comprehensive world-money claim is made.

| Historical run | Supporting result |
| --- | --- |
| `adfe8a43-413c-448a-b68b-3fd58f001723` | 2 exact observer days, 2 complete scans: 271 countries/597 states/2311 provinces; 19996 then 20030 POPs; 92611/90725 us; 8 accepted economic records, no gaps/drops/write failure; source unchanged. |
| `28dc5e32-ec54-4682-b383-11bbee695803` | 7 exact days from integrated single handler: seven each health/capacity/holdings/credit; complete, all snapshot/collection/credit flags clear; no duplicates/gaps/drops/write failure. |

## Country and province candidates

These remain provisional. Country identity is normalized three-character tag;
country observations use its daily event, not an additional country traversal.

| Family | Sources / bounds | Historical supporting evidence | Limits |
| --- | --- | --- | --- |
| Metrics | `CCountry`: plurality `+0x1a8`, diplomatic points `+0x65c`, exhaustion `+0x680`, leadership `+0x7d0`, research `+0xe3c`, prestige `+0xea0`, ranks `+0x1404..+0x1410`, infamy `+0x1430`. Scalars /1000 except leadership /32768. | PRU one-day `f1e91e80-a3fc-41f6-9dc4-45fc2577f068`: save-matched raw prestige 50055, plurality 25059, diplomatic 5000, research 32603, leadership 270728; zero infamy/exhaustion; ranks overall/military/industrial 4, prestige 5. | Zero keys omitted in save; rankings lack serialized counterpart. |
| Military | Unit list `+0x7b4`, leadership `+0x7d0`, mobilized `+0x120`, rank `+0x1408`, scheduled vector `+0x15dc`; world wars `CGameState+0xb3c`. Units/scheduled ≤100000; wars ≤4096. | Vanilla `970c3cf3-f56f-4f10-aed8-5f153534150a`: PRU 12 units, no active/scheduled mobilization, leadership 270728, rank 4, world wars 3. | Unit/war/combat storage and durable identities unmapped; no composition, participants, battle, casualties or movement. |
| Diplomacy | Flags `+0xcf4/+0xcf5`, overlord `+0xcf8`, vassals `+0xd38`, allies `+0xd58`, guarantees `+0xd78`, neighbors `+0xd88`, spherelings `+0x1418`, leader `+0x1428`. Each vector ≤512; three normalized tag bytes + terminator. | Vanilla `70db3477-0e35-4fa6-8ea4-415bc96e7483`: PRU no overlord/leader, 15 spherelings, 5 allies, 0 vassals/guarantees, 21 neighbors. | Change-over-time/symmetry untested; no relation objects/influence. Invalid metadata suppresses affected status/relations and increments invalid. |
| Province daily | `CProvince`: ID `+0x58`, owner `+0x128`, controller `+0x130`, colonial `+0x190`, life rating `+0x1a4`, infrastructure `+0x2b8`. | Unchanged vanilla benchmark `dd4c7396-4fa0-4598-9b76-e1d43874d690`: province 1 RUS/RUS, colonial 2, life 20; 425 FRA/FRA, life 40; 549 PRU/PRU, infrastructure 160 versus save railroad 1. Summary 4 attempts/3 accepted/1 contended drop. | Correlation and drop accounting, not infrastructure-to-building conversion. |
| Province production | Construction list `+0xd8` ≤4096; building vector `+0x118` ≤64. | Berlin `efc1f359-f33a-4105-a2f1-1fba825fe34d`: 3 slots/0 constructions; save fort/railroad, omitted naval base. | Definition slots, not active-building/factory count; factories state-level. |

## State factories

### Established fields and identity

Unchanged vanilla benchmark/UI: Brandenburg has four factories, Nordrhein one,
Schlesien two on 1836.1.2. Read-only live probe: Berlin 549 resolves to state
list `CState+0x60`, lengths 4/1/2. RTTI `CStateBuilding`, constructor RVA
`0x000f2bc0` initializes through `+0x218`, live links at `+0x220`: object size
`0x220`.

| Field | Supporting evidence |
| --- | --- |
| Definition `+0x18`, level `+0x20`, employees `+0x128` | UI-order Glass/Ammunition/Small Arms/Paper keys; all level 1, employees 1998/1698/1848/2431. |
| Output `+0xd8` | Small Arms 0.410 on 1836.1.2, 0.389 on 1836.1.3, /32768. |
| Employment `+0xf0` | 16-byte `CPopEmployment` records: POP pointer +8, assigned count +12; type-key splits exactly match UI/save. |
| Stock `+0x28` | Goods pool reproduces Small Arms ammunition/steel/cement/machine-parts save amounts. |
| Recipe `CBuilding+0x12c` | Production type output good `+0x80`, base output `+0x88` match definitions/live key. |
| Finance `+0x150/+0x158/+0x160/+0x168/+0x170` | Budget/spending/sales/paychecks/investment correlated twice; nonnegative /32768000 amounts, expense signs only UI. |
| Injection `+0x1d0/+0x1d8` | Small Arms save `injected_money=122750.09155`, `injected_days=7`; exact budget accounts have no separate injection residual. Deferred capital appears in last investment; funding/tranche rules unclaimed. |
| Subsidized `+0x180`, closed `+0x188` | Paused UI toggles each 0 → 1 with stable object/list position. Closing also resets `+0x218`; counter semantics unclaimed. |
| State ID `CState+0xc` | Save-correlated Brandenburg 750; `+0x8=47` is object-type discriminator, not region ID. |
| Region `CState+0x250 -> CRegion` | RTTI/contiguous size `0x108`; key `+0x18=PRU_549`, display `+0x34=Brandenburg`; split-owner portions share key/pointer (ENG/BRE/HAM/HAN Osthannover examples). |

English game localized names include Windows-1252 Württemberg/Westpreußen.
Only stable ASCII region keys are emitted until UTF-8 transcoding has a contract.
Upgrade projects use embedded `CPopProject` at state `+0x1c8` with external goods
storage; observed 730-day Small Arms upgrade matches its definition. Progress,
material acquisition and policy/ownership/funds/level/project-dependent
availability remain unmapped. Vanilla paycheck leftover factor 0.25 (GFM 0.3)
does not establish paycheck cadence; the referenced wiki supplied no cadence.

Bounds: 512 states, 64 factories/state, 4096 factories, 1024 employment
assignments/factory, 16384 input records/country. Checked links/spans/keys,
assignment totals, goods indices and nonnegative amounts apply only to selected
groups; malformed selected data suppresses the country poll/increments invalid.

### Input demand versus consumption

Requested pool `CStateBuilding+0x80`: RVA `0x000dd470` copies/scales recipe,
adds scaled maintenance, subtracts `+0x28` stock and clamps zero; daily RVA
`0x00084060` stores it before settlement `0x00082ff0`. Partial fulfillment
proves it remains requested demand, not fulfilled purchase.
Loaded-goods count at global RVA `0x00e587f4` bounds pool indices: zero means
empty in-registry; out-of-registry ordinal 48 held stale `0xff` (PRU)/`0xee`
(BEL); GFM probe loaded 48 goods.

Physical boundaries: daily callsite `0x000845fd -> 0x00082ff0` exposes
post-consumption stock at factory+0x28 before purchases. Primary/secondary
calls `0x0008369d/0x000836aa -> 0x0007dc20` add delivered pools.
Hooks verify five-byte calls and preserve registers/flags/original calls.
Closing = pre-purchase + primary + secondary; consumption = previous closing −
current post-consumption. Skipped purchases use previous post-consumption stock
as opening. Daily bound 2048 records/512 factories; overflow explicit invalid.

| Historical run | Supporting evidence / counterexample |
| --- | --- |
| PRU `8b355d27-c585-4fb7-8c8f-f79caf33c025` | 2 dates, 50 input/92 factory records, no invalid/drops. Second-day requested × same-date price matches all 7 spending amounts within 0.000081 currency (Glass 0.000056). Fully supplied case cannot prove fulfillment. |
| BEL `a859d772-6846-4749-9fb6-d12467774133` | 6 factories/3 dates/123 accepted, no invalid/drops. Cement request value exceeds spending 0.09938/0.41326; Canned Food 0.23857; fully supplied within 0.00009. Disproves request-as-purchase inventory equations. |
| Same BEL shortage case, provisional monetary estimate | Spending + opening stock value − closing stock value gives intermediate consumption. Current-price endpoints avoid inventory revaluation but transaction spending has a different price basis. Two dates: gross/intermediate/VA 110.4296/94.1747/16.2549 and 116.1762/98.4568/17.7194; all six factory results positive. Assumes interval alignment/no other transfers. Superseded by physical boundaries. |
| BEL 30-day `3aacdf40-e989-4d52-92c8-f052fa2a78d8` | 1230 accepted, no invalid/drops; 29 intervals/174 positive factory results despite Canned Food/Cement/Steel/Fabric/Small Arms shortages. Average gross/intermediate/VA 119.3726/99.8497/19.5229; VA range 16.2549–20.7850. Supports alignment, not common price basis. |
| Direct BEL 30-day `0cff14dc-70fa-4d36-b820-f819e13dcb3e` | 174 intervals, no incomplete boundaries/negative consumption/reconciliation failures. Max difference from old monetary estimate 0.000811603; average VA 19.522634493, range 16.255384248–20.784684079. |
| Renamed events `608c1a54-b2f5-41b8-8fcd-83c0382c51d1` | Complete flow summary/detail; first intervals VA 16.255384248/17.719507277. Establishes narrow callsite/phase/physical consumption as verified-runtime. |
| Flows-only `b7c6d433-2404-49f7-a12f-738810579949` | 4 dates, no ordinary inputs/finance; 24 production, 24 complete summaries, 69 flows; no gaps/drops/invalid/write failure. First hook warm-up; 2 exported intervals VA 17.719507277/19.813987492, verified-runtime. |

### Finance, inventory and merges

Finance callsite `0x00088710 -> 0x000f4b30`: first argument live factory,
64-bit amount matches sales income `+0x160`; output `+0xd8`, closing unsold
`+0x1f8`, opening unsold caller local at hook-stack `+0xc4`. Realized sold =
opening + produced − closing; proceeds = call amount.

| Historical probe | Supporting result / limit |
| --- | --- |
| Four-day `a4edf016-5d61-40e0-83e7-37b9df278e2a` | Every factory budget delta exactly sales + investment − spending − paychecks. Output/income/price inferred sold-output ratios 0.14–2.16: disproves sales inference without inventory. Later direct boundary supersedes that limitation. |
| Five-day `662343cc-fc1a-4ad9-a95a-4215541612c6` | 21 factory accounts, no invalid; closing chains to opening, including raw 0/10479/27007/42396/52866. |
| Vanilla `40d36695-696f-408e-af64-df266a1cfcc8` | 1836.1.3 seven PRU factories: four anchored Berlin 549, one 575, two 682; 28 identity/employment/production/finance accepted, no filtered/dropped/invalid; independent Brandenburg UI/process values exact. |
| Depth `734efd9b-7873-4c26-b9aa-660422b3d4ad` | 53 accepted (7 each four groups +25 inputs), no gaps/filters/drops/invalid; IDs 750/753/756, worker splits and Small Arms stocks match save. |
| Region `421a1be4-c4c4-4e03-bcec-01d380742c7e` | Same 53 records with region keys; no gaps/filters/drops/invalid. |
| Paused non-duplicate `changeowner` fixture | SAX provinces 558/559/560 → PRU. PRU state 757/pointer survives; SAX 907 disappears; unchanged level-1 fabric node moves into PRU immediately, no tick; `SAX_558` unchanged, PRU provinces 687/558/559/560. |
| Equal-level duplicate fixture | Subsidized PRU fabric node `0x3d3e96c0`, budget 1000.00 versus unsubsidized SAX `0x44615010`, 931.91. Transfer 558/559/560 leaves original PRU node/subsidy/budget, removes SAX node. Recipient wins equal-level duplicates. |

Public OpenVic testing reported duplicate reconciliation but disputed the winner;
the controlled supported-game fixtures establish only the two cases above.
Different levels and merges above eight distinct types remain untested.

## World market

`CGameState+0xbcc -> CWorldMarket`; goods pools use zero-based mod-load ordinals
and /32768 values, matching benchmark save byte-for-byte.

| Offset | Save field |
| --- | --- |
| `+0x008` / `+0x060` | `supply_pool` / `last_supply_pool` |
| `+0x120` | `worldmarket_pool` |
| `+0x178` / `+0x1d0` | `demand` / `real_demand` |
| `+0x280` / `+0x2d8` | `price_pool` / `last_price_history` |
| `+0x434` / `+0x4f4` | `actual_sold` / `actual_sold_world` |

Run `51693f9a-19fb-4900-bfb8-3254022e79ae`: 192 records/48 goods, no
gaps/filters/drops/invalid. Small Arms ordinal 1 save values: price 37.01001,
previous 37.00000, supply 40.24960, demand 320.48798, real demand 25.16080,
sold 20.36069, world sold 4.80011. These global totals cannot establish producer
or country sales. The four-day finance counterexample above disproves same-day
or one-day output/income/price inference; direct inventory now supplies the
separate producer account.

## Artisan production

Runtime/save correlation: economy at `CPop+0x1d4`, engine ID `CPop+0x0c`.
Offsets below are relative to the economy object.

| Offset | Field |
| --- | --- |
| `+0x00` / `+0x58` | Stock / recipe-need goods pools |
| `+0xb0` | Active production type; output good `+0x80`, base output `+0x88` |
| `+0xb8` / `+0xc0` / `+0xc8` | Last spending / current production / percent afforded |
| `+0xd0` / `+0xd8` / `+0xe0` | Domestic/export clearing values / leftover output |
| `+0xe8` / `+0xf0` / `+0xf8` | Throttle / needs cost / production income |

Quantities/fractions use /32768; money /32768000. Berlin POP 8845 exactly
matched save: ammunition recipe, production 3116, afforded 32768, spending and
needs cost 1126244000, income 163852000, recipe inputs 65536/65536/163840.
Gross output = factor × base output >>15.

Later factor 23565 gives recipe consumption 47130/47130/117825. Independent
boundary trace: prior closing 47132/47132/117830, residual 2/2/5, exactly the
same consumption. This verifies physical recipe consumption without assuming
input continuity when recipes switch.

Settlement caller `0x00086bff -> 0x00083aa0` holds POP in EBX; primary/secondary
additions `0x00083fca/0x00083fda` hold POP in EDI/economy in ESI. Three-day
country-filtered run (ID unrecorded) captured all four boundaries after warm-up
for nine Berlin artisans: 40 per-good records, no gaps/drops/invalid. Bound:
8192 records/date for at most 16 country tags.

Counterexample: `0x00086b81 -> 0x000dd3c0` copies/scales recipe and subtracts
stock from a **temporary requirement pool** via `0x0007dca0`, not artisan stock.
Temporary before/after wrapper found unchanged stock. It is not the physical
consumption boundary; that exploratory wrapper is not retained behavior.
Sales require consecutive POP/province/country/output identity; recipe changes
break the chain. Shared sales validation is below.

## RGO production

Paused UI: `CProvince+0x1ac` capacity, Berlin 549 fruit 195625 / Görlitz 687
coal 60000. Farmer/labourer employment sums 110896/54730 match UI.
Runtime vector global RVA `0x00e58728` has exactly 3249 elements, accessor
`0x000c25b0`, static index `province_id * 0xb0`; eight-byte prefix then live
`CStateEmployment`.

| Record offset | Meaning / scale |
| --- | --- |
| `+0x08` / `+0x0c` / `+0x1c` | Production type / goods / province identity |
| `+0x38` / `+0x40` | Efficiency / throughput, /32768 |
| `+0x58` | Employed workers |
| `+0x80` | Income, /32768000 |
| `+0x88` | Base size, /32768 |
| `+0x90` / `+0x98` / `+0xa0` | Domestic/export clearing values / leftover quantity, /32768 |

Berlin `orchard`/fruit ordinal 33: UI base 11.20 × efficiency 1.90 × throughput
0.9703 = 20.648. Görlitz `coal_mine`/coal 10: 7.20 × 1.45 × 0.5472 = 5.713.
RVA `0x000de350` multiplies efficiency, throughput, recipe `CProductionType+0x88`,
record size `+0x88`, shifting 15 bits after each: raw 676588/187206 exactly
matches UI. Tooltips expose owner/technology/worker/infrastructure contributions.

Owner modifier UI RVA `0x00340f8d`: province `+0x188 -> CState`, production type
`+0xf0 -> +0x28` chooses owner type in state `+0x118` population array; divide
by total RGO capacity state `+0xc8`. Berlin 4275/457875 → raw 305 (0.0093);
Görlitz 848/60000 → 463 (0.0141), exact UI contributions.

| Historical run | Supporting result |
| --- | --- |
| `ea77637b-f661-47f5-b85f-18d48de2a5a0` | Exact one day; both provinces' four groups, 2 attempts/8 accepted, no filtered/dropped/invalid. |
| `b08cf98f-3ece-4467-9aec-6afe3284ab14` | Exact gross raws 676588/187206, exact date, 8 accepted, no invalid/drops. |
| `e5f0655c-7a87-4342-a77a-96c2b2d47492` | Separate modifier records with 305/463; 10 accepted, no filtered/dropped/invalid. |
| Metz `962c2f5b-afb6-4ef2-b29d-eb579c480561` | Province 412 `precious_metal_mine`/ordinal 17; gross 262128 (7.9995), income 7385056000, no gaps/drops/invalid. Income matches save `last_income=225374.02344` at save /32768, not gross × vanilla cash rate 0.5. |

Thus precious-metal RGO income is not cash creation. GDP uses explicit active-mod
rate (vanilla 0.5, GFM 1.0), never non-tradeable market price or income.

## Producer sales and GDP validation

Sold = opening + produced − closing; revenue retained independently. RGO/artisan
opening stock needs warm-up and continuity. Domestic/export sequential clearing
semantics do not prove a producer-attributed split. Export value is nonnegative
but not bounded to 32768; the old upper-bound validation was false.

| Historical run | Supporting evidence / counterexample |
| --- | --- |
| Five-day `662343cc-fc1a-4ad9-a95a-4215541612c6` | Factory/RGO/artisan pairs 21/184/498; RGO/artisan summaries 230/630; zero producer invalid/drops, all complete quantity chains reconcile. Missing first-day pairs explicit warm-up. |
| 30-day `dc58f6ce-369a-48c3-a1ad-fdae369b0193` | Strict 5152 rows: 196 factory/1334 RGO/3622 artisan; no gaps/drops/write failures/invalid/reconciliation failures. |
| Annual `7212971a-f622-488a-9283-e02056e5c001` (superseded) | 387773 records; factory/RGO healthy, Machine Parts 199 post-warm-up no-settlement days exercise incomplete/shutdown. All 44881 artisan pairs reconcile but 213 other invalid attempts cause correct strict rejection. |
| Annual diagnostic `2301bee7-364f-49b7-ba53-dd74dc5fcab0` | All 349 rejected artisan reads have nonnegative export values 32931–118028, disproving the /32768 upper bound. Different diagnostic run/count, not a reclassification of the 213 count above. |
| Repaired annual `e2ae9961-4698-473e-840b-2dace5271dc0` | Exact 365 days, 385929 records, no gaps/drops/write failures/family-invalid; strict 63952 producer accounts. Supersedes false-bound rejection, not producer mapping uncertainty. |
| Three-day PRU GDP `001c25f0-f5a4-429d-89b7-fafd126a3487` | Capture contracts, 2859 records; no gaps/drops/write/family-invalid; nominal 1422.356650989, real 1415.854841757, population 3564450. |
| FRA GDP `64dc24a5-e811-4ed5-b3e9-90fa1943fe79` | Contracts/healthy daily 87 RGOs, 164 active artisans, POP/market/flows; vanilla rate 0.5: nominal 3997.530945404, real 3985.369406416, population 8784157, precious-metal VA 4.024536133. |

These validate historical strict offline exports, not current in-game
`country.economy` interval completeness. No distinct runtime validation of that
newer interval account was recorded in the source material.

## POP snapshots, identity and needs

| Source | Evidence |
| --- | --- |
| Money `+0x180`, savings `+0x250`, interest flow `+0x210`, total flow `+0x218` | Narrow runtime evidence reused from interest work/GiveMoney. |
| ID `+0x0c`, size `+0x58`, employment `+0x60`, province `+0x64`, type `+0x68` | Current in-process ID; size/employment/province/type candidates. Candidate IDs from province `+0x58`, type `+0x28`. |
| Consciousness `+0x118`, militancy `+0x120`, literacy `+0x128` | Candidate 48.15 (/32768) rates. |
| Life/everyday/luxury `+0x130/+0x138/+0x140` | Provisional bounded satisfaction, not demand/spending. |
| Culture pointer `CCulture*` at `+0x6c`, religion object pointer at `+0x70` | Constructors RVAs `0x00554a40/0x00554f40`; keys culture `+0x18`, religion `+0x10` (not extra serialized string `+0x2c`). |

| Historical probe | Supporting evidence / limit |
| --- | --- |
| Save scan `952f4ca8-fbfc-4859-8ce6-b18995f30431` | POP 18563: satisfaction raws 32768/24672/30; save everyday 0.75293/luxury 0.00092, omitted/default life. Ratios 0.7529296875/0.0009155273; full life correlation. Broad exploratory layout event removed. |
| Identity `a54302ac-af7a-470d-8c6f-f457b8e14d2b` | POP 18563 clergymen/polish/catholic matches unchanged save; 449 PRU records, no invalid/gaps/drops/write failure. |
| Complete line-oriented save audit | 23429 POPs/unique IDs; province+type+culture+religion yields 22610 keys, 329 duplicate keys, all artisans; adding recipe still 126 duplicates. Descriptive tuple cannot replace individual ID. |
| Berlin `4f40b617-b56a-4478-81b1-9e35b1d90b4e` | Province 549: 23 economy +23 demographic records, no filtered/dropped/invalid. First POP size 4275, consciousness 98337, literacy 22938, militancy 0 match save 4275/3.00101/0.70001/0; type candidates 1/2 correlate aristocrat/artisan. |
| Shared copy `3ac4d510-7dbe-45af-9701-1902379785df` | 23 economy/23 demographic/10 aggregate accepted, no drops/invalid; first copy 90224 us, other rules 0 extra. Type 2 nine POPs/size 60020; types 7/8 four/three POPs. Aggregates intentionally cross culture/religion. |
| PRU three-day `081308fc-fdd5-4bf4-ba62-913b60da54f8` | 1348 economy/1348 demographic/709 aggregate records, healthy; date/ID keys match; unique IDs 449/449/450, original 449 persist. Consecutive stability only, new-ID cause unknown. |
| Lifecycle `39d37341-e686-4094-bb8a-e24ba5f104d7` | Complete 19996-POP world opening stock, PRU detail; after warm-up +40/−6 then +43/−6, PRU appeared 27654. All 3453 records valid, no gaps/drops/write/lifecycle-invalid. Observed changes, not demographic causes. |
| Needs `dc0c5f46-abab-4c00-8143-7cdc32712c99` | 449 PRU needs +lifecycle warm-up, 1632 total, no gaps/drops/write/family-invalid; all satisfaction values 0–32768, no broad layout event. Separate reader rejection does not invalidate established shared POP copy. |

### Migration-like counterexample to durable ID

17-day FRA run `719679d6-4d1a-439d-937b-b8104c3164e3` followed French/Catholic
craftsmen, type candidate 7, size 65:

| Raw date | Province / ID | Observation |
| --- | --- | --- |
| Through 59883744 | 412 / 27606 | Original cohort remains. |
| 59883768 | 420 / 28227 | Original disappears; same complete dimensions/size appear. |
| 59883792 | 458 / 28266 | Prior ID disappears; same dimensions/size appear again. |

Both moves conserve all 65 observed people but replace ID, so they emit
appearance/disappearance, not `scope_changed`. Native migration boundary was
not instrumented: matched pairs are not universal proof of migration.
Exact target, 200010 records, no gaps/drops/family-invalid/write failure; strict
export accepted all 17 stocks/reconciliations; source hash preserved.
Promotion/demotion/split/merge/deletion/save-reload/ID reuse semantics remain
unestablished. Only surviving IDs can have scope changes.

## POP cash-flow accounting

`CPop::GiveMoney` RVA `0x0055a5f0`: seven reviewed direct callers establish
EAX=POP, ESI=index, stack 64-bit amount, `ret 8`. Reversible +1000/−1000 fixture
verified money `+0x180`, indexed flow `+0x1d8`, total `+0x218`, exact restoration.
Presentation callers establish 0 needs, 1 welfare, 2 salary, 3 expenses,
4 events, 5 projects, 6 bank, 7 interest. Observational hook records posted and
actual pre/post delta, preserving engine clamping.

| Historical run | Supporting evidence / limit |
| --- | --- |
| `eade1c13-e28f-4113-97b3-07410ece7f74` | 1765/1799 opening-seen individual accounts reconcile; 34 residuals equal/opposite, type residuals cancel at country scope. Evidence for redistribution outside observed calls, not incomplete capture by itself. |
| `f442f9c0-b2fc-47f6-9ad7-05ddfba38243` | 5-day hook/strict export, 7598 records; no gaps/drops/write/family-invalid. |

Promotion/demotion/split/merge/direct redistribution are not proven to pass
through GiveMoney. Individual/type residuals stay explicit. Taxes/tariffs lack
independent field/call attribution and cannot be inferred from needs/expenses.

<a id="historical-pre-batching-paired-ten-year-observer-benchmark"></a>
<a id="final-batched-paired-ten-year-observer-benchmark"></a>

## Historical paired ten-year benchmarks

Both pairs ran consecutively on 2026-08-01, unchanged benchmark, observer mode,
speed 5, 3650 days, 30-day economic sampling, queue 8192, country filter `ZZZ`
(suppresses country detail, not global records). Start/target
59883384/59970984 exact; paused/responsive after completion; source hash
preserved. Each trace has 122 health/capacity/holdings/credit snapshots; all 244
health records per pair complete with snapshot/probe/credit flags zero.

| Implementation | Variant | Run ID | Trace SHA-256 | Records |
| --- | --- | --- | --- | --- |
| Pre-batching `f057bf5`, timeout 7200 s | Baseline | `a267ac0b-5fd9-4efa-973e-ac3129155474` | `7686a63561de070660356fefcda2c496c97e0971fc823f80fa207bf40bdb2054` | 4270 |
| Pre-batching | `interest_fix` | `c6193149-f6b2-4a88-9366-32484389d40a` | `43098eae4a75efdd48eab0c4cf74b887968d69b57684ff1bb93377695d7499d1` | 235918 |
| Batched `d8458bd`, timeout 1800 s | Baseline | `3cca7928-811d-4819-a431-7a4baf960ab4` | `a8369aee355d6d61e63346486b9de85d9359cfa13733781ed110f3849816621c` | 4270 |
| Batched | `interest_fix` | `c99a16f5-41e3-40ef-8f75-f6cc835f3aee` | `0acbe58e187c5d4950ec03908294d8b653c678b6faf0e373762c6588f7e7b7fc` | 11571 |

Every trace: gaps/drops/write failure `0/0/false`. All peaks below bounds:

| Peak/cost | Pre baseline | Pre fix | Batched baseline | Batched fix | Limit |
| --- | ---: | ---: | ---: | ---: | ---: |
| Countries | 271 (52.92%) | 271 (52.92%) | 271 (52.92%) | 271 (52.92%) | 512 |
| Provinces | 2359 (57.59%) | 2359 (57.59%) | 2360 (57.61%) | 2348 (57.32%) | 4096 |
| POPs | 22963 (22.96%) | 23044 (23.04%) | 23059 (23.05%) | 23074 (23.07%) | 100000 |
| Collection median us | 103560 | 103693 | 28950 | 29205 | — |
| Collection max us | 111399 | 124363 | 35310 | 34712 | — |

| Performance | Pre baseline | Pre fix | Batched baseline | Batched fix |
| --- | ---: | ---: | ---: | ---: |
| Elapsed s | 246.917 | 2074.719 | 222.658 | 314.257 |
| Days/s | 14.7823 | 1.75927 | 16.3928 | 11.6147 |
| Process CPU s | 325.781 | 2184.234 | 294.672 | 389.734 |
| Peak working set bytes | 1471287296 | 1489620992 | 1473703936 | 1479155712 |
| End working set bytes | unrecorded | unrecorded | 1381883904 | 1393106944 |
| End private bytes | 1593921536 | 1565233152 | 1590743040 | 1604603904 |

Pre-batching elapsed/CPU ratios 8.40x/6.70x, days/s 0.119x, peak memory delta
+18333696, end private −28688384. Fix callback time 1629.137 s: 78.5% of its
elapsed, 87.7% of CPU increase. Batched elapsed/CPU 1.411x/1.323x, days/s
0.709x; peak/end working set +5451776/+11223040, private +13860864. Callback
72.554 s = 79.2% of 91.599 s elapsed increase. Recipient traversal/allocation,
mutation and postcondition costs remain material. The 8.40x → 1.41x comparison
is fixture-specific, not a current or cross-machine/save/mod performance claim.

Batched fix: 3650 health/value pairs; destination bank gain 2003503700
(domestic 343963354, foreign 1659540346), exact POP payout 2003503700000;
zero rejected debtors/failed recipients/result drops/postcondition failures.

Final samples are separate raw categories from divergent simulations, not
additive money supply or causal decomposition:

| Field | Pre baseline | Pre fix | Batched baseline | Batched fix |
| --- | ---: | ---: | ---: | ---: |
| `treasury_observed_raw` | 99950580341 | 114708632203 | 99030403012 | 106278163339 |
| `pop_money_observed_raw` | 29809689504634 | 23300428465520 | 28056708337586 | 31425196653994 |
| `pop_savings_observed_raw` | 128776949934488 | 127667712827126 | 125012634775634 | 144892903609587 |
| `creditor_count` | 455 | 576 | 492 | 622 |
| `creditor_debt_candidate_raw` | 4019948489 | 2864622699 | 2629676588 | 3551628691 |
| `state_interest_candidate_raw` | 1924693371 | 1179563725 | 1438307103 | 1850768026 |

All final samples: negative-treasury countries 0, bank interest accumulator 0.
Bankruptcy and comprehensive money supply remain unmapped.
