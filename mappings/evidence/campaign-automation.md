# Campaign automation

Native frontend entry, full-AI observer mode, speed/pause control, bounded-date
runs, and native exit are `verified-runtime` on the supported executable.
Automation dispatches Victoria II's own GUI signals, not synthetic input.
See [launcher controls](../../docs/launcher.md) for usage,
[service boundaries](../../docs/architecture.md) for ownership, and the
[mapping catalog](../v2game-3.04.toml) for signatures.

## Sequence

All addresses below are RVAs unless marked as object offsets. Runtime checks
establish identity, captured constructor thread, readable metadata, and
postconditions before advancing to the next phase.

| Step | Verified boundary and contract |
| --- | --- |
| Capture frontend | Suspended launch initializes `campaign_runner` before resume. Main-menu constructor `0x354a00` and Single Player constructor `0x36a2f0` capture controllers with primary vtables `0xa13dbc` and `0xa14ed0`. Scalar deleting destructors `0x354df0` and `0x36b030` invalidate captures before storage release. |
| Enter lobby | Ten seconds after Single Player capture, resolve `mainmenu_panel/single_player_button` through main-controller registry `+0x704`. Native update `0x354f90` independently uses registry slot `+0x6c` and returned-panel slot `+0x34`. Press/release entries `0x5ee510` / `0x5ee550` dispatch the signal. |
| Select save | Confirm basename at lobby `+0x590`; construct only from canonical empty inline-string state. Reject malformed metadata or a different existing name. Require valid registry/lookup target, write `+0x5bc=1`, `+0x5bd=0`, and read back both. |
| Load and play | Normal lobby update calls `CCurrentGameState::LoadSave` at callsite `0x36f8b3`, completes its post-load contract, clears `+0x5bc`, and sets `+0x5bd`. Resolve `play_button` through lobby registry `+0x278`, dispatch press/release, and verify `CGameState+0xb24` RTTI changes from `CFrontEnd` to `CInGameIdler`. |
| Restore AI | Native return-to-AI `0x287a70` clears human control and constructs/registers `CCountryAI`. Require every `CGameState+0xaec` player-control entry zero, former player `CCountry+0x208` AI pointer non-null, and scheduler growth. |
| Reveal map | Registered native `debug` command with argument `fow`; require global byte `0xb092fb=0`. |
| Set speed/pause | Native up/down handlers must reach `CGameState+0xb28 = selected speed - 1`, with readback after every call. The retained down probe selected speed 2 from a higher speed and checked every decrement. `CInGameIdler::TogglePause` at `0x26a2c0` must produce the requested `+0x1538` state. |

## Current boundary

The kernel owns frontend captures, generations, vtables, thread checks, native
calls, console capture/replacement, annex/message trampolines, and copied
observations. The runner receives opaque generation-bound capabilities and
copied names, tags, counts, and flags; malformed, stale, released, unsupported,
or cross-thread state fails closed before lookup or mutation. Constructor,
destructor, annex, and message hooks install transactionally with reverse-order
rollback. Captures clear at phase transition and shutdown. Modules remain loaded
for process lifetime; without thread quiescence, shutdown makes callbacks inert
rather than rewriting live hook bytes.

Observer mode keeps a country tag only as a UI perspective: Victoria II has no
native spectator/null-country UI. Human ownership is the `player_nations` map;
AI participation is `CCountry::_ai` plus scheduler membership. A `benchmark.v2`
probe returned JAN to AI, grew the scheduler from 271 to 272, verified zero human
entries and FOW zero, and advanced at speed 5. Failed AI/FOW checks leave the
campaign paused; observer mode cannot start paused.

