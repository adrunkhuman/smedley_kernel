# Telemetry

`telemetry` is the opt-in native plugin that writes Smedley observations as UTF-8
JSON Lines. It is a trusted DLL, not a sandbox; no user C++ is required. Capture
is observational: it does not enable gameplay fixes or infer unavailable state.

## Enable capture

1. Select `plugins/telemetry.toml` and set `telemetry_enabled = true` in the
   launcher profile. Preflight rejects enabled telemetry without plugin ID
   `telemetry`.
2. Choose lifecycle events, state capture, or both. Use explicit capture rules
   for focused data; otherwise legacy state sampling includes world economic
   scans and country treasury records.
3. Launch an injected run, then validate the resulting trace:

```powershell
smedley_trace validate run.jsonl
smedley_trace summary run.jsonl
```

Without an explicit output, injected runs write
`%LOCALAPPDATA%\Smedley\traces\<run-id>.jsonl`. Dry runs create no run ID;
safe-mode launches receive no telemetry arguments or trace link. Outputs must
end in `.jsonl`; replacing an existing file requires explicit overwrite.

A small profile capture:

```toml
telemetry_enabled = true
telemetry_categories = ["lifecycle", "state"]

[[telemetry_captures]]
family = "country.daily"
cadence = "monthly"
fields = ["treasury_raw", "treasury"]
country_tags = ["PRU"]
```

Explicit rules replace legacy state sampling, not global lifecycle selection.
Empty supported entity filters select all entities; empty `fields` selects the
whole family. This can make daily POP or province capture expensive. Start with
narrow filters and inspect family health before relying on exports.

<a id="native-extension-abi-and-lifecycle"></a>

## Reference

| Need | Reference |
| --- | --- |
| Profile fields, CLI options, family/field catalog, cadence and filters | [Configuration](telemetry/configuration.md) |
| JSONL envelope, event meanings, identities, units and availability | [Events](telemetry/events.md) |
| Validation, inspection, strict derived CSV exports and GDP recipe | [Trace tool](telemetry/trace.md) |
| Native observation services, publication ABI and coordinated drain | [Native API](telemetry/native-api.md) |
| Mapping provenance, runtime results and unresolved claims | [Mapping evidence](../mappings/evidence/telemetry.md) |

## Read traces safely

- Missing, `null`, and numeric zero are distinct. Failed reads never become
  observed zeroes. Candidate fields and `quality` retain mapping uncertainty.
- Queue capacity is bounded. Contention, full/stopped queues and oversized
  records can drop data; check sequence gaps, family summaries and writer health.
  Reliable publication prevents contention-only loss, not full-queue loss.
- Callbacks do no file I/O. A worker writes complete lines and flushes at least
  once per second. Abrupt or non-Smedley exits can lose queued data and the
  latest flush interval; prior complete lines remain parseable.
- Explicit unload drains accepted records. `campaign_runner` coordinates
  telemetry drain before opt-in native exit; busy, timeout or failure leaves the
  campaign paused and open. Absent/inactive/older telemetry may permit exit
  without a final-summary or durability guarantee. See [drain semantics](telemetry/native-api.md#coordinated-drain).
- World holdings are separate observed categories, **not a money-supply
  identity**. POP lifecycle events are observed stock changes, not classified
  migration, birth, death, promotion, split or merge.

Campaign lifecycle and benchmark events require `campaign_runner` and an active
sink. Interest-fix records are currently rejected by the bundled sink; use its
[CSV/log diagnostics](interest-fix.md#diagnose-a-run) instead. CSV debug mode
truncates `<GAME_DIR>/interest_bug_fix.csv`, independently of trace overwrite
policy.

These pages describe current contracts. Historical runtime runs in mapping
evidence support only their recorded executable, implementation and scope; this
documentation cleanup is not a new runtime verification.
