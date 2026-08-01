# Telemetry

`telemetry` is Smedley's first-party, opt-in native JSON Lines plugin. It is a
trusted DLL, not a sandbox. Enable `telemetry_enabled` and select
`plugins/telemetry.toml`; the shared launcher preflight rejects an enabled
profile without plugin ID `telemetry`. It requires no user C++.

## Configuration

Profiles use these top-level fields. Every present field is type-checked on
load and save; the launcher repeats range, category, output, and selected-plugin
validation before launch.

| Field | Type | Default | Rules |
| --- | --- | --- | --- |
| `telemetry_enabled` | boolean | `false` | Requires selected plugin ID `telemetry` when injecting. |
| `telemetry_output` | string, optional | per-run path | Must name a non-directory file. |
| `telemetry_categories` | string array | `"lifecycle", "state"` | Non-empty, unique, values are `lifecycle` and/or `state`. |
| `telemetry_country_tags` | string array | empty | Optional unique normalized three-character uppercase ASCII alphanumeric country tags, including dynamic tags such as `D01`. |
| `telemetry_start_date_raw` | integer, optional | none | Inclusive raw game-date lower bound. |
| `telemetry_end_date_raw` | integer, optional | none | Inclusive raw game-date upper bound; cannot precede start. |
| `telemetry_sample_days` | integer | `1` | 1 through 365 game days. Applies before country extraction and formatting. |
| `telemetry_queue_capacity` | integer | `1024` | 64 through 8192 fixed record slots. |
| `telemetry_overwrite` | boolean | `false` | Required to replace an existing output. |

Selecting the `state` category also enables bounded world economic snapshots in
`telemetry.dll`. Collection follows the same inclusive date bounds and
`telemetry_sample_days` interval. A complete world POP walk is materially more
expensive than ordinary treasury sampling. Country filters apply only to
`country.daily`; they do not suppress global world records.

If no output path is supplied, a real injected launch derives
`%LOCALAPPDATA%\Smedley\traces\<run-id>.jsonl`. The launcher creates its run ID
only after the shared `BuildLaunchPlan` phase. Before `CreateProcessW`, it
appends safely quoted `-smedley-run-id=<run-id>` to the injected child command
line, appends the derived output argument when necessary, and stores the exact
child command line and trace link in the run record. Dry runs do not create a
run ID; safe-mode launches do not receive telemetry arguments or create a trace
link.

CLI controls are `--telemetry`, `--no-telemetry`, `--telemetry-output PATH`,
repeatable `--telemetry-category lifecycle|state`,
`--telemetry-sample-days N`, `--telemetry-queue-capacity N`, and
`--telemetry-overwrite`, repeatable `--telemetry-country TAG`,
`--telemetry-start-date-raw N`, and `--telemetry-end-date-raw N`.

Outputs must end in `.jsonl` (case-insensitive). Existing outputs are rejected
unless overwrite is explicit. Directories, reparse points, and paths colliding
with the save, executable, kernel, selected manifests, modules, or mod
descriptors are rejected. The plugin repeats output validation immediately
before opening with `CREATE_NEW` by default, or `CREATE_ALWAYS` only when the
explicit overwrite argument is present.

## JSONL envelope v1

Each physical line is one UTF-8 JSON object with schema name
`smedley.telemetry` and `schema_version` `1`. The built-in encoder escapes JSON
controls, quotes, backslashes, and invalid UTF-8 bytes. Valid Unicode UTF-8 is
preserved. Payload and entity objects are typed JSON, never serialized pointers.

