# Trace tool

[Overview](../telemetry.md) · [Configuration](configuration.md) · [Events](events.md)

`smedley_trace.exe` is installed beside the launcher/CLI. It streams JSONL
without external dependencies.

```powershell
smedley_trace validate run.jsonl
smedley_trace summary run.jsonl
smedley_trace inspect run.jsonl --event country.daily --country ENG --limit 20
smedley_trace compare baseline.jsonl changed.jsonl
smedley_trace assert-benchmark run.jsonl --completed --days 365
smedley_trace assert-benchmark timeout.jsonl --failed timeout
smedley_trace export-csv run.jsonl treasury.csv --event country.daily
smedley_trace factory-value-added run.jsonl factory-va.csv --country BEL
smedley_trace producer-sales run.jsonl producer-sales.csv --country FRA
smedley_trace pop-cashflow run.jsonl pop-cashflow.csv --country FRA
smedley_trace pop-stock-lifecycle run.jsonl pop-stock.csv --country FRA
smedley_trace country-gdp run.jsonl gdp.csv --country FRA --gold-to-cash-rate 0.5
smedley_trace export-trace run.jsonl eng.jsonl --country ENG
```

## Validation and safe exports

Malformed complete records, invalid envelopes, mixed run IDs and
non-increasing sequences fail validation. An incomplete final line is warned
and ignored by ordinary validation/inspection; acceptance checks below are
stricter.

Exports create a new file by default. With `--overwrite`, they write/flush a
sibling temporary file and atomically replace the destination only after the
complete source snapshot validates. Reparse paths, hard-linked destinations
and input/output aliases are rejected. CSV text beginning `=`, `+`, `-` or `@`
gets a leading apostrophe; JSON numeric cells stay numeric.

## Benchmark acceptance and comparison

`assert-benchmark` succeeds only for the requested completed/failed terminal
state with exact typed invariants, zero sequence gaps and available progress
counters showing zero drops/no writer failure. Completed/overshot runs require
progress accounting; an ignored incomplete final record fails assertion.
Immediate failures such as `invalid_target` can precede progress: counters are
reported unavailable, not zero.

`summary` reports the single benchmark start/terminal state. `compare` uses
completed benchmark days, elapsed time, process CPU and terminal memory
separately from trace-date spans/throughput. Raw-date spans divide by 24.
`process_peak_working_set_bytes` is Windows' process-lifetime peak, not a
benchmark-interval maximum. Older traces without resources remain valid and
report missing metrics unavailable. `export-trace` rejects event filters that
separate a benchmark start from its terminal; use `inspect --event` instead.

Startup durations require each boundary exactly once in causal order:

| Metric | Boundaries |
| --- | --- |
| `lifecycle_save_load_us` | Save-selection request → frontend controller completion. |
| `lifecycle_campaign_enter_us` | Controller completion → first verified in-game idler. |
| `lifecycle_observer_configure_us` | Campaign entry → verified observer postconditions. |

Missing/ambiguous boundaries make durations unavailable. These are observation
intervals including scheduling/ingress delay, not new native timing semantics.

## Strict accounting exports

Strict exports reject unavailable evidence rather than substitute zeroes.
Required families must have healthy terminal summaries and writer health;
sequence/date gaps, drops, invalid records, duplicates or failed arithmetic can
reject an otherwise parseable trace. Capture scope and warm-up differ by command.

| Command | Output / required capture |
| --- | --- |
| `factory-value-added` | Country row per complete daily interval; daily factory `production`, direct `flows`, market `price`. |
| `producer-sales` | Complete factory/RGO/artisan sales accounts with matching summary and quantity/revenue pair. |
| `pop-cashflow` | Daily type and country accounts from `pop.cashflow.aggregate`; individual detail stays in filtered JSONL. |
| `pop-stock-lifecycle` | Daily unfiltered/unbounded aggregate stock and lifecycle reconciliation/detail. |
| `country-gdp` | Strict production account from market, factory flows, RGO/artisan production and resident POP size. |

### Factory value added

Consumption = previous flow's reconciled closing stock − current
post-consumption stock. Value physical consumption and output at closing-date
prices. If purchase settlement is skipped, the opening boundary is previous
post-consumption stock rather than a nonexistent delivery total, preserving
consumption across shutdown/shortage/no-purchase days.

