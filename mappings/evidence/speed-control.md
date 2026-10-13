# Speed control

These findings target the executable identified in `v2game-3.04.toml`.

## Native speed

`CGameState+0xb28` is the current speed index. The native speed-up and
speed-down message handlers clamp it to `0..4`; index `4` is displayed as speed
5. Writing index `5` is unsafe because consumers use the value directly as a
five-element table index.

The topbar binds `button_speedup` and `button_speeddown` to callbacks at RVAs
`0x318ce0` and `0x318cf0`. Those callbacks invoke `CInGameIdler` virtual slots
`+0x164` and `+0x168`, whose runtime targets are RVAs `0x26a0d0` and
`0x26a1b0`. Their message handlers at RVAs `0x32ee90` and `0x32efe0` update the
speed index.

The kernel-owned game-state implementation checks the invariant handler bodies after the
ASLR-relocated global pointer before calling either message handler. It reads
`CGameState+0xb28` after every call and rejects a transition that does not move
exactly one index toward the requested profile speed. Both directions are
`verified-runtime`; speed selection and pause state are separate controls.

## Daily pacing

RVA `0x282bd0` selects these thresholds before advancing the simulation date:

| Speed index | Displayed speed | Threshold |
| ---: | ---: | ---: |
| 0 | 1 | 4.0 s |
| 1 | 2 | 2.0 s |
| 2 | 3 | 1.0 s |
| 3 | 4 | 0.5 s |
| 4 | 5 | 0.0001 s |

Speed 5 is already effectively unpaced. A threshold of `0.000001` has no
measured throughput advantage over the stock value.

## Intra-day servicing

RVA `0x285620` is called at checkpoints within long daily country, POP, unit,
and economy loops. It uses the five floats at preferred VA `0x00f0956c`
(`0.03, 0.03, 0.03, 0.04, 0.06`) to decide when to call the application pump at
RVA `0x5df2b0`. That pump services Windows messages and render-facing
application interfaces. It is an intra-day responsiveness interval, not a
delay between days.

Very low intervals increase application-pump work rather than accelerate daily
simulation. At interval `0`, a light Jan Mayen campaign slows from about
18 days/s to about 0.5 days/s because the pump runs at nearly every checkpoint.
Intervals `0.02` through `100.0` have no established acceleration beyond normal
variance in light and heavy-mod workloads. Keep the stock `0.06`.

Minimization selects a background-throttled application path. Throughput can
roughly halve in a light campaign; keep the game visible for unattended runs.
These figures are workload-specific, not general performance bounds.
