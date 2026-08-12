# Telemetry mapping evidence

## Adapter ownership

Telemetry raw adapters are implemented under `game_state/`: current-state,
daily-event `CountryRef` conversion, and copied country/province snapshots in
the telemetry runtime adapter; POP, artisan, factory-consumption, and
factory-sales hooks are in the kernel-owned `game_state/` implementation. The telemetry module receives
typed `PopRef`/`FactoryRef` records or copied reader snapshots and retains
capture, filtering, aggregation, and publication policy.
This ownership move preserves the evidence levels below; the post-migration
smoke records operation of the moved boundaries but does not promote their
existing mapping quality.

The adapter copies fields through guarded spans and validates vector/list
metadata with the existing reader bounds before reporting a capture group.
Malformed metadata makes only that group unavailable; it does not promote the
underlying layout evidence or turn an unavailable value into zero.

Post-migration run `01edd2fb-4c64-468b-8db9-efeefecc7b05` used the exact
supported executable, unmodified `benchmark.v2`, observer campaign automation,
and three daily FRA/all-country capture rules. It exercised factory consumption
and sales, artisan consumption, and POP cash-flow hooks together for three exact
days. The trace contained 13,308 ordered records with zero gaps, drops, writer
failure, or family-invalid results. `state.factory` accepted 61 records,
`pop.artisan` accepted 2,083, and `pop.cashflow.aggregate` accepted 11,132. The
run paused at raw target `59883456`, drained telemetry, exited natively, and
retained source-save SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

## Raw Game-Date Units

`CGameState+0x0b0c` is exposed as `game_date_raw` with `provisional` quality.
In the supported executable, one observed daily simulation transition advances
the value by exactly 24. A July 31, 2026 GFM lifecycle probe independently
recorded 153 consecutive dated progress records spanning 152 transitions: raw
values advanced by 3,648 from first to last (`152 * 24`). Smedley therefore uses
24 raw units per game day for sampling intervals and trace-tool throughput
calculations.

This observation establishes the unit conversion and repeated live behavior;
it does not upgrade the surrounding `CGameState` layout or field semantics
beyond the quality carried by each telemetry record.

The complete Victoria II encoding is a fixed, non-leap calendar:
`raw = 24 * (365 * (year + 5000) + zero_based_day_of_year)`. The local
`benchmark.v2` date `1836.1.2` and runtime raw value `59883384` provide the
executable-specific anchor. Monthly and yearly telemetry use this conversion;
weekly telemetry remains a seven-day interval anchored to the first eligible
observation.

## World Snapshot Containers

`world.daily` reads the existing `CGameState` country-slot vector at `+0x0adc`,
AI scheduler vector at `+0x00a4`, and player-control vector at `+0x0aec` once per
selected date. Observer automation already uses the same containers for its
runtime invariants, including exact scheduler growth when returning a country
to AI. The record remains `provisional`: slot count is not a playable-country
count, scheduler membership is not a general AI-control definition, and the
surrounding `CGameState` layout is not promoted wholesale.

Runtime run `c1a299d3-9172-40fd-bceb-b5547048d481` exercised a ten-day vanilla
observer interval. It emitted exactly ten `world.daily` and ten JAN
`country.daily` records. Every world record reported 272 country slots, 272 AI
scheduler entries, and `human_control_present=false`, matching the independently
logged observer transition from 271 to 272 scheduler entries. The 40-record
trace had zero gaps, drops, or write failures and completed at the exact target.

An initial complementary non-observer probe observed 272 slots, 271 scheduler
entries, and `human_control_present=true`, but its trace dropped back-to-back
lifecycle records and lacked a terminal record. This isolated lock contention
in the nonblocking ABI ingress. After adding the separate reliable lifecycle
symbol, run `154d1e12-79bf-494c-bb32-0332bbdf0f25` repeated the exact one-day
case with the same world values and all 12 expected records. It completed at the
exact target with zero gaps, drops, or write failures; the source save retained
SHA-256 `f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

## Economic Snapshot Inventory

`telemetry.dll` reuses the bounded state/province/POP traversal documented
in `interest-payout.md`. It scans country ordinals 1 through `country_count - 1`
once on the first daily callback for each telemetry-selected sample date,
retaining no raw pointers afterward. Complete snapshots require zero traversal
flags and unique POP identities across every country.

| Field family | Source | Evidence and interpretation |
| --- | --- | --- |
| `treasury_observed_raw` | `CCountry+0xe78` | Provisional world sum of readable country-slot treasuries. Interest-boundary deltas are independently runtime verified. |
| `pop_money_observed_raw` | bounded POP `+0x180` | Narrow field behavior is runtime verified by `CPop::GiveMoney`; completeness of the world traversal remains provisional. |
| `pop_savings_observed_raw` | bounded POP `+0x250` | Runtime-verified savings storage and `1000:1` relation to state scale; it is a deposit/claim category, not liquid POP money. |
| `bank_interest_accumulator_raw` | each country bank `+0x20` | Runtime-verified temporary interest destination, not bank cash. |
| creditor counts, interest, debt, and `was_paid` | bounded `CCreditor` vectors | `+0x10` interest and `+0x18` debt are runtime verified by save correlation and 12 exact repayment calls with creditor/debtor identity. The creditor interest/debt telemetry keys retain the `_candidate_raw` suffix for schema compatibility; for those keys, the suffix no longer describes mapping quality. |
| state `+0x258`/`+0x260` | bounded country state lists | Savings correlation is runtime verified with rounding drift; `+0x260` is nonconserved and provisional. |

The plugin emits observed components separately and makes no additive
money-supply claim. In particular, treasuries, POP money, POP savings, creditor
claims, state aggregates, and the bank interest accumulator can represent
different sides or phases of the same economic value.

Hard limits are 512 scanned countries, 4,096 resolved provinces, and 100,000
POP records per snapshot. Structural incompleteness emits only
`world.economy.health`; apparently plausible partial holdings, credit, or
capacity values are suppressed. Credit-specific flags suppress only the credit
record because they do not invalidate independently complete holdings and
capacity traversal.

Observer smoke run `adfe8a43-413c-448a-b68b-3fd58f001723` completed two exact
days with two complete snapshots: 271 countries, 597 states, 2,311 provinces,
and 19,996 then 20,030 POP records. Collection took 92,611 microseconds for the
first snapshot and 90,725 microseconds for the second. All eight economic records
were accepted with zero sequence gaps, drops, or writer failure, and the source
save remained unchanged.

Integrated smoke run `28dc5e32-ec54-4682-b383-11bbee695803` removed the
standalone producer and exercised the same scan from `telemetry.dll`'s single
daily handler for seven exact days. The trace contained exactly seven each of
`world.economy.health`, capacity, holdings, and credit, with no duplicates,
sequence gaps, drops, or writer failures. All seven scans were complete with
zero snapshot, collection, and credit flags.

## Historical pre-batching paired ten-year observer benchmark

Consecutive runs on 2026-08-01 used pre-batching commit `f057bf5`, the same unmodified
`benchmark.v2`, observer mode, speed 5, 3,650 days, a 7,200-second safety
timeout, 30-day state sampling, queue capacity 8,192, and country filter `ZZZ`.
The filter suppresses unrelated high-volume country records; world and economic
records remain global. Both runs advanced from raw date `59883384` to exact
target `59970984`, remained paused and responsive, and preserved source-save
SHA-256 `f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