The first snapshot is normally hook warm-up; N captured days yield at most N−2
rows. Rejects incomplete settlement, negative consumption, stock-flow failure,
missing prices, non-daily intervals, factory-set changes, unsupported mappings
and missing healthy factory/market/writer summaries. Supported-executable rows
use `verified-runtime` quality. The superseded spending-plus-inventory estimate
and its mixed-price limitation remain only in mapping evidence.

### Producer sales

Requires paired quantity/revenue, matching summaries, healthy summaries for
every captured producer family/writer, monotonic dates and no sequence gaps.
Warm-up/incomplete rows are checked but not exported. Inapplicable identity or
market-fraction CSV columns stay empty, not zero. Export retains one date in
memory and spools validated rows transactionally; memory does not grow with
campaign duration.

### POP cash flow

Exports type and country accounts with opening/closing money, component total,
residual, reconciliation and posted/actual amounts for all eight components.
Country rows use `pop_type_id_candidate=-1`. Requires matching summaries/accounts,
arithmetic reconciliation, healthy aggregate/writer summaries and no
sequence/date gaps. Warm-up is checked but not exported. World-wide individual
POP CSV is deliberately unsupported as a low-volume export.

### POP stock and lifecycle

Source rules must be daily, unbounded and unfiltered for both `pop.aggregate`
and `pop.lifecycle`, with aggregate count/size/employment and every lifecycle
group. The exporter proves aggregate count = lifecycle closing stock and detail
counts = reconciled summary before applying optional `--country` output filters.
Requires matching daily polls, healthy summaries, monotonic dates and no gaps;
rejects an incomplete final line or any trace warning.

Warm-up remains `opening_seen=false`, `complete=false`, not zero change. Country
filters retain current-country stock/appearance/disappearance and scope changes
when either old or new country matches. Global lifecycle summaries remain in
filtered output. Row labels remain observations, not demographic causes.

### Country GDP

This offline account is separate from [in-game `country.economy` intervals](events.md#country-economy).
It requires three consecutive daily snapshots and healthy terminal summaries
for `world.market`, `state.factory`, `province.rgo`, `pop.artisan`,
`pop.aggregate` and the writer. Matching polls and capture metadata must prove
required fields, unbounded dates, selected country and no partial province
filter. Gaps, drops, invalid records, missing prices/identities, incomplete
factory boundaries, entrants without opening factory flow evidence and
non-daily intervals fail export.

Nominal GDP values physical production and consumption at current prices; real
GDP uses first-snapshot prices or `--base-date RAW`. CSV includes factory, RGO,
precious-metal and artisan components, resident population, nominal/real GDP
and per-capita values. Sales, compensation, capitalist income, inventory change
and subsidies remain distribution/financing measures, not production value.
`--gold-to-cash-rate RATE` is mandatory when selected scope produces precious
metal; use the active mod's rate, not market price or RGO income.

Minimal unbounded capture (change all four `PRU` filters together):

```toml
[[telemetry_captures]]
family = "world.market"
cadence = "daily"
fields = ["price"]

[[telemetry_captures]]
family = "state.factory"
cadence = "daily"
fields = ["production", "flows"]
country_tags = ["PRU"]

[[telemetry_captures]]
family = "province.rgo"
cadence = "daily"
fields = ["identity", "production"]
country_tags = ["PRU"]

[[telemetry_captures]]
family = "pop.artisan"
cadence = "daily"
fields = ["identity", "production", "inputs"]
country_tags = ["PRU"]

[[telemetry_captures]]
family = "pop.aggregate"
cadence = "daily"
fields = ["size_candidate"]
country_tags = ["PRU"]
```

Do not set `start_date_raw`, `end_date_raw` or `province_ids`. After at least
three captured dates:

```powershell
smedley_trace country-gdp run.jsonl gdp.csv --country PRU
```

Add `--gold-to-cash-rate RATE` for precious-metal scope. Runtime examples and
failure counterexamples are in [mapping evidence](../../mappings/evidence/telemetry.md),
not acceptance of a newly built runtime.