| Observer operation | Boundary and evidence |
| --- | --- |
| Annex handoff | `CCountry::Annex` entry `0x118620`, before player-tag capture or province removal, selects a living scheduled-AI country and changes only `CGameState+0xb5c`. Require zero human entries, scheduled target AI, and unchanged scheduler count. Timer-driven safe switch remains the fallback for other disappearances. |
| Manual switch | Observer-only native `tag` replacement rejects direct use. Registered `switch TAG` pauses, calls the saved asynchronous handler, waits for ownership transition, restores target AI and exact scheduler count, then resumes. Native `tag` is unchanged outside observer mode. `--view-tag` dispatches the same transaction after watchdog start. |
| Popup suppression | Nine signature-checked generic dispatchers bypass visible popup and `CPauseGame` construction regardless of `messagetypes.txt`. Technology, invention, upper-house, bankruptcy, diplomatic/Great War notifications are covered; message production, effects, AI choices, logs, map notices, and icons remain intact. |

| Runtime evidence | Result |
| --- | --- |
| `bac0c2f2-94a5-403d-a836-47a5689a5bcc`, deterministic `ENG = { inherit = THIS }` fixture | JAN observer verified; view moved to ENG inside Annex entry, no game over, `CInGameIdler` continued beyond April 1837. Earlier native `tag` attempt proved asynchronous and unsuitable at that boundary. |
| Initial `--view-tag ENG` acceptance | JAN → ENG; AI and exact scheduler restored one second later; simulation resumed; subsequent `tag FRA` rejected. |
| `b14dc611-cfd0-4b15-ae47-bfd3473cbf5e` | Runtime-owned console callback completed ENG switch/AI restoration and suppressed 15 generic popups; responsive process, unchanged vanilla source hash below. |
| Message-coverage probes | Six dispatches bypassed over about 3,085 advancing days without watchdog pause; after all-nine coverage, six policy-marked popups suppressed while 385,895 country rows advanced without pause. |
| `01edd2fb-4c64-468b-8db9-efeefecc7b05` | Checked frontend/console/observer/annex/message/pause/speed/quit integration: JAN AI, full map, exact three-day target `59883456`, telemetry drain and native exit; zero gaps, drops, or writer failure. |

These runs validate exercised mappings and integration, not a broader UI-thread
contract. Extraction into kernel services and synthetic fail-closed tests do not
add engine evidence.

## Fixed-date benchmark

One game day is runtime-verified as 24 raw date units. Relative targets add
`N * 24` in int64 to the copied date after speed/unpause and optional observer
postconditions; absolute targets must advance and align to that scale.
A Win32 `SetTimer` at `USER_TIMER_MINIMUM` (effectively 10 ms), not a daily hook,
consumes checked date, pause, typed game-over state, and observer invariants.
The callback must match the frontend constructor thread; that correlation does
not prove general engine UI-thread ownership.

| Terminal condition | Contract |
| --- | --- |
| Exact target | Toggle pause, then require a later stable paused observation. This extra UI cycle lets the native endgame dialog publish before success is declared. |
| Game over | Takes precedence over target completion and pause recovery; emits failure reason `game_over`. |
| Other failure | Overshoot, timeout, regression, invalid idler/pause, observer failure, invalid target, or unavailable timer is terminal. Attempt pause/readback; never claim paused when readback fails or invent unavailable progress counters. |
| All terminal paths | Stop monitoring/popup suppression and cancel timer; leave safely paused/open when readback succeeds. Never write a save. Only exact success or typed game over may request explicitly enabled native quit. |
| Pre-exit drain | One five-second deadline for optional telemetry. Completed drain stops ingress, consumes accepted payloads, joins writer, writes enabled summary, flushes/closes. Absent/inactive/legacy sink is unavailable and permits exit without summary/durability guarantee. Busy, timeout, or failure leaves paused/open. |
| Native quit | Validate idler RTTI, exact vtable target, and request-flag readback. Accepted `QUIT` confirmation `0x276500` dispatches primary vtable slot `+0x110` to `0x24edb0`, which sets idler `+0x1d20=1`. No external termination or synthetic input. |

### Game-over mapping