| Variant | Run ID | Trace SHA-256 | Records | Gaps/drops/write failure |
| --- | --- | --- | ---: | --- |
| Baseline | `a267ac0b-5fd9-4efa-973e-ac3129155474` | `7686a63561de070660356fefcda2c496c97e0971fc823f80fa207bf40bdb2054` | 4,270 | `0 / 0 / false` |
| `interest_fix` | `c6193149-f6b2-4a88-9366-32484389d40a` | `43098eae4a75efdd48eab0c4cf74b887968d69b57684ff1bb93377695d7499d1` | 235,918 | `0 / 0 / false` |

Each trace contains 122 health, capacity, holdings, and credit snapshots. All
244 health records have `complete=true` and zero snapshot, probe, and credit
flags. Capacity remained below every fixed bound:

| Peak or cost | Baseline | `interest_fix` | Limit |
| --- | ---: | ---: | ---: |
| Countries | 271 (52.92%) | 271 (52.92%) | 512 |
| Provinces | 2,359 (57.59%) | 2,359 (57.59%) | 4,096 |
| POPs | 22,963 (22.96%) | 23,044 (23.04%) | 100,000 |
| Snapshot collection, median | 103,560 us | 103,693 us | n/a |
| Snapshot collection, maximum | 111,399 us | 124,363 us | n/a |

The benchmark's own process counters produced this paired performance result:

| Measure | Baseline | `interest_fix` | Observed ratio/delta |
| --- | ---: | ---: | ---: |
| Benchmark elapsed | 246.917 s | 2,074.719 s | 8.40x |
| Game days/second | 14.7823 | 1.75927 | 0.119x |
| Process CPU | 325.781 s | 2,184.234 s | 6.70x |
| Peak working set | 1,471,287,296 | 1,489,620,992 | +18,333,696 bytes |
| End private bytes | 1,593,921,536 | 1,565,233,152 | -28,688,384 bytes |

The fix trace reports 1,629.137 seconds inside its post-original callbacks,
78.5% of fix-run elapsed time and 87.7% of the process-CPU increase over this
baseline. This identifies destination POP traversal, allocation, mutation, and
postcondition checking as the measured cost in this scenario; it is not a claim
about performance on other machines, saves, or mods.

Final sampled economic observations differed as follows. These are separate raw
categories from one pair of simulations, not an additive money-supply identity
or a causal estimate:

| Final field | Baseline | `interest_fix` | Difference |
| --- | ---: | ---: | ---: |
| `treasury_observed_raw` | 99,950,580,341 | 114,708,632,203 | +14,758,051,862 |
| `pop_money_observed_raw` | 29,809,689,504,634 | 23,300,428,465,520 | -6,509,261,039,114 |
| `pop_savings_observed_raw` | 128,776,949,934,488 | 127,667,712,827,126 | -1,109,237,107,362 |
| `creditor_count` | 455 | 576 | +121 |
| `creditor_debt_candidate_raw` | 4,019,948,489 | 2,864,622,699 | -1,155,325,790 |
| `state_interest_candidate_raw` | 1,924,693,371 | 1,179,563,725 | -745,129,646 |

Both final samples reported zero negative-treasury countries and a zero bank
interest accumulator. Bankruptcy and comprehensive world-money supply remain
unmapped, so this benchmark does not make claims about either.

## Province candidate snapshots

## Country metric candidates

`country.metrics` reads historical `CCountry` candidates plurality `+0x1a8`,
diplomatic points `+0x65c`, war exhaustion `+0x680`, leadership `+0x7d0`, research
points `+0xe3c`, prestige `+0xea0`, rankings `+0x1404` through `+0x1410`, and
infamy `+0x1430`. Country tags remain the stable entity identity. Clausewitz
scalar candidates use 1/1000 storage; leadership uses 48.15 fixed point.

Vanilla one-day run `f1e91e80-a3fc-41f6-9dc4-45fc2577f068` loaded the unchanged
`benchmark.v2` and selected PRU. Runtime values exactly matched save values for
prestige (`50055` / 1000), plurality (`25059` / 1000), diplomatic points (`5000`
/ 1000), research points (`32603` / 1000), and leadership (`270728` / 32768).
Infamy and war exhaustion were zero, consistent with omitted zero-valued save
keys. Integer rankings were overall 4, military 4, industrial 4, and prestige 5;
these had no direct serialized counterpart and remain provisional candidates.

## Province production candidates

`province.production` reads the historical province construction list at
`+0xd8` and province-building vector at `+0x118`. Vanilla one-day run
`efc1f359-f33a-4105-a2f1-1fba825fe34d` reported three building slots and zero
constructions for Berlin. The save serialized fort and railroad entries but no
naval base, demonstrating that vector length is a definition-slot count rather
than a count of active buildings. Save factories are state-level and use the
separate mapping below. Building metadata is bounded to 64 slots and the
construction list to 4,096 entries; invalid metadata suppresses the record.

