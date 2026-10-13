# Native telemetry APIs

[Overview](README.md) · [Events](events.md)

The kernel owns engine reads/hooks; the telemetry plugin owns cadence,
filtering, aggregation, valuation, reconciliation and JSON shaping. Native
plugins are trusted code. No C++ ABI or raw engine object crosses the service
boundary.

## Observation services

The plugin dynamically resolves `event_api`, `logging_api`,
`telemetry_game_api` and `telemetry_observation_api` from `smedley_kernel.dll`;
it does not link kernel internals. Public headers:

- [`telemetry_game_api.h`](../../include/smedley/telemetry_game_api.h): v1 telemetry sessions and hook capture.
- [`telemetry_observation_api.h`](../../include/smedley/telemetry_observation_api.h): separately discoverable v1 checked observation reads; existing game API v1 layouts/table remain intact.
- [`event_api.h`](../../include/smedley/event_api.h): daily event callbacks; observation services do not schedule callbacks.

Read on the daily callback's game thread. Observation sessions require an open
parent telemetry session and are owner-thread/game-session-epoch bound. Parent
close/staleness invalidates handles.

| Contract | Meaning |
| --- | --- |
| Records | Fixed-width, pointer-free, versioned; reserved fields zero. |
| Capabilities | Full market/ongoing wars; daily tag resolution; country metrics/military/diplomacy/economy/creditors; POP economy/demographics/identity/needs/artisan inputs; factory records/inputs; province daily/production availability; RGO by province ID. |
| Handles | POP/factory handles share the parent namespace with v1 hook `entity_id`: high 32 bits nonzero parent session ID, low 32 opaque entity serial, never address bits. Parent IDs cannot exceed 32 bits. |
| Factory identity | Opaque hook correlation plus per-read `observation_index`; no native address. |
| Group masks | Market/artisan/factory/RGO require nonzero known mask; only requested groups traversed. |
| Bulk output | Caller-owned bounded buffers; `TRUNCATED` writes available prefix. |
| `INVALID_SOURCE` | Checked reader rejected source metadata. |
| `UNAVAILABLE` | Requested state absent. |

Unavailable, stale, wrong-thread, invalid-source and truncated reads are missing
observations, never zeroes. Adapters use bounded static scratch storage and do
not allocate in callbacks; native addresses remain kernel-private in the
session identity map. Engine mapping evidence is not upgraded by API migration.

## Publication ABI v1

[`include/smedley/telemetry.h`](../../include/smedley/telemetry.h) defines the C
publication ABI. Do not link against `telemetry.dll`. Resolve symbols from an
**already loaded, trusted** sibling module with `GetModuleHandleW` and
`GetProcAddress`; `campaign_runner` verifies its plugin directory. Extensions
need the equivalent installation trust check. Do not use `LoadLibrary` or paths
derived from run metadata.

| Symbol | Contract |
| --- | --- |
| `SmedleyTelemetryEmitV1` | Nonblocking; contention may return `dropped`. Suitable for hot observational hooks; non-waiting shared sink-lifetime guard. |
| `SmedleyTelemetryEmitReliableV1` | Optional low-frequency publication; waits for bounded in-memory lifetime/serialization/queue locks. Still drops on full/stopped queues. **Not for hot hooks.** |
| `SmedleyTelemetryDrainV1(timeout_ms)` | Optional exit coordination; see below. |

Emit results are `unavailable`, `filtered`, `accepted`, `dropped`, `invalid`.
Internal exceptions report `dropped`; invalid caller ABI reports `invalid`.
Nonblocking/reliable calls coexist; unload takes exclusive sink ownership after
in-flight calls finish. `campaign_runner` prefers reliable emission and falls
back to the original symbol for older compatible sinks.

| Constraint | Contract |
| --- | --- |
| Data/lifetime | Fixed-width C values and bounded UTF-8 pointer/length pairs; consumed synchronously and copied before return. |
| Validation | Records/fields have `struct_size`, `version`, zero reserved fields. V1 array elements require exactly published V1 size; no stride exists for larger elements. |
| Threading | Thread-safe; no caller-thread execution guarantee. |
| Sink-owned fields | Run ID, sequence, wall time, monotonic time. |
| Identifier / string length | 48 / 128 bytes maximum. |
| Field count | Eight entity + payload fields combined. |
| Encoded size | 1024 bytes maximum. |
| Scalars | Null, bool, int64, double, UTF-8 string; no raw JSON. |

Future array layouts need a new version/symbol or explicit stride, not oversized
V1 elements. Lifecycle producers retry only `unavailable`; all other outcomes
are terminal for the observed transition.

## Coordinated drain

Drain results: `unavailable`, `completed`, `busy`, `timeout`, `failed`.
`timeout_ms` is milliseconds; `UINT32_MAX` has no deadline. A finite call uses
one monotonic deadline for exclusive sink ownership, drain-state ownership,
producer quiescence, queue consumption, worker join, final summary, flush/close.
Do not call recursively or from a telemetry producer.

`completed` guarantees new extension/daily ingress stopped, entered producers
returned, every accepted payload consumed, worker joined, enabled final summary
written, file flushed/closed. The summary excludes itself from accepted/written
counts. Explicit unload also drains/joins and appends a best-effort post-drain
summary before final flush/close.

| Result/state | Consequence |
| --- | --- |
| `busy` | Concurrent drain rejected immediately. |
| Deadline before exclusive sink ownership | No shutdown transition occurred. |
| Timeout after shutdown starts | Not cancellation: ingress stays disabled; worker drain continues, never detached. |
| Later non-recursive call | Retries unfinished coordination or waits/joins the same drain; never reopens ingress or starts another worker. |
| `unavailable` | No active drain-capable sink; no durability/final-summary guarantee. |

Before opt-in native exit, `campaign_runner` resolves the sibling drain symbol
and uses a five-second deadline. Completed permits quit; unavailable permits
compatibility quit when telemetry is absent/inactive/older. Busy, timeout or
failure leaves the campaign paused/open. This is telemetry-specific, not a
generic kernel pre-exit callback; the kernel does not unload plugins in
`DllMain`. Abrupt/non-Smedley exit can lose the queue and latest one-second
flush interval. Completed prior lines remain parseable; callbacks do not
interleave output or perform file I/O.

Historical bundled completed-drain acceptance is recorded in
[mapping evidence](../../mappings/evidence/telemetry.md#delivery-and-lifecycle).
It does not cover runtime timeout retry.

## Contributor checklist

Update the shared [registry](../../telemetry_registry/telemetry_registry.cpp),
owning collector, launcher selection behavior, strict trace handling, tests
and these references together. Cadence, required filters, event ownership,
schema membership, admission and hook planning are enforced. Cost class,
collector identity, mapping and quality are reported descriptive metadata;
registry contents expose no engine offsets. Retain bounded callback work and
explicit missing/loss accounting.
