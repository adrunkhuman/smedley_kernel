# Telemetry mapping evidence

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
in `INTEREST_FIX.md`. It scans country ordinals 1 through `country_count - 1`
once on the first daily callback for each telemetry-selected sample date,
retaining no raw pointers afterward. Complete snapshots require zero traversal
flags and unique POP identities across every country.

| Field family | Source | Evidence and interpretation |
| --- | --- | --- |
| `treasury_observed_raw` | `CCountry+0xe78` | Provisional world sum of readable country-slot treasuries. Interest-boundary deltas are independently runtime verified. |
| `pop_money_observed_raw` | bounded POP `+0x180` | Narrow field behavior is runtime verified by `CPop::GiveMoney`; completeness of the world traversal remains provisional. |
| `pop_savings_observed_raw` | bounded POP `+0x250` | Runtime-verified savings storage and `1000:1` relation to state scale; it is a deposit/claim category, not liquid POP money. |
| `bank_interest_accumulator_raw` | each country bank `+0x20` | Runtime-verified temporary interest destination, not bank cash. |
| creditor counts and `was_paid` | bounded `CCreditor` vectors | Structure and paid byte are runtime verified. The interpretations of `+0x10` and `+0x18` remain provisional. |
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

Small Arms finance fields correlated on two dates: budget `+0x150`, market
spending `+0x158`, sales income `+0x160`, paychecks `+0x168`, and investment
`+0x170`. These are nonnegative 64-bit amounts displayed after division by
32,768,000; expense signs are UI presentation. The vanilla define
`FACTORY_PAYCHECKS_LEFTOVER_FACTOR = 0.25` describes distribution from factory
leftovers, while GFM overrides it to 0.3. The wiki does not establish payment
cadence, and telemetry does not infer one.

Capture bounds state lists to 512, factory lists to 64 per state, and output to
4,096 factories per selected country. It validates list links, readable spans,
normalized definition keys, and nonnegative observed values. Entity identity
uses country tag, factory-definition key, and the first state province as an
explicit candidate; state splitting and durable state-region identity remain
unmapped.

Injected vanilla run `40d36695-696f-408e-af64-df266a1cfcc8` loaded the unchanged
benchmark and emitted four record groups for each of seven PRU factories. The
28 records had zero filtered, dropped, or invalid results and preserved the
independently observed Brandenburg values exactly.

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
walk also reads provisional size `+0x58`, employed count `+0x60`, province pointer
`+0x64`, type pointer `+0x68`, consciousness `+0x118`, militancy `+0x120`, and
literacy `+0x128`. Candidate IDs come from province `+0x58` and POP type `+0x28`.
The three rate candidates use the same 48.15 fixed-point scale as other mapped
game values.

Vanilla one-day run `4f40b617-b56a-4478-81b1-9e35b1d90b4e` loaded the unchanged
`benchmark.v2` and selected province 549. Each of `pop.economy` and
`pop.demographics` emitted 23 records with zero filtered, dropped, or invalid
records. The first Berlin POP reported size 4,275, consciousness raw 98,337,
literacy raw 22,938, and militancy zero. These correlate with the save's size
4,275, consciousness 3.00101, literacy 0.70001, and militancy zero after dividing
the rate candidates by 32,768. Candidate POP-type IDs 1 and 2 correlated with
the save's aristocrat and artisan groups in this probe, but names and durable
per-POP identities remain unmapped.

`pop.aggregate` groups the same bounded snapshot by candidate province and
POP-type IDs. Its sums are observational and deliberately omit culture and
religion until stable identifiers for those dimensions are mapped.

Run `3ac4d510-7dbe-45af-9701-1902379785df` enabled all three POP families for
Berlin on the same date. It accepted 23 economy records, 23 demographic records,
and 10 province/type aggregates with no drops or invalid records. The initial
bounded copy took 90,224 microseconds; the other two rules reused it and reported
zero additional snapshot collection time. Type 2 grouped nine POPs with total
size 60,020; types 7 and 8 grouped four and three POPs respectively.

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

The fix emitted 3,650 aggregate health/value pairs plus one zero-transfer
treasury-mismatch warning. Every named transfer was paid exactly: aggregate
destination-bank gain `2,003,503,700`, domestic `343,963,354`, foreign
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