| Field | Type | Meaning |
| --- | --- | --- |
| `schema` | string | Always `smedley.telemetry`. |
| `schema_version` | integer | Always `1`. |
| `run_id` | string | Launcher-created run identifier. |
| `sequence` | integer | Monotonically increasing per emitted record; queue drops can leave gaps. |
| `wall_time_utc` | string | UTC ISO-8601 timestamp with milliseconds. |
| `monotonic_us` | integer | Process monotonic timestamp in microseconds. |
| `game_date_raw` | integer or `null` | Raw game date only when available from the current mapping. |
| `event_type` | string | Stable event name such as `session.started` or `country.daily`. |
| `category` | string | `lifecycle` or `state`. |
| `mapping_id` | string | `v2game-3.04` for this slice. |
| `quality` | enum | Canonical mapping level: `verified-runtime`, `verified-current`, `verified-static-callsites`, `provisional`, `historical-unverified`, or `historical-skeleton`. Built-in records currently use `verified-current`, `verified-runtime`, and `provisional`. |
| `entities` | object | Stable identifiers, currently `country_tag` where applicable. |
| `payload` | object | Event-family-specific typed fields. |

Absent is not the same as `null`, and neither is zero. A field is absent when
the event family does not define it. `null` means the field is defined but
unavailable, for example `game_date_raw` on lifecycle records. Numeric `0` is
an observed zero. Consumers must preserve all three states.

For the supported executable, `game_date_raw` advances by 24 units per game
day. Trace summaries divide raw-date deltas by 24 before reporting game-day
spans or game-days per second. This is a verified runtime property recorded in
`mappings/TELEMETRY.md`; profile date bounds remain raw values.

Example:

```json
{"schema":"smedley.telemetry","schema_version":1,"run_id":"9f0...","sequence":8,"wall_time_utc":"2026-07-31T12:34:56.789Z","monotonic_us":912345,"game_date_raw":12345,"event_type":"country.daily","category":"state","mapping_id":"v2game-3.04","quality":"provisional","entities":{"country_tag":"ENG"},"payload":{"treasury_raw":32768,"treasury":1.000000}}
```

## Current events and evidence

`session.started`, `telemetry.progress`, and best-effort `telemetry.summary` are
lifecycle records with `verified-current` quality. Progress and summary payloads
report these delivery metrics:

| Field | Meaning |
| --- | --- |
| `accepted` | Records accepted by the queue. |
| `written` | Records written when the payload is sampled, before the summary itself is written. |
| `dropped` | Records rejected by bounded delivery. |
| `high_water` | Highest observed queue occupancy. |
| `write_failed` | Whether the writer has failed. |
| `callback_enqueue_format_us_total` | Total callback enqueue and formatting time in microseconds. |
| `callback_enqueue_format_us_mean` | Mean callback enqueue and formatting time in microseconds. |
| `callback_count` | Number of callback attempts. |
| `skipped_unsampleable` | Samples skipped because the mapped game state was unavailable. |

`country.daily` is a `state` record from `DailyUpdateEvent` with `provisional`
quality. Its stable entity is the three-character country tag. Its payload
contains `treasury_raw` and
`treasury`, where `treasury = treasury_raw / 32768.0` because the exposed
fixed-point representation is 48.15. It deliberately does not contain
`treasury_shadow`, pointers, guessed AI intentions, candidates, scores, or
decision reasoning. This is economy state telemetry, not a decision trace.

`world.daily` is emitted once per selected sample date before country filtering.
It reports `country_slot_count`, `ai_scheduler_entry_count`, and
`human_control_present` with `provisional` quality. Country slots include
non-playable engine entries, and scheduler entries are not asserted to equal a
count of AI-controlled countries. These names expose the observed containers
without inventing stronger gameplay semantics.

`telemetry` attempts one bounded scan per selected sample date. It
always attempts to emit `world.economy.health`; it attempts the other `state`
records only when their stated completeness and flag conditions pass:

| Event | Payload | Contract |
| --- | --- | --- |
| `world.economy.health` | structural completeness, snapshot/collection/credit flags, country/state/province/POP counts | Emission is attempted after every scan; counts are traversal observations. Credit flags do not invalidate independently complete holdings and capacity records. |
| `world.economy.capacity` | fixed limits, basis-point utilization, and collection microseconds | Emitted only for a structurally complete zero-flag scan. Limits are 512 countries, 4,096 provinces, and 100,000 POP records. |
| `world.economy.holdings` | observed treasury, POP money, POP savings, bank interest accumulator, positive-balance counts, negative-treasury country count | Emitted only for a complete scan with `provisional` quality. Components remain separate and are not a claimed money-supply identity. |
| `world.economy.credit` | creditor counts, paid-entry counts, and creditor/state candidate aggregates | Emitted only when structural and credit-specific flags are clear, with `provisional` quality. Every `_candidate_raw` field retains its mapping uncertainty. |