Require supported bytes at `0x25548d`, current `CInGameIdler`, dialog pointer
`+0x1d6c` with window vtable `0xa3c0d8`, visibility byte `+0x67` exactly one,
and `CGameState+0xb0c > [RVA 0xe586dc]+0xc`. Missing or mismatched state makes
the entire observation unavailable, not an ordinary pause.

The August 21, 2026 comparison used the supported executable and unmodified
`benchmark_1936_interest_probe.v2`: visible game over had date `60759360`,
configured end `60759336`, pause one, dialog visible. Ordinary pause had date
`59883384`, the same end/vtable, pause one, and dialog visibility zero.

### Acceptance inputs

| Input | Identity / reproduction settings |
| --- | --- |
| Executable / mapping | SHA-256 `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648`; `v2game-3.04` |
| Vanilla `benchmark.v2` | SHA-256 `f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`; unchanged across recorded runs |
| End-date fixture | SHA-256 `592c41f7258c1b383e796a2cbe9829d0159f1eb898fd79dedf960fab5b66a1d9`; unchanged |
| Common settings | No mods, native speed 5, `campaign_runner` and `telemetry` unless noted; observer specified per row. Launcher flags/profiles: [launcher](../../docs/launcher.md). |

### Bounded-run evidence

| Date / run ID | Settings and result |
| --- | --- |
| July 31, 2026: `a53ae91b-5e1b-4a52-9422-c63c65d52643`, `5a0134ce-2125-4da6-a59e-9b0dc7e8f551` | Vanilla observer, 365 days: `59883384` → target/actual `59892144`, one start/completed record each, paused and responsive/open. Each trace: 374 ordered records, no gaps/drops/write failure; 98,644 daily callbacks. Wall 21,124,677 / 21,230,817 us; 17.28 / 17.19 days/s, 0.50% repeat difference. Establishes pause harness, not final flush/save/replay equivalence. |
| August 1, 2026: `21b024dd-ec69-4d46-a22e-1d309ab83b21` | Detached one-day target, native request readback and exit without `ExitProcess`, `TerminateProcess`, `WM_CLOSE`, or synthetic input. Predates drain; process exit only. |
| August 1, 2026: `4d3d4e44-b4e5-4d7f-b10a-b50bd5b96cfb` | Detached one-day target, bundled lifecycle telemetry: drain logged before native exit. Valid 11-record trace: completed sequence 10, final newline-terminated summary 11; accepted/written 10, dropped 0, no writer failure. Covers completed drain, not timeout retry/legacy compatibility. |
| Final-DLL: `31a41204-121e-4e11-be5d-6ae3fb47f771` | Non-observer absolute target, exact 10 days to `59883624`, paused readback. |
| Final-DLL: `e1c015df-5b89-4f08-9925-90c49214f924` | One-second timeout, failed at 1,013,936 us/date `59883864`, reason `timeout`, paused readback. |
| Final-DLL: `866c652a-fbef-49f5-91e1-b76bcadc0e81` | Nonadvancing start-date target: standalone `invalid_target`, remained paused, no fabricated progress counters. All three final-DLL traces: zero gaps; available counters zero drops/write failure; unchanged source. |
| August 21, 2026: `e17a2ecb-64b7-4177-b16f-1de9c36d5315` | End-date fixture, one day `60759336` → `60759360`: one `benchmark.failed`, `game_over`, paused true; no gaps. Process left open, end-date screen manually responsive. |
| August 21, 2026: `d7ebea5f-ea81-4b59-883d-fb5b839cdf3d` | Same typed terminal result with `quit_after_run`: drain through final sequence 10, dropped 0, no write failure, native exit without external termination. |
| August 21, 2026: `5cc040f5-11a4-4d3b-999b-e38546c1e9d6` | Ordinary vanilla one-day regression target `59883408`, paused true, responsive map left open; no false game over or lingering observer/configuration recovery. |

### Resource measurements

The resource-profiling build repeated the vanilla 365-day observer interval with
one Windows process snapshot before the terminal record. Each trace had 375
ordered records, exactly one `benchmark.resources`, no gaps/drops/write failure,
and unchanged source save.

