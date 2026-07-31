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