POP money and POP savings are different storage categories. Savings and
creditor/state values may be financial claims or bookkeeping aggregates; adding
them to liquid balances can double-count value. `bank_interest_accumulator_raw`
is the verified temporary destination of charged interest, not a national-bank
cash balance. The plugin deliberately does not emit `world_money_supply`.

With both `interest_bug_fix` and `telemetry` selected, the fix emits:

| Event | Payload | Contract |
| --- | --- | --- |
| `interest.fix.health` | status, flags, source/province/POP counts, verified POP count, callback microseconds | One `---` daily aggregate plus any rejected debtor, recipient result, or treasury-mismatch warning. Complete paid-recipient detail remains in `interest_bug_fix.csv`. |
| `interest.fix.value` | exact aggregate bank transfer, derived POP payout, domestic and foreign components | Emitted once for a fully successful day; failed or partial days emit no value record. |

Both records use `verified-runtime` quality. The health shape uses the ABI limit
of eight combined entity/payload fields exactly. They use the reliable bounded
emitter so lock contention alone cannot hide a fix result; unavailable,
filtered, full-queue, or invalid telemetry remains independent of mutation and
never changes whether the fix pays POPs. `interest_bug_fix.csv` records the two
telemetry result codes for independent diagnosis. This guarantee requires the
bundled `SmedleyTelemetryEmitReliableV1`; an older compatible telemetry plugin
without that symbol receives best-effort nonblocking publication.

The fix CSV is separate from JSONL telemetry configuration. Selecting the fix
opens and truncates `<GAME_DIR>/interest_bug_fix.csv`; telemetry output paths and
overwrite policy do not change that fixed diagnostic file.

The project mapping inventory has historical status spellings, but telemetry
uses only canonical project evidence levels.

A daily record depends on weaker field and date evidence, so it is always
`provisional`; a non-null date never upgrades it. If the game-state pointer is
unavailable, no country record is emitted and `skipped_unsampleable` increases.

## Bounded delivery

The selected plugin starts a worker after opening the output. State callbacks
first check category, date range, and sampling. A selected date emits one global
snapshot; country records then check the country tag before treasury extraction
and JSON formatting. Lifecycle progress remains useful when state records are
excluded by date or country filters. Callbacks never perform file I/O or flush.
The queue has fixed-capacity, fixed-size record slots. Ordinary callbacks use a
non-waiting queue lock; full, contended, stopped, or oversized records are
explicitly counted as dropped. `telemetry.progress`, `world.daily`,
date-regression, economic snapshot, and opt-in interest-fix result records use
reliable bounded publication so lock contention alone cannot split their
evidence. The queue remains bounded, and publication still performs no file I/O.
The sole worker writes
complete JSON Lines incrementally and flushes at least once per second. An
explicit future unload drains accepted records, appends a best-effort final
summary using post-drain statistics, and flushes. The current kernel has no
verified normal game-shutdown callback and does not unload plugins from
`DllMain`; normal process exit therefore does not emit `telemetry.summary` or
join the writer. `telemetry.progress` is emitted at least once per observed game
date when `lifecycle` is selected. Up to the latest userspace queue and the
one-second flush interval can be lost at real process exit. Completed prior
lines remain independently parseable and no callbacks interleave output.

The economic producer performs its bounded world traversal on the game thread
only at selected dates. It allocates its fixed storage at plugin construction,
retains no game pointers across callbacks, performs no file I/O, and reports
`collection_us`. Interest-fix `callback_us` measures the AFTER callback from
post-original sampling through validation and allocation/mutation postconditions,
immediately before telemetry publication. The fix's CSV worker remains
independently bounded at 1,024 result slots.

## Trace tool