## State factories

The supplied vanilla `benchmark.v2` and its Production UI establish that PRU
has four factories in Brandenburg, one in Nordrhein, and two in Schlesien on
1836.1.2. A read-only live-process probe resolved Berlin province 549 to the
Brandenburg `CState` and found a linked list at `CState+0x60` with exactly four
`CStateBuilding` nodes. The other two nonempty Prussian lists contained exactly
one and two nodes. RTTI identifies the node object as `CStateBuilding`; static
constructor RVA `0x000f2bc0` initializes through `+0x218`, and live links begin
at `+0x220`, establishing object size `0x220`.

The four definition keys at `CStateBuilding+0x18` resolved in UI order to
`glass_factory`, `ammunition_factory`, `small_arms_factory`, and `paper_mill`.
Level `+0x20` was one for each. Employee count `+0x128` exactly matched 1,998,
1,698, 1,848, and 2,431. Small Arms output `+0xd8` matched 0.410 on 1836.1.2 and
0.389 on 1836.1.3 after division by 32,768.

The `CPopEmployment` vector at `+0xf0` contains 16-byte records with POP pointer
`+0x8` and assigned count `+0xc`. Resolving each POP's `CPopType` key reproduced
the UI's craftsmen/clerk splits and the serialized save assignments exactly.
The `CGoodsPool` at `+0x28` maps zero-based goods ordinals to 64-bit stockpile
values and exactly reproduced the save's Small Arms ammunition, steel, cement,
and machine-parts values. `CBuilding+0x12c` points to `CProductionType`; output
good `+0x80` and base output `+0x88` match `production_types.txt` and the live
`CGoods` key.

Static callsites identify a second `CGoodsPool` at `CStateBuilding+0x80`.
RVA `0x000dd470` copies the production type's input
recipe, scales it, adds scaled maintenance goods, subtracts the current `+0x28`
stockpile, and clamps at zero. The daily caller at RVA `0x00084060` stores that
result in `+0x80` before market settlement at RVA `0x00082ff0`. Telemetry samples
the retained request after the daily boundary. Belgian partial fulfillment
proves that settlement does not replace it with fulfilled purchases.

The requested-input index table uses zero for an empty in-registry entry. Bytes
beyond the loaded registry are stale (`0xff` for Prussia and `0xee` for Belgium
at ordinal 48), so traversal is bounded by the loaded-goods count at global RVA
`0x00e587f4`; the GFM probe reports 48 goods. Run
`8b355d27-c585-4fb7-8c8f-f79caf33c025` emitted 50 input records over two dates
and accepted all 92 selected factory records without invalid or dropped
results. On the second date, summing `requested_raw * world.market.price` for
each factory reproduced `market_spending_expense_raw` for all seven factories
within 0.000081 currency. Glass differed by 0.000056 from fixed-point rounding.
This fully supplied case did not distinguish requests from fulfilled purchases.

Belgian run `a859d772-6846-4749-9fb6-d12467774133` emitted 123 accepted factory
records over three dates with zero invalid or dropped results. Cement request
value exceeded market spending by 0.09938 and 0.41326 currency on consecutive
dates; Canned Food differed by 0.23857 on the third date. Fully supplied
factories remained within 0.00009. This verifies `+0x80` as requested demand and
disproves using it as fulfilled purchases in a quantity inventory equation.

Valuing both inventory endpoints at the current market price supported an
initial shortage-safe monetary estimate:

```text
intermediate consumption = market spending + opening stock value - closing stock value
factory value added = output value - intermediate consumption
```

Across Belgium's six factories, the two comparable daily results were
110.4296 gross / 94.1747 intermediate / 16.2549 value added and 116.1762 gross /
98.4568 intermediate / 17.7194 value added. All six factory-level value-added
results were positive on both dates. This remains provisional: snapshots must
bracket the same interval as spending, other stock transfers must be absent,
and spending may use transaction prices different from the current prices used
for both inventory endpoints.

Thirty-day Belgian run `3aacdf40-e989-4d52-92c8-f052fa2a78d8` extended this to
29 consecutive intervals and 174 factory-interval results. All results remained
positive through persistent Canned Food, Cement, Steel, Fabric, and Small Arms
shortages. Country daily value added ranged from 16.2549 to 20.7850 and averaged
19.5229. The trace accepted all 1,230 selected factory records with zero invalid
or dropped results. This supports the daily snapshot alignment used by the
offline interval alignment. The mixed transaction/current-price estimate was
subsequently replaced by direct physical consumption measurement.

The daily settlement caller at RVA `0x000845fd` calls settlement routine RVA
`0x00082ff0`. Immediately after production consumption and before purchases,
the factory stock pool is the caller's factory argument plus `0x28`. Primary
and secondary delivered-goods pools are passed to the same `CGoodsPool` add
helper at callsite RVAs `0x0008369d` and `0x000836aa`; the helper is RVA
`0x0007dc20`. The telemetry `flows` group verifies the expected five-byte calls
before replacing them and preserves registers, flags, and the original calls.

Belgian 30-day run `0cff14dc-70fa-4d36-b820-f819e13dcb3e` captured 174 factory
intervals with zero incomplete boundaries, negative consumption quantities, or
stock-flow reconciliation failures. Every closing stock equaled pre-purchase
stock plus primary and secondary deliveries. Direct consumption is therefore
the previous closing stock minus the current post-consumption stock. Its maximum
difference from the earlier spending-plus-inventory estimate was 0.000811603
currency after current-price valuation. Country factory value added averaged
19.522634493 and ranged from 16.255384248 to 20.784684079.

Final renamed-event run `608c1a54-b2f5-41b8-8fcd-83c0382c51d1` emitted complete
`state.factory.input.flow.summary` and `state.factory.input.flow` records. Its
first two intervals produced 16.255384248 and 17.719507277 of direct factory
value added. This establishes the callsites, phase boundaries, and derived
physical consumption as `verified-runtime` for the supported executable.

