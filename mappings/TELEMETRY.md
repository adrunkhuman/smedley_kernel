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