`smedley_trace.exe` is installed beside the launcher and CLI. It streams JSONL
without external dependencies:

```powershell
smedley_trace validate run.jsonl
smedley_trace summary run.jsonl
smedley_trace inspect run.jsonl --event country.daily --country ENG --limit 20
smedley_trace compare baseline.jsonl changed.jsonl
smedley_trace assert-benchmark run.jsonl --completed --days 365
smedley_trace assert-benchmark timeout.jsonl --failed timeout
smedley_trace export-csv run.jsonl treasury.csv --event country.daily
smedley_trace export-trace run.jsonl eng.jsonl --country ENG
```

Malformed complete records, invalid envelopes, mixed run IDs, and non-increasing
sequences fail validation. An incomplete final line is warned and ignored.
Exports create a new file by default. Both export commands accept `--overwrite`:
the tool writes and flushes a sibling temporary file, then atomically replaces
the destination only after the complete source snapshot validates. It rejects
reparse paths, hard-linked destinations, and input/output aliases. CSV text
cells beginning with `=`, `+`, `-`, or `@` receive a leading apostrophe to avoid
spreadsheet formula interpretation; JSON numeric cells remain numeric.

`assert-benchmark` is the scriptable acceptance boundary. It exits successfully
only when the trace has the requested completed or failed terminal state, typed
completion invariants are exact, sequence gaps are zero, and available progress
counters report zero drops and no writer failure. Completed and overshot runs
require progress accounting, and an ignored incomplete final record fails the
assertion. Immediate failures such as
`invalid_target` can occur before a progress sample; the compact output reports
those counters as unavailable rather than inventing zero.

`summary` and `compare` derive three startup phase durations from existing
monotonic lifecycle records: `lifecycle_save_load_us` spans save-selection
request to frontend controller completion, `lifecycle_campaign_enter_us` spans
controller completion to the first verified in-game idler, and
`lifecycle_observer_configure_us` spans campaign entry to verified observer
postconditions. A duration is unavailable unless both boundary events occur
exactly once in causal order. These are telemetry observation intervals, so
they include any scheduling or ingress delay at the boundaries; no new game
timing hook or engine semantic is inferred.

## Native extension ABI and lifecycle

`include/smedley/telemetry.h` defines stable C ABI v1. Extensions do not link
against `telemetry.dll`; they may resolve exactly `SmedleyTelemetryEmitV1` from
an already loaded `telemetry.dll` with `GetModuleHandleW` and `GetProcAddress`.
First-party `campaign_runner` additionally confirms that telemetry is in its own
plugin directory. Extensions should make the equivalent trust check for their
installation and must not use `LoadLibrary` or derive a DLL path from run
metadata. Results are `unavailable`, `filtered`, `accepted`, `dropped`, and
`invalid`.

`SmedleyTelemetryEmitV1` is nonblocking and may report `dropped` on lock
contention, making it suitable for hot observational hooks. Low-frequency
lifecycle producers may optionally resolve `SmedleyTelemetryEmitReliableV1`.
That entry waits only for bounded in-memory sink-lifetime, serialization, and
queue locks; it still reports `dropped` when the queue is full or stopped and
must not be used from hot hooks. The nonblocking entry uses a non-waiting shared
lifetime guard and can coexist with reliable calls; unload takes exclusive
ownership only after in-flight calls finish. `campaign_runner` prefers the
reliable symbol and falls back to the original symbol when paired with an older
telemetry plugin.

The ABI contains only fixed-width C values and bounded UTF-8 pointer/length
pairs. Records and fields include `struct_size`, `version`, and zero reserved
fields.

| Constraint | Contract |
| --- | --- |
| Input lifetime | Consumed synchronously and copied before return. |
| Threading | Thread-safe, with no caller-thread guarantee. |
| Sink-owned fields | Run ID, sequence, wall time, and monotonic time. |
| Identifier length | At most 48 bytes. |
| String length | At most 128 bytes. |
| Field count | At most eight entity and payload fields combined. |
| Encoded record size | At most 1,024 bytes. |
| Scalar types | Null, bool, int64, double, or UTF-8 string; raw JSON is not accepted. |