After queue draining moved to one shared snapshot per observed game date,
flows-only run `b7c6d433-2404-49f7-a12f-738810579949` omitted both finance and
ordinary input snapshots. It emitted 24 complete summaries and 69 flow records
over four sampled dates with zero gaps, drops, or invalid records. The first
date was the expected pre-settlement hook warm-up; the next two complete
intervals exported value added of 17.719507277 and 19.813987492. The hook queue
and date snapshot are bounded to 2,048 records, supporting at most 512 factories
per daily boundary before explicit invalid accounting.

Small Arms finance fields correlated on two dates: budget `+0x150`, market
spending `+0x158`, sales income `+0x160`, paychecks `+0x168`, and investment
`+0x170`. These are nonnegative 64-bit amounts displayed after division by
32,768,000; expense signs are UI presentation. The vanilla define
`FACTORY_PAYCHECKS_LEFTOVER_FACTOR = 0.25` describes distribution from factory
leftovers, while GFM overrides it to 0.3. The wiki does not establish payment
cadence, and telemetry does not infer one.

Factory finance settlement callsite RVA `0x00088710` calls RVA `0x000f4b30`.
Its first argument is the live `CStateBuilding*`; the 64-bit value argument
matches `sales_income +0x160`. At this boundary output is `+0xd8`, closing unsold
output is `+0x1f8`, and the caller local at hook-stack `+0xc4` is opening unsold
output. Run `662343cc-fc1a-4ad9-a95a-4215541612c6` captured 21 factory accounts
with zero invalid records. Their closing inventory chained exactly into the next
opening inventory, including the observed sequence 0, 10,479, 27,007, 42,396,
and 52,866 raw units. This establishes realized sold quantity as
`opening + produced - closing` and proceeds as the call's 64-bit amount.

Small Arms save `injected_money=122750.09155` and `injected_days=7` correlate to
`CStateBuilding+0x1d0` and `+0x1d8`. Four-day run
`a4edf016-5d61-40e0-83e7-37b9df278e2a` balanced every factory budget exactly
from the five emitted finance fields, with no separate injection residual. This
supports deferred injected capital being represented through observed
`last_investment`; exact funding source and tranche rules remain unclaimed.

Controlled paused UI toggles isolated subsidy byte `+0x180` and closed byte
`+0x188`: each changed only from zero to one while the object and list position
remained stable. Closing also reset an unrelated counter at `+0x218`, whose
semantics remain unclaimed. Save correlation identifies `CState+0xc` as the
persistent state ID (Brandenburg 750). The value 47 at `+0x8` is the constant
persistent object-type discriminator, not a state-region ID. Shared region
grouping is provided separately by `CRegion` below.

Factory upgrades create state-level project data through the embedded
`CPopProject` at `CState+0x1c8`; its changing goods state is referenced outside
the inline `CState` bytes. The 730-day Small Arms upgrade matched its active
building definition. Progress, material acquisition, and upgrade availability
remain unavailable pending a same-process project-storage correlation.

Capture bounds state lists to 512, factory lists to 64 per state, output to
4,096 factories, employment to 1,024 assignments per factory, and stockpiles to
16,384 input records per selected country. It validates list links, readable
spans, normalized definition and POP-type keys, assignment totals, goods-pool
indices, and nonnegative observed values. Entity identity uses country tag,
persistent state ID, and factory-definition key; the anchor province remains an
explicit candidate.
Only selected record groups are traversed. Malformed metadata in a selected
group suppresses that selected-country poll and increments family `invalid`.

`CState+0x250` points to a shared `CRegion` object. RTTI and contiguous runtime
instances establish size `0x108`; key `+0x18` and Windows-1252 display name `+0x34` resolve
Brandenburg as `PRU_549` / `Brandenburg`. Owner-specific state instances for a
split region share this pointer and key. The benchmark includes many examples,
including Osthannover portions owned by ENG, BRE, HAM, and HAN. Factory identity
emits the durable region key while country attribution continues to come from
the owning country's state list.

Localized region names are not emitted because the supported English game still
stores names such as `Württemberg` and `Westpreußen` as Windows-1252, while the
telemetry ABI requires UTF-8. The stable ASCII region key avoids lossy or invalid
text until localization transcoding has an explicit contract.

Public OpenVic testing reports that merging split portions reconciles duplicate
factory types, but the exact winner is disputed and behavior above eight
distinct types is not established. No merge semantics are claimed until a
controlled supported-game probe covers duplicate levels/subsidies and overflow.

A controlled supported-game `changeowner` probe transferred Saxony provinces
558/559/560 into Prussia while paused. PRU's existing state 757 and its pointer
survived, SAX state 907 disappeared, and the unchanged level-1 `fabric_factory`
node moved from SAX's list into PRU's list. Region key remained `SAX_558`, and
PRU's province vector became 687/558/559/560 immediately without a daily tick.
This verifies non-duplicate merge attribution and factory-node continuity; it
does not establish duplicate-type winner or over-eight behavior.

A second `SAX_558` fixture built a subsidized level-1 Fabric Factory in PRU state
757 while SAX state 907 retained its unsubsidized level-1 Fabric Factory. Before
merge the nodes were `0x3d3e96c0` (PRU, budget 1000.00) and `0x44615010` (SAX,
budget 931.91). After transferring 558/559/560, PRU still contained exactly its
original node with unchanged subsidy and budget; the SAX node disappeared. This
establishes recipient-wins reconciliation for equal-level duplicate types.
Different-level duplicates and over-eight distinct types remain untested.

## World market

`CGameState+0xbcc` resolves a live `CWorldMarket`. Its mapped market-data prefix
contains `CGoodsPool` fields whose zero-based ordinals and 32,768-scaled values
match the benchmark save byte-for-byte:

| Offset | Save field |
| ---: | --- |
| `+0x008` | `supply_pool` |
| `+0x060` | `last_supply_pool` |
| `+0x120` | `worldmarket_pool` |
| `+0x178` | `demand` |
| `+0x1d0` | `real_demand` |
| `+0x280` | `price_pool` |
| `+0x2d8` | `last_price_history` |
| `+0x434` | `actual_sold` |
| `+0x4f4` | `actual_sold_world` |

