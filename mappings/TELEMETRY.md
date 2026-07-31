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