V1 array elements must have exactly the published V1 `struct_size`; a larger
element has no stride in this ABI and is rejected. Future layouts therefore use
a new version and symbol, or a future ABI with an explicit array stride. The
export catches all internal exceptions and reports `dropped`; `invalid` means
the caller's ABI record failed validation. Lifecycle observation points retry
only while the sink is unavailable; accepted, filtered, dropped, and invalid
outcomes are terminal for that observed transition. One-shot actions remain
best effort if telemetry becomes active only after the action.

| Event | Typed data | Quality and observation |
| --- | --- | --- |
| `session.started` | `plugin` | `verified-current`, after writer startup. |
| `campaign.save_selection_requested` | `source="campaign_runner"` | `verified-runtime`, after the requested filename is present in `+0x590` and `+0x5bc`/`+0x5bd` are written; no save name/path. |
| `campaign.save_load_completed` | none | `verified-runtime`, first nonzero `+0x5bd`, before play dispatch; controller completion only. |
| `campaign.entered` | observer/speed/pause requests | `verified-runtime`, first successful `CInGameIdler` RTTI readback. |
| `observer.configured` | `viewing_country`; full AI/map booleans | `verified-runtime`, only after no human control, AI scheduling, FOW disable, and observed valid view. Pending initial switches delay it. |
| `speed.configured` | previous/current/requested speed | `verified-runtime`, after handler readback, including no-op configuration. |
| `pause.configured` | previous/current/requested pause | `verified-runtime`, once after final readback; observer setup pauses are not final. |
| `date.regressed` | previous/current/delta raw date | `provisional`, lower date observed by the daily callback; not proof of reload. |
| `benchmark.started` | start/target raw date, requested days, timeout seconds | `provisional`; emitted when the fully configured benchmark interval begins. The record date is the start raw date. |
| `benchmark.resources` | process CPU microseconds; available start/end working set and private committed bytes; process-lifetime peak working set | `verified-current`; one optional record immediately before a started benchmark's terminal record, derived from Windows process samples at benchmark start and terminal. CPU is the user-plus-kernel delta for the benchmark interval. |
| `benchmark.completed` | start/target/actual raw date, game days, elapsed microseconds, zero overshoot, paused | `provisional`; exact-target pause readback only. |
| `benchmark.failed` | start/target raw date, elapsed microseconds, stable reason; actual date and paused state when readable | `provisional`; reason is `timeout`, `date_overshoot`, `idler_unavailable`, `invalid_pause_state`, `pause_failed`, `observer_invariant_failed`, `date_regressed`, `unexpected_pause`, `timer_unavailable`, or standalone `invalid_target`. |

Campaign lifecycle records exist only with `campaign_runner` selected and an
active telemetry sink. They obey only the `lifecycle` category, never state
country/date filters. This slice makes no political-event, AI-reasoning, or
reload claim. A queue drop can create gaps; unavailable and filtered results
are intentionally quiet.

Benchmark records have no effect on campaign outcome when telemetry is absent,
filtered, dropped, or invalid. `smedley_trace summary` reports the single
benchmark start and terminal state; `compare` uses completed benchmark
game days, elapsed time, process CPU, and terminal memory separately from
observed trace-date metrics. Memory is sampled rather than a benchmark-interval
maximum; `process_peak_working_set_bytes` is the process-lifetime peak reported
by Windows. Older traces without a resource record remain valid and report
these fields as unavailable.
`export-trace` rejects an event filter that would separate a benchmark start
from its terminal record; use `inspect --event` to view one event type.

The July 31, 2026 observer acceptance run
`f2d403e1-82f8-46b8-8832-a136c822d38a` recorded save selection, load
completion, campaign entry, speed and pause configuration, then delayed
`observer.configured` until the requested ENG view had restored AI control.
After 515 game days the trace contained 1,039 strictly ordered records with no
gaps, drops, or write failure. The game remained responsive and the reported
mean daily-callback enqueue/format cost was 0.217 microseconds.