Run `51693f9a-19fb-4900-bfb8-3254022e79ae` emitted 192 market records for 48
goods with zero gaps, filters, drops, or invalid records. Small Arms ordinal 1
matched every independently serialized value. These are global market totals;
they do not establish factory-specific sold quantity or country attribution.

The four-day phase run found no stable same-day or one-day relationship between
factory output and `sales_income / price`; ratios ranged from 0.14 to 2.16.
Producer inventory or market-clearing instrumentation is required before
factory-specific sold quantity can be claimed.

## Artisan production

Typed runtime and save correlation establish the artisan economy object at
`CPop+0x1d4`. `CPop+0x0c` is the persistent POP ID. The object contains the
stockpile `CGoodsPool` at `+0x00`, recipe-need pool at `+0x58`, active
`CProductionType*` at `+0xb0`, last spending `+0xb8`, current production factor
`+0xc0`, percent afforded `+0xc8`, domestic/export sold fractions `+0xd0/+0xd8`,
leftover `+0xe0`, throttle `+0xe8`, needs cost `+0xf0`, and production income
`+0xf8`. Quantities and fractions use the 32,768 scale; monetary values use the
32,768,000 scale. `CProductionType+0x80` is the output good and `+0x88` is base
output.

Berlin POP 8845 matched every independently serialized field: ammunition
recipe, current-production raw 3,116, percent afforded 32,768, last spending and
needs cost 1,126,244,000, production income 163,852,000, and input recipe raws
65,536/65,536/163,840. Gross output is the fixed-point product of current
production and base output.

The engine's active recipe coefficients are the intermediate-input quantities
per unit of `current_producing_raw`. Runtime POP 8845 later had production raw
23,565. Multiplying its three coefficients and shifting 15 bits gives consumed
raw quantities 47,130/47,130/117,825. The independent market-boundary trace
showed previous closing stocks 47,132/47,132/117,830 and current residuals
2/2/5, exactly the same consumption. This establishes recipe consumption as
`verified-runtime` without relying on cross-date stock identity when artisans
switch recipes.

The artisan economy's sold fractions `+0xd0/+0xd8`, leftover output `+0xe0`,
and production income `+0xf8` form a separate realized-sales account. Run
`662343cc-fc1a-4ad9-a95a-4215541612c6` emitted 630 settlement summaries and 498
complete quantity/revenue pairs with zero invalid records. The first observation
for each POP is an explicit warm-up because no opening inventory exists; recipe
changes also break the inventory chain rather than joining different output
goods.

The artisan market-settlement caller at RVA `0x00086bff` holds `CPop*` in EBX
and calls RVA `0x00083aa0`. Primary and secondary goods additions occur at RVAs
`0x00083fca` and `0x00083fda`, with `CPop*` retained in EDI and its economy
object in ESI. A three-day country-filtered hook run recorded all four boundaries
for nine Berlin artisans after warm-up, 40 per-good records, and zero gaps,
drops, or invalid records. The hook is observational and bounded to 8,192
records for at most 16 selected country tags.

Callsite RVA `0x00086b81` invokes RVA `0x000dd3c0`, which copies the active
recipe input pool, scales it, and subtracts artisan stock from a temporary
requirement pool through `CGoodsPool` subtraction RVA `0x0007dca0`; it does not
mutate the artisan stock pool. A temporary before/after wrapper confirmed equal
artisan stock on both sides. This disproves treating that call as the physical
stock-consumption boundary and supports using the recipe/current-production
account directly.

## RGO production

Paused UI correlation on Berlin 549 (fruit farm) and Görlitz 687 (coal mine)
establishes `CProvince+0x1ac` as RGO employment capacity: 195,625 and 60,000
respectively. Current employment is derived from farmer/labourer POP employment;
Berlin totals 110,896 and Görlitz 54,730, matching the UI.

The UI output identity is exact for both examples:

```text
Berlin:  11.20 base × 1.90 output efficiency × 0.9703 throughput = 20.648 fruit
Görlitz:  7.20 base × 1.45 output efficiency × 0.5472 throughput = 5.713 coal
```

The component tooltips expose aristocrat-owner, RGO technology, worker, and
infrastructure contributions. Runtime records are an exact 3,249-element vector
rooted at global RVA `0x00e58728`. Accessor RVA `0x000c25b0` returns the vector
object, and static callers index it as `province_id * 0xb0`. Each element has an
eight-byte prefix followed by a live `CStateEmployment` object.

| Record offset | Meaning |
| ---: | --- |
| `+0x08` | `CProductionType*` |
| `+0x0c` | output `CGoods*` |
| `+0x1c` | `CProvince*` identity check |
| `+0x38` | output efficiency, 32,768 scale |
| `+0x40` | throughput, 32,768 scale |
| `+0x58` | employed workers |
| `+0x80` | income, 32,768,000 scale |
| `+0x88` | base size, 32,768 scale |
| `+0x90` | domestic sold fraction, 32,768 scale |
| `+0x98` | export sold fraction, 32,768 scale |
| `+0xa0` | closing leftover output, 32,768 scale |

Berlin's record resolves `orchard` and fruit ordinal 33; Görlitz resolves
`coal_mine` and coal ordinal 10. The record values reproduce both UI efficiency,
throughput, employment, and income displays. Function RVA `0x000de350` multiplies
efficiency, throughput, `CProductionType+0x88` recipe output, and record `+0x88`
base size in that order, shifting right 15 bits after every multiplication. The
derived raw outputs 676,588 and 187,206 display as the exact UI values 20.648
and 5.713, so `province.rgo.production` emits `gross_output_raw`.

The Production UI path at RVA `0x00340f8d` reads `CProvince+0x188 -> CState`,
uses `CProductionType+0xf0 -> +0x28` to select the owner POP type from the
state's `+0x118` population-by-type array, and divides that population by total
state RGO capacity `+0xc8`. Berlin computes 4,275 / 457,875 = raw 305; Görlitz
computes 848 / 60,000 = raw 463. These display as the exact owner contributions
0.0093 and 0.0141.

