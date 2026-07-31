# Telemetry Mapping Evidence

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

`economic_telemetry` reuses the bounded state/province/POP traversal documented
in `INTEREST_FIX.md`. It scans country ordinals 1 through the current slot count
at the first daily callback selected by telemetry sampling, retaining no raw
pointers afterward. Complete snapshots require zero traversal flags and unique
POP identities across every country.

| Field family | Source | Evidence and interpretation |
| --- | --- | --- |
| `treasury_observed_raw` | `CCountry+0xe78` | Provisional world sum of readable country-slot treasuries. Interest-boundary deltas are independently runtime verified. |
| `pop_money_observed_raw` | bounded POP `+0x180` | Narrow field behavior is runtime verified by `CPop::GiveMoney`; completeness of the world traversal remains provisional. |
| `pop_savings_observed_raw` | bounded POP `+0x250` | Runtime-verified savings storage and `1000:1` relation to state scale; it is a deposit/claim category, not liquid POP money. |
| `bank_interest_accumulator_raw` | each country bank `+0x20` | Runtime-verified temporary interest destination, not bank cash. |
| creditor counts and `was_paid` | bounded `CCreditor` vectors | Structure and paid byte are runtime verified. `+0x10`/`+0x18` names remain candidates. |
| state `+0x258`/`+0x260` | bounded country state lists | Savings correlation is runtime verified with rounding drift; `+0x260` is nonconserved and provisional. |

The plugin emits observed components separately and makes no additive
money-supply claim. In particular, treasuries, POP money, POP savings, creditor
claims, state aggregates, and the bank interest accumulator can represent
different sides or phases of the same economic value.

Hard limits are 512 scanned countries, 4,096 resolved provinces, and 100,000
POP records per snapshot. An incomplete scan emits only
`world.economy.health`; apparently plausible partial holdings, credit, or
capacity values are suppressed.

Observer smoke run `adfe8a43-413c-448a-b68b-3fd58f001723` completed two exact
days with two complete snapshots: 271 countries, 597 states, 2,311 provinces,
and 19,996 then 20,030 POP records. Collection cost was 92,611 and 90,725
microseconds. All eight economic records were accepted with zero sequence gaps,
drops, or writer failure, and the source save remained unchanged.
