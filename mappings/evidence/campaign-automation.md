# Campaign automation

Native frontend entry, full-AI observer mode, speed/pause control, bounded-date
execution, and native exit are `verified-runtime` on the supported executable,
within the boundaries below. Automation dispatches Victoria II's own GUI
signals, not synthetic input. See [launcher controls](../../docs/launcher.md)
for usage, [service boundaries](../../docs/architecture.md) for current
ownership, and the [mapping catalog](../v2game-3.04.toml) for signatures.

All addresses below are RVAs unless marked as object offsets. Each operation
requires the exact executable, applicable signatures, valid object identity,
readable metadata, and the captured frontend constructor thread. Thread
correlation at these boundaries does not establish general UI-thread authority.

## Frontend lifecycle

| Boundary | Contract |
| --- | --- |
| Controller capture | Main-menu constructor `0x354a00` and Single Player constructor `0x36a2f0` identify controllers with primary vtables `0xa13dbc` and `0xa14ed0`. Scalar deleting destructors `0x354df0` and `0x36b030` invalidate captures before storage release. |
| Enter lobby | Resolve `mainmenu_panel/single_player_button` through main-controller registry `+0x704`. Native update `0x354f90` independently uses registry slot `+0x6c` and returned-panel slot `+0x34`. Press/release entries `0x5ee510` / `0x5ee550` dispatch the signal. |
| Select save | Confirm basename at lobby `+0x590`; construct only from canonical empty inline-string state. Reject malformed metadata or a different existing name. Require valid registry/lookup target, write `+0x5bc=1`, `+0x5bd=0`, and read back both. |
| Load and play | Normal lobby update calls `CCurrentGameState::LoadSave` at callsite `0x36f8b3`, completes its post-load contract, clears `+0x5bc`, and sets `+0x5bd`. Resolve `play_button` through lobby registry `+0x278`, dispatch press/release, and verify `CGameState+0xb24` RTTI changes from `CFrontEnd` to `CInGameIdler`. |

Controller capabilities are generation-bound. Malformed, stale, released,
unsupported, or cross-thread state fails before lookup or mutation. Captures
clear at phase transition and shutdown; destructor callbacks invalidate without
retaining access to released storage. Hook callbacks and trampolines remain
process-lifetime where thread quiescence is unavailable; deactivation makes
callbacks inert rather than freeing reachable executable memory.

## Observer ownership and view

Victoria II has no native spectator/null-country UI. Observer mode retains a
country tag only as a viewing perspective. Human ownership is the
`CGameState+0xaec` player-control map; AI participation requires both
`CCountry+0x208` and scheduler membership. A view tag alone proves neither.

| Operation | Contract |
| --- | --- |
| Return country to AI | Native return-to-AI `0x287a70` clears human control and constructs/registers `CCountryAI`. Require every player-control entry zero, former player AI pointer non-null, and scheduler growth. |
| Reveal map | Registered native `debug` command with argument `fow`; require global byte `0xb092fb=0`. |
| Annex handoff | `CCountry::Annex` entry `0x118620`, before player-tag capture or province removal, permits changing only `CGameState+0xb5c` to a living scheduled-AI country. Require zero human entries, scheduled target AI, and unchanged scheduler count. |
| Manual view switch | Pause, call the saved native `tag` handler, wait for ownership transition, restore target AI and the exact scheduler count, then resume. Observer mode rejects direct `tag` use; outside observer mode the native handler is unchanged. |
| Popup suppression | Nine signature-checked generic dispatchers bypass visible popup and `CPauseGame` construction regardless of `messagetypes.txt`. Technology, invention, upper-house, bankruptcy, and diplomatic/Great War notifications are covered. Message production, effects, AI choices, logs, map notices, and icons remain intact. |

Native `tag` is asynchronous: invoking it at Annex entry does not complete the
view/ownership transition before province removal. It is therefore not a safe
replacement for the synchronous view-field handoff at that callback boundary.
Timer-driven switching handles other disappearances. Failed AI/FOW checks leave
the campaign paused; observer configuration requires an unpaused start.

## Speed and pause

Native up/down handlers must reach
`CGameState+0xb28 = selected speed - 1`, with readback after every call.
Runtime decrement coverage includes selecting speed 2 from a higher speed,
not every possible starting state.

`CInGameIdler::TogglePause` at `0x26a2c0` must produce the requested
`CInGameIdler+0x1538` state. Require current idler RTTI, readable pause state
`0` or `1`, and the cataloged signature; call only when a transition is needed
and require immediate readback. Failed readback cannot be reported as paused.

## Fixed-date completion

One game day is `verified-runtime` as 24 raw date units. Relative targets add
`N * 24` in int64 to the copied date after speed/unpause and optional observer
postconditions; absolute targets must advance and align to that scale.
Monitoring uses a Win32 `SetTimer` at `USER_TIMER_MINIMUM` (effectively 10 ms),
not a daily hook. Its callback must match the captured constructor thread.

| Condition | Contract |
| --- | --- |
| Exact target | Toggle pause, then require a later stable paused observation. The extra UI cycle allows the native endgame dialog to publish before completion. |
| Game over | Takes precedence over target completion and pause recovery. |
| Failure | Overshoot, timeout, date regression, invalid idler/pause, observer failure, invalid target, or unavailable timer is terminal. Attempt pause/readback without fabricating unavailable state. |
| Termination of monitoring | Cancel the timer and popup suppression; leave paused/open when readback succeeds. Do not write a save. Only exact completion or typed game over permits explicitly enabled native quit. |

### Game-over mapping

Require supported bytes at `0x25548d`, current `CInGameIdler`, dialog pointer
`+0x1d6c` with window vtable `0xa3c0d8`, visibility byte `+0x67` exactly one,
and `CGameState+0xb0c > [RVA 0xe586dc]+0xc`. Missing or mismatched state makes
the observation unavailable, not an ordinary pause.

Pause state alone does not identify game over: the same dialog vtable is present
with visibility zero during ordinary pause. The mapped distinction is visible
endgame dialog plus date beyond the configured end, not simply pause or dialog
existence.

### Native exit

Accepted `QUIT` confirmation `0x276500` dispatches primary vtable slot
`+0x110` to `0x24edb0`, which sets idler `+0x1d20=1`. Validate idler RTTI,
the exact vtable target, and request-flag readback. This requests native exit;
it does not use external termination or synthetic input.

Optional telemetry drain is a framework precondition, not a mapped engine
operation. Busy, timeout, or failed drain leaves the campaign paused/open;
an unavailable sink provides no summary or durability guarantee. Native exit
has no generic pre-exit plugin notification, checkpoint save, or final
game-state assertion. Drain coverage is limited to the historical bundled path,
not timeout retry or legacy-sink behavior; the current contract is in
[telemetry](../../docs/telemetry/README.md).

## Limits

No native return-to-menu operation is mapped; repeated loads in one process are
not established. Frontend compatibility includes unmodded and native GFM saves,
not arbitrary mods or every save. Resolving a mod descriptor's `user_dir` does
not establish campaign entry or compatibility.

Runtime evidence is historical and does not record exact-built-commit
provenance. It establishes the exercised engine boundaries, not acceptance of
later builds or the C service-table extraction. Defensive checks and host tests
do not expand those claims.