Injected one-day run `ea77637b-f661-47f5-b85f-18d48de2a5a0` emitted identity,
employment, production, and finance records for both provinces. Its family
summary reported two collection attempts, eight accepted records, and zero
filtered, dropped, or invalid results.

Follow-up run `b08cf98f-3ece-4467-9aec-6afe3284ab14` emitted raw gross outputs
676,588 and 187,206, completed on the exact date, and again reported eight
accepted records with zero invalid or dropped records.

Run `e5f0655c-7a87-4342-a77a-96c2b2d47492` emitted production and separate
modifier records with owner raw values 305 and 463. Its family summary reported
ten accepted records with zero filtered, dropped, or invalid results.

Producer-sales run `662343cc-fc1a-4ad9-a95a-4215541612c6` emitted 230 RGO
settlement summaries and 184 complete quantity/revenue pairs over five dates,
with zero invalid records. Consecutive leftover inventory and gross output
reconciled every complete interval. Domestic and export fractions are retained
as engine evidence, but telemetry does not derive a producer-attributed market
split because their sequential clearing semantics are not independently proven.
Annual diagnostic run `2301bee7-364f-49b7-ba53-dd74dc5fcab0` proved the export
value is not bounded by 32,768: all 349 previously invalid artisan reads were
nonnegative export values from 32,931 through 118,028. Telemetry therefore
retains the raw value but does not label it a percentage.

Final thirty-day run `dc58f6ce-369a-48c3-a1ad-fdae369b0193` produced 5,152 strict
producer rows with zero family invalid records. Post-fix annual run
`e2ae9961-4698-473e-840b-2dace5271dc0` completed 365 days with 385,929 records,
zero sequence gaps, drops, writer failure, or family invalid records. Strict
export produced 63,952 factory, RGO, and artisan accounts. This supersedes the
earlier annual run whose false export-value bound rejected valid artisan states.

Metz province 412 resolves `precious_metal_mine` and precious-metal ordinal 17.
Run `962c2f5b-afb6-4ef2-b29d-eb579c480561` emitted gross output raw 262,128 and
income raw 7,385,056,000 with zero gaps, drops, or invalid records. The income
matches save `last_income=225374.02344` after the save's raw / 32,768 display
scale, but does not match 7.9995 units times vanilla `GOLD_TO_CASH_RATE=0.5`.
Therefore RGO income is not precious-metal cash creation. GDP values precious
metal as gross quantity times the active mod's explicit rate (vanilla 0.5, GFM
1.0), never by the non-tradeable good's market price or RGO income field.

Injected vanilla run `40d36695-696f-408e-af64-df266a1cfcc8` loaded the unchanged
benchmark and emitted four record groups for each of seven PRU factories. The
28 records had zero filtered, dropped, or invalid results and preserved the
independently observed Brandenburg values exactly.

Depth run `734efd9b-7873-4c26-b9aa-660422b3d4ad` emitted 53 accepted records
with zero gaps, filters, drops, or invalid results: seven each for identity,
employment, production, and finance and 25 per-good stockpile records. It
confirmed persistent state IDs 750/753/756 and matched all independently read
Brandenburg employment and Small Arms stockpile values.

## Military aggregate candidates

`country.military` reads the historical country unit list `+0x7b4`, leadership
`+0x7d0`, mobilization byte `+0x120`, military ranking `+0x1408`, and scheduled
mobilizations vector `+0x15dc`. `world.military` reads the ongoing-war list count
at `CGameState+0xb3c`. Vanilla run `970c3cf3-f56f-4f10-aed8-5f153534150a`
reported 12 PRU unit-list entries, no active or scheduled mobilization,
leadership raw 270728, military rank 4, and three ongoing-war list entries.
`CUnit`, `CWar`, combat storage, and durable identities remain unmapped, so no
composition, participant, battle, casualty, or movement semantics are claimed.
Country values are observed from that country's `DailyUpdateEvent`; there is no
additional country traversal. Metadata limits are 100,000 units or scheduled
mobilizations and 4,096 ongoing wars. Invalid list structure or exceeded limits
suppresses the affected record.

## Diplomacy and sphere candidates

`country.diplomacy` reads historical country fields substate/vassal flags
`+0xcf4/+0xcf5`, overlord `+0xcf8`, vassals `+0xd38`, allies `+0xd58`, guarantees
`+0xd78`, neighbors `+0xd88`, spherelings `+0x1418`, and sphere leader `+0x1428`.
Country tags are stable identities; relation objects and influence storage are
not exposed. Vanilla run `70db3477-0e35-4fa6-8ea4-415bc96e7483` observed PRU
with no overlord or sphere leader, 15 spherelings, five allies, zero vassals and
guarantees, and 21 neighbors. These vector semantics remain provisional pending
change-over-time and symmetry probes.
Each relation vector is bounded to 512 entries and candidate tags must contain
three normalized bytes plus a terminator. Invalid metadata suppresses only its
status or relations record and is reported through family `invalid` accounting.

`province.daily` reads the historical `CProvince` candidate fields ID `+0x58`,
owner `+0x128`, controller `+0x130`, colonial level `+0x190`, life rating
`+0x1a4`, and infrastructure `+0x2b8`. These fields remain provisional and are
named `_candidate` or `_candidate_raw` in telemetry.

Vanilla one-day run `dd4c7396-4fa0-4598-9b76-e1d43874d690` loaded the unchanged
`benchmark.v2`. Province 1 matched RUS/RUS, colonial level 2, and life rating
20; province 425 matched FRA/FRA and life rating 40; province 549 matched
PRU/PRU and reported infrastructure raw 160 while the save recorded railroad
level 1. Three records were accepted and one contended record was dropped; the
new `telemetry.family.summary` reported four attempts, three accepted, and one
drop. This validates accounting and several correlations, but does not yet
establish a general infrastructure-to-building-level conversion.

## POP candidate snapshots