| Run | Wall us | Process CPU us | CPU / wall | Working set start → end | Private bytes start → end | Peak working set |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `c835ced2-9140-4acb-a656-648795a77de3` | 20,874,288 | 27,500,000 | 131.741% | 1,338,437,632 → 1,360,338,944 | 1,540,493,312 → 1,564,241,920 | 1,372,725,248 |
| `8b259c50-5cf7-476a-a634-95b87b152c9a` | 21,133,183 | 31,734,375 | 150.164% | 1,362,038,784 → 1,361,985,536 | 1,565,822,976 → 1,568,247,808 | 1,373,532,160 |

Throughput was 17.49 / 17.27 days/s (1.23% repeat difference); CPU differed
14.30%. Two samples do not bound CPU variance for optimization claims.
CPU/wall exceeds 100% because Windows sums CPU across process threads.

## Lifecycle Telemetry

With an active optional sink, copied C-ABI lifecycle records follow confirmed
save name/flag writes, load flag `+0x5bd`, RTTI entry, observer postconditions,
speed and pause readback. `observer.configured` waits for a valid view after
requested switching. Tags are three normalized uppercase ASCII alphanumeric
characters, including dynamic tags such as `D01`. See
[telemetry](../../docs/telemetry/README.md) for the record contract.

| Acceptance | Evidence |
| --- | --- |
| July 31, 2026: `f2d403e1-82f8-46b8-8832-a136c822d38a` | Vanilla, observer ENG, speed 5, lifecycle/state capture. Ordered save selection → load complete → `CInGameIdler` → speed 5 → unpaused → ENG observer postconditions. Advanced 515 days; 1,039 ordered records, no gaps/drops/write failure or runner telemetry warning. Verifies emission points, not general timer-thread ownership. |
| August 3, 2026: `bab8f3b3-e9dd-4eca-8c63-ac391a738637` | Vanilla, runner only, speed 5, one day. Exact controller vtables; Play followed by frontend `0x36b030` and main-menu `0x354df0` destructor phase-release observations before console initialization; unpause and bounded native exit. Source unchanged. |
| August 3, 2026: `a2221287-f1f7-4dbb-95c2-d53aa50aa92d` | Unmodded compatibility: 293 valid records. |
| August 3, 2026: `0a1415cd-acd8-4962-867d-f2479028b753` | Observer compatibility: 294 valid records including `observer.configured`. |
| August 3, 2026: `df59cb6d-e112-43c3-8134-916519d26fea` | GFM compatibility: 672 valid records; descriptor `mod/GFM.mod`, name `Greater Flavor Mod`, no declared version; native GFM save SHA-256 `276a53c07afc713bb685ded48c64f6a8c5fa13c4539cc70a380fd4347c4f0dad`. |

The three compatibility runs used the common executable/mapping, both plugins,
speed 5, one day, and quit after success. Each exercised controller-vtable and
bounded string checks, save-flag pre/postconditions, destructor invalidation,
and GUI dispatch; each advanced exactly one day and exited natively with zero
gaps/drops/write failure. Vanilla and GFM source hashes stayed unchanged.
Destructor logging was removed after correlation; callbacks only invalidate.

Earlier GFM probes `2ee97720-b85b-4616-9749-b59ad9e04e90` and
`26e16827-a6a2-45e1-84f9-963ce6f80460` used a copied vanilla save and never
reached `CInGameIdler`. They prove only descriptor preflight resolved
`user_dir = "GFM"`, not campaign compatibility.

## Limits

No native return-to-menu operation is mapped; repeated loads in one process are
outside acceptance. Native exit has no generic pre-exit plugin notification,
checkpoint save, or final game-state assertion. Drain guarantees apply only to
the exercised bundled path. Run IDs and source hashes identify historical
acceptance, but exact binary hashes were not recorded here; they are not proof
that every later build repeats those results.