`pop.economy` reuses the runtime-verified POP money `+0x180`, savings `+0x250`,
interest cash flow `+0x210`, and total cash flow `+0x218` fields. The bounded POP
walk also reads engine POP ID `+0x0c`, provisional size `+0x58`, employed count `+0x60`, province pointer
`+0x64`, type pointer `+0x68`, consciousness `+0x118`, militancy `+0x120`, and
literacy `+0x128`. Candidate IDs come from province `+0x58` and POP type `+0x28`.
The three rate candidates use the same 48.15 fixed-point scale as other mapped
game values.

Save-correlated runtime scan `952f4ca8-fbfc-4859-8ce6-b18995f30431` selected
POP ID 18,563 from the unchanged `benchmark.v2`. Memory values at `CPop+0x130`,
`+0x138`, and `+0x140` were respectively 32,768, 24,672, and 30. The source save
records `everyday_needs=0.75293` and `luxury_needs=0.00092`; 24,672 / 32,768 is
0.7529296875 and 30 / 32,768 is 0.0009155273. The omitted/default life-needs
value correlates with full satisfaction 32,768. `pop.needs` exposes these three
bounded values as provisional satisfaction candidates, not goods demand or
spending. The temporary broad layout emission used to locate them is not part
of the plugin.

Static constructor evidence at RVAs `0x00554a40` and `0x00554f40` identifies
`CPop+0x6c` as `CCulture*` and `CPop+0x70` as the religion object. Their
normalized definition keys are stored at `CCulture+0x18` and religion `+0x10`;
the latter is distinct from the additional serialized religion string at
`+0x2c`. Clean runtime run `a54302ac-af7a-470d-8c6f-f457b8e14d2b` read POP ID 18,563
as type `clergymen`, culture `polish`, religion `catholic`, exactly matching the
unchanged `benchmark.v2` save. It emitted 449 PRU identity records with zero
family-invalid records, sequence gaps, drops, or writer failures.

A complete line-oriented audit of that save found 23,429 POP records and 23,429
unique serialized IDs. Province + type + culture + religion produced only
22,610 distinct keys: 329 keys were duplicated, all for artisans. Adding the
artisan production type still left 126 duplicated keys. Therefore `pop_id` is
the only mapped individual in-process identity. Type/culture/religion are valid
cohort dimensions, but no combination of those mapped dimensions is claimed as
a universal individual POP key.

Vanilla one-day run `4f40b617-b56a-4478-81b1-9e35b1d90b4e` loaded the unchanged
`benchmark.v2` and selected province 549. Each of `pop.economy` and
`pop.demographics` emitted 23 records with zero filtered, dropped, or invalid
records. The first Berlin POP reported size 4,275, consciousness raw 98,337,
literacy raw 22,938, and militancy zero. These correlate with the save's size
4,275, consciousness 3.00101, literacy 0.70001, and militancy zero after dividing
the rate candidates by 32,768. Candidate POP-type IDs 1 and 2 correlated with
the save's aristocrat and artisan groups in this probe. The same `pop_id` field
now identifies individual economy, demographic, artisan, and cash-flow records.
It remains an in-process engine identity: behavior across promotion, demotion,
split, merge, deletion, save/reload, and ID reuse is not yet established.

`pop.aggregate` groups the same bounded snapshot by candidate province and
POP-type IDs. Its sums are observational and deliberately aggregate across
culture and religion; `pop.identity` exposes those normalized keys separately.

Run `3ac4d510-7dbe-45af-9701-1902379785df` enabled all three POP families for
Berlin on the same date. It accepted 23 economy records, 23 demographic records,
and 10 province/type aggregates with no drops or invalid records. The initial
bounded copy took 90,224 microseconds; the other two rules reused it and reported
zero additional snapshot collection time. Type 2 grouped nine POPs with total
size 60,020; types 7 and 8 grouped four and three POPs respectively.

Three-day PRU run `081308fc-fdd5-4bf4-ba62-913b60da54f8` replaced the former
snapshot-local individual index with engine `pop_id` and exercised country-only
filters for both individual families. It emitted 1,348 economy, 1,348
demographic, and 709 aggregate records with zero gaps, drops, writer failures,
or family invalid records. Every economy date/ID key had one matching
demographic key. The first two dates each had 449 unique IDs; the third had 450.
The original 449 appeared on all three dates, establishing consecutive-observation
stability but not yet the semantics of the newly appearing ID or any lifecycle
transition.

Three-day lifecycle run `39d37341-e686-4094-bb8a-e24ba5f104d7` reconciled the
complete 19,996-POP world stock while filtering detail records to PRU. After the
warm-up date, consecutive observations reported `+40/-6` and `+43/-6`; PRU
detail included observed appearance of POP ID 27,654. All 3,453 trace records
were valid, with zero sequence gaps, drops, writer failures, or lifecycle-family
invalid records. These remain observed stock changes, not classified births,
deaths, migrations, promotions, splits, or merges.

Targeted 17-day run `719679d6-4d1a-439d-937b-b8104c3164e3` established one
migration-like identity correlation. FRA POP `27606` was French, Catholic, craftsmen,
type candidate 7, size 65, and remained in province 412 through raw date
59883744. On 59883768 it disappeared while POP `28227` appeared in province
420 with the exact same type, culture, religion, and size. On 59883792 that ID
disappeared while POP `28266` appeared in province 458 with the same complete
cohort identity and size. Thus these location transitions replaced `pop_id`
rather than retaining it as a `scope_changed` event; both moves conserved all
65 observed people exactly. The native migration mutation boundary was not
instrumented, so this evidence does not classify every matched pair as migration.

The run completed its exact target with 200,010 records, zero sequence gaps,
drops, family-invalid records, or writer failure. The strict POP stock/lifecycle
export accepted all 17 daily stocks and reconciliations. The source save retained
SHA-256 `f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

One-day needs run `dc0c5f46-abab-4c00-8143-7cdc32712c99` emitted 449 PRU
`pop.needs` records and the lifecycle warm-up summary. The complete 1,632-record
trace had zero sequence gaps, drops, writer failures, or family invalid records;
all emitted satisfaction candidates remained within zero through 32,768. The
temporary broad layout event was absent. Needs reads use a separate bounded
reader, so rejection of a provisional value does not invalidate the shared POP
snapshot used by established families.

## POP cash-flow accounting

`CPop::GiveMoney` at RVA `0x0055a5f0` is the verified mutation boundary. Seven
direct callers establish `EAX=CPop*`, `ESI=cash-flow index`, a stack-passed
64-bit amount, and `ret 8`. A reversible `+1000/-1000` runtime fixture verified
the money field at `+0x180`, indexed cash flow at `+0x1d8`, total cash flow at
`+0x218`, and exact restoration. Presentation callsites establish indices
`0 needs`, `1 welfare`, `2 salary`, `3 expenses`, `4 events`, `5 projects`,
`6 bank`, and `7 interest`.

The observational hook records both the posted amount and the actual pre/post
money delta, so engine clamping remains visible. Final run
`f442f9c0-b2fc-47f6-9ad7-05ddfba38243` exercised the hook and strict exporter
for five days and emitted 7,598 records with zero sequence gaps, drops, writer
failures, or family invalid records. Earlier diagnostic run
`eade1c13-e28f-4113-97b3-07410ece7f74` found exact reconciliation for 1,765 of
1,799 opening-seen individual accounts. The 34 residuals formed equal-and-
opposite redistribution evidence, and candidate POP-type residuals canceled at
country scope. Promotion, demotion, split, merge, and direct redistribution are
not proven to pass through `GiveMoney`; individual and type residuals therefore
remain explicit rather than invalidating otherwise complete hook capture.

No verified field or callsite independently attributes POP taxes or tariffs.
They must not be inferred from the broader needs or expenses indices.

## Final batched paired ten-year observer benchmark

The run IDs and `interest_fix` labels below predate the plugin's rename to
`interest_bug_fix` and are retained as historical artifact identities.

Consecutive runs on 2026-08-01 used final commit `d8458bd`, the same unmodified
`benchmark.v2`, observer mode, speed 5, 3,650 days, a 1,800-second safety
timeout, 30-day economic sampling, queue capacity 8,192, and country filter
`ZZZ`. Both advanced from raw date `59883384` to exact target `59970984`,
remained paused and responsive, and preserved source-save SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

| Variant | Run ID | Trace SHA-256 | Records | Gaps/drops/write failure |
| --- | --- | --- | ---: | --- |
| Baseline | `3cca7928-811d-4819-a431-7a4baf960ab4` | `a8369aee355d6d61e63346486b9de85d9359cfa13733781ed110f3849816621c` | 4,270 | `0 / 0 / false` |
| `interest_fix` | `c99a16f5-41e3-40ef-8f75-f6cc835f3aee` | `0acbe58e187c5d4950ec03908294d8b653c678b6faf0e373762c6588f7e7b7fc` | 11,571 | `0 / 0 / false` |

Each trace contains 122 health, capacity, holdings, and credit snapshots. All
244 health records have `complete=true` and zero snapshot, probe, and credit
flags. Capacity remained below every fixed bound:

| Peak or cost | Baseline | `interest_fix` | Limit |
| --- | ---: | ---: | ---: |
| Countries | 271 (52.92%) | 271 (52.92%) | 512 |
| Provinces | 2,360 (57.61%) | 2,348 (57.32%) | 4,096 |
| POPs | 23,059 (23.05%) | 23,074 (23.07%) | 100,000 |
| Snapshot collection, median | 28,950 us | 29,205 us | n/a |
| Snapshot collection, maximum | 35,310 us | 34,712 us | n/a |

The benchmark's process counters produced this paired performance result:

| Measure | Baseline | `interest_fix` | Observed ratio/delta |
| --- | ---: | ---: | ---: |
| Benchmark elapsed | 222.658 s | 314.257 s | 1.411x |
| Game days/second | 16.3928 | 11.6147 | 0.709x |
| Process CPU | 294.672 s | 389.734 s | 1.323x |
| Peak working set | 1,473,703,936 | 1,479,155,712 | +5,451,776 bytes |
| End working set | 1,381,883,904 | 1,393,106,944 | +11,223,040 bytes |
| End private bytes | 1,590,743,040 | 1,604,603,904 | +13,860,864 bytes |

The fix reported 72.554 seconds inside its recipient-processing callbacks,
79.2% of the 91.599-second wall-time increase. This is an opt-in gameplay fix
whose daily country sampling, bounded POP traversal, exact allocation, mutation,
and postcondition checks remain material. Daily recipient batching reduced the
historical pre-batching slowdown from 8.40x elapsed time to 1.41x on this
fixture; this is not a claim about other machines, saves, or mods.

The fix emitted 3,650 aggregate health/value pairs. Every named transfer was
paid exactly: aggregate destination-bank gain `2,003,503,700`, domestic `343,963,354`, foreign
`1,659,540,346`, and POP payout `2,003,503,700,000`. There were zero rejected
debtors, failed recipients, result-queue drops, or postcondition failures.

Final sampled economic observations differed as follows. These remain separate
raw categories from divergent simulations, not an additive money-supply
identity or causal decomposition:

| Final field | Baseline | `interest_fix` | Difference |
| --- | ---: | ---: | ---: |
| `treasury_observed_raw` | 99,030,403,012 | 106,278,163,339 | +7,247,760,327 |
| `pop_money_observed_raw` | 28,056,708,337,586 | 31,425,196,653,994 | +3,368,488,316,408 |
| `pop_savings_observed_raw` | 125,012,634,775,634 | 144,892,903,609,587 | +19,880,268,833,953 |
| `creditor_count` | 492 | 622 | +130 |
| `creditor_debt_candidate_raw` | 2,629,676,588 | 3,551,628,691 | +921,952,103 |
| `state_interest_candidate_raw` | 1,438,307,103 | 1,850,768,026 | +412,460,923 |

Both final samples reported zero negative-treasury countries and a zero bank
interest accumulator. Bankruptcy and comprehensive world-money supply remain
unmapped, so this benchmark does not make claims about either.
