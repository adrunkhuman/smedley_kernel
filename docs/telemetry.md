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

Profiles may instead add up to 32 explicit capture rules. A family may appear
once. Explicit rules replace legacy `state` sampling; lifecycle category
selection remains global. Capture rules require the `state` category.

```toml
[[telemetry_captures]]
family = "world.economy"
cadence = "monthly"
fields = ["health", "holdings"]
country_tags = []
province_ids = []

[[telemetry_captures]]
family = "province.daily"
cadence = "daily"
fields = ["owner_tag_candidate", "life_rating_candidate", "infrastructure_candidate_raw"]
country_tags = []
province_ids = [1, 300, 549]

[[telemetry_captures]]
family = "pop.aggregate"
cadence = "monthly"
fields = ["pop_count", "size_candidate", "money_raw", "savings_raw"]
country_tags = []
province_ids = [549]
```

Each rule accepts `family`, `cadence`, `fields`, `country_tags`, `province_ids`,
and optional `start_date_raw` and `end_date_raw`. Empty `fields` selects every
field in that family. Country filters are valid for all `country.*` families and
`state.factory`.
Province filters are valid for `province.daily`, `province.production`, `province.rgo`, `pop.economy`,
`pop.demographics`, and `pop.aggregate`. An empty entity filter means every
entity, including daily all-province or all-POP capture when explicitly
requested. Country families filter the country supplied by each
`DailyUpdateEvent`; they do not initiate a separate country traversal. Global
families such as `world.military` have no country entity and ignore country
filters.

| Family | Selectable fields |
| --- | --- |
| `world.daily` | `country_slot_count`, `ai_scheduler_entry_count`, `human_control_present` |
| `world.economy` | record groups `health`, `capacity`, `holdings`, `credit` |
| `country.daily` | `treasury_raw`, `treasury` |
| `country.metrics` | record groups `power`, `politics` |
| `country.military` | `unit_count_candidate`, `mobilized_candidate`, `scheduled_mobilization_count_candidate`, `leadership_candidate_raw`, `military_ranking_candidate` |
| `world.military` | `ongoing_war_count_candidate` |
| `country.diplomacy` | record groups `status`, `relations` |
| `state.factory` | record groups `identity`, `employment`, `production`, `finance`, `inputs` |
| `world.market` | record groups `price`, `supply`, `demand`, `sales` |
| `province.daily` | `owner_tag_candidate`, `controller_tag_candidate`, `colonial_level_candidate`, `life_rating_candidate`, `infrastructure_candidate_raw` |
| `province.production` | `building_slot_count_candidate`, `construction_count_candidate` |
| `province.rgo` | record groups `identity`, `employment`, `production`, `finance` |
| `pop.economy` | `money_raw`, `savings_raw`, `interest_cash_flow_raw`, `total_cash_flow_raw` |
| `pop.demographics` | `size_candidate`, `employed_candidate`, `consciousness_candidate_raw`, `militancy_candidate_raw`, `literacy_candidate_raw` |
| `pop.aggregate` | `pop_count`, `size_candidate`, `employed_candidate`, `money_raw`, `savings_raw` |

Cadences are `daily`, `weekly`, `monthly`, and `yearly`. Weekly capture is
anchored to the first eligible observed date and repeats every seven game days.
Monthly and yearly capture emits on the first observed date in each Victoria II
calendar period. The game calendar has 24 raw units per day, fixed 365-day
years, no leap day, and epoch `-5000.1.1`; `1836.1.2` is raw `59883384`.
Date regression resets each rule independently.

Selecting the `state` category also enables bounded world economic snapshots in
`telemetry.dll`. Collection follows the same inclusive date bounds and
`telemetry_sample_days` interval. A complete world POP walk is materially more
expensive than ordinary treasury sampling. Legacy top-level country filters
apply to `country.daily`; explicit rule filters apply to every `country.*`
family. Neither suppresses global world records.

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
`--telemetry-start-date-raw N`, and `--telemetry-end-date-raw N`. Repeat
`--telemetry-capture "family|cadence|fields|countries|provinces|start|end"`
to replace profile rules from the CLI. Comma separates fields and entity IDs;
empty components are allowed.

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
| `accepted` | Payload records accepted by the queue; excludes `telemetry.summary`. |
| `written` | Accepted payload records written by the worker; excludes `telemetry.summary`. |
| `dropped` | Records rejected by bounded delivery. |
| `high_water` | Highest observed queue occupancy. |
| `write_failed` | Whether the writer has failed. |
| `callback_enqueue_format_us_total` | Total callback enqueue and formatting time in microseconds. |
| `callback_enqueue_format_us_mean` | Mean callback enqueue and formatting time in microseconds. |
| `callback_count` | Number of callback attempts. |
| `skipped_unsampleable` | Samples skipped because the mapped game state was unavailable. |

At a coordinated drain, one `telemetry.family.summary` record is attempted per
configured family. It reports `polls_due`, `collection_attempts`, `accepted`,
`filtered`, `dropped`, `invalid`, and `collection_us`. These counters describe
producer results, not records subsequently written by the shared worker.

`country.daily` is a `state` record from `DailyUpdateEvent` with `provisional`
quality. Its stable entity is the three-character country tag. Its payload
contains `treasury_raw` and
`treasury`, where `treasury = treasury_raw / 32768.0` because the exposed
fixed-point representation is 48.15. It deliberately does not contain
`treasury_shadow`, pointers, guessed AI intentions, candidates, scores, or
decision reasoning. This is economy state telemetry, not a decision trace.

`country.metrics` emits provisional `country.metrics.power` and
`country.metrics.politics` records keyed by country tag. Power contains raw
prestige and infamy candidates plus overall, military, industrial, and prestige
ranking candidates. Politics contains raw plurality, war exhaustion, diplomatic
points, research points, and leadership candidates. All raw fields except
leadership use the game's 1/1000 scalar representation; leadership uses 48.15
fixed point and divides by 32,768.

Vanilla run `f1e91e80-a3fc-41f6-9dc4-45fc2577f068` correlated PRU against
`benchmark.v2`: prestige 50.055, plurality 25.059, diplomatic points 5.000,
research points 32.603, leadership 8.26196, and zero infamy and war exhaustion.
Observed rankings were overall 4, military 4, industrial 4, and prestige 5.

`country.military` and `world.military` expose only bounded aggregate candidates.
`country.military` reports country unit-list count, mobilization state and
schedule count, leadership, and military ranking. `world.military` has no entity
and reports only the global ongoing-war list count. They do not
expose unit, regiment, war, side, or battle identities. Run
`970c3cf3-f56f-4f10-aed8-5f153534150a` observed PRU with 12 unit-list entries,
no active or scheduled mobilization, leadership raw 270728, military rank 4,
and three global ongoing-war entries.

`country.diplomacy` emits provisional status and relation summaries keyed by
country tag. Status contains substate/vassal flags plus overlord and sphere-
leader tag candidates. Relations contains sphereling, vassal, ally, guarantee,
and neighbor counts. Run `70db3477-0e35-4fa6-8ea4-415bc96e7483` observed PRU
with no overlord or sphere leader, 15 spherelings, five allies, zero vassals or
guarantees, and 21 neighbors. Influence values and opaque diplomatic actions or
statuses remain unavailable.

`state.factory` emits one record per selected group and factory from the owning
country's bounded state list. Factory entities are country tag, serialized
persistent state ID, and factory-definition key. Identity additionally carries
the shared state-region key, anchor-province candidate, level, subsidy state, and closed
state. Persistent state IDs distinguish separately owned state instances;
owner-specific portions of a split state share the same `CRegion` key.

A controlled `SAX_558` merge retained PRU state 757, removed SAX state 907, and
moved the same `fabric_factory` node into PRU immediately while paused. This
verifies ordinary non-duplicate attribution and lineage. Duplicate factory-type
selection was also tested for equal-level Fabric Factories: the recipient PRU
node survived unchanged, including subsidy and budget, while the transferred
SAX node disappeared. Different-level duplicates and merges above eight
distinct types remain unavailable.

Employment reports total employees plus craftsmen and clerk assignment totals.
Production reports current `output_raw`, output-good key and ordinal, and the
definition's `base_output_raw`; fixed-point output values use a 32,768 scale.
`inputs` emits one record per nonempty stockpile good with its zero-based,
mod-load-order-dependent ordinal and 32,768-scaled `stockpile_raw`. It does not
infer current demand from the static production recipe.

Finance reports nonnegative observed amounts: `budget_raw`,
`market_spending_expense_raw`, `sales_income_raw`, `paychecks_expense_raw`, and
`investment_income_raw`. Displayed currency is raw / 32,768,000. Expense fields
remain positive in telemetry; the UI supplies their minus signs. Vanilla
`FACTORY_PAYCHECKS_LEFTOVER_FACTOR` is 0.25, but mods can override it (GFM uses
0.3), so telemetry records the engine result rather than deriving paychecks.
The wiki documents employment and production formulas but not a paycheck
cadence; no cadence claim is made.

The UI's last-day balance equals sales income plus investment income minus
market-spending and paycheck expenses. The main factory-list profit is a
separate computed display value and remains unavailable. Upgrade projects use
an embedded state `CPopProject`; progress and material acquisition remain
unavailable until its referenced storage is correlated. Upgrade availability
must not be inferred because policy, ownership, funds, level, and existing
projects can prevent it.

Vanilla run `40d36695-696f-408e-af64-df266a1cfcc8` emitted identity,
employment, production, and finance records for all seven PRU factories on
1836.1.3: four in the state anchored by Berlin 549, one anchored by province
575, and two anchored by province 682. All 28 records were accepted with zero
filtered, dropped, or invalid records. Brandenburg's types, levels, employment,
and Small Arms values exactly matched the independent UI and read-only process
probe.

Depth run `734efd9b-7873-4c26-b9aa-660422b3d4ad` emitted 53 factory records:
seven each for identity, employment, production, and finance plus 25 input
stockpile records. The trace had zero gaps, filtered records, drops, or invalid
records. Persistent state IDs were 750, 753, and 756; all Brandenburg worker
splits and Small Arms stockpile values matched the save exactly.

Region run `421a1be4-c4c4-4e03-bcec-01d380742c7e` emitted the same 53 factory
records with shared `state_region_key` identities, zero gaps, filters, drops, or
invalid records.

`world.market` emits one record per active zero-based goods ordinal and selected
group. All amounts use the engine's 32,768 fixed-point scale. Price reports
current and previous price; supply reports current/previous supply and world
market stock; demand reports nominal and real demand; sales reports total actual
sold and the world-market subset. Goods ordinals follow mod load order and need
a future goods-catalog record for portable names. Global market records do not
attribute sales to individual factories, RGOs, artisans, or countries.

Run `51693f9a-19fb-4900-bfb8-3254022e79ae` emitted four market groups for 48
goods with zero gaps, filters, drops, or invalid records. Small Arms matched the
save exactly: price 37.01001, previous price 37.00000, supply 40.24960, demand
320.48798, real demand 25.16080, actual sold 20.36069, and world-market sold
4.80011.

No GDP or value-added record is emitted yet. A future versioned production
account must distinguish gross physical output, realized sales, intermediate
input consumption, inventory change, worker compensation, capitalist income,
and transfers such as subsidies. Factory output and global market pools also
need a verified daily phase alignment before they can be joined by date. Until
actual factory input consumption and producer-specific sold quantity are mapped,
`sales_income / market_price` and `output * market_price` remain analytical
candidates rather than telemetry contracts.

Four-day run `a4edf016-5d61-40e0-83e7-37b9df278e2a` disproved both same-day and
one-day-lag producer-sales derivation: inferred sold/output ratios ranged from
0.14 to 2.16. Factory output, realized income, and market price therefore need a
verified clearing phase or producer inventory field before joining. Across the
same run, every factory and day satisfied the exact cash-flow identity
`budget_delta = sales_income + investment_income - market_spending - paychecks`.

`province.rgo` emits selected groups from the bounded global `CStateEmployment`
vector. Identity reports the production-type key and output-good key/ordinal;
employment reports capacity and assigned workers; production reports recipe
output per size, the provisional base-size field, output efficiency, and
throughput; finance reports RGO income. Quantity fields and modifiers use the
32,768 scale, while income uses the 32,768,000 money scale.

Berlin 549 resolved `orchard`/`fruit`, capacity 195,625, employment 110,896,
output efficiency 1.90, and throughput 0.9703. Görlitz 687 resolved
`coal_mine`/`coal`, capacity 60,000, employment 54,730, output efficiency 1.45,
and throughput 0.5472. The effective RGO-size modifier and stored gross-output
quantity remain unmapped. `base_output_per_size_raw * base_size_raw_candidate`
therefore does not reproduce every UI base-output value and must not be treated
as a complete production formula.

One-day run `ea77637b-f661-47f5-b85f-18d48de2a5a0` emitted all four groups for
Berlin and Görlitz: eight accepted records with zero filters, drops, or invalid
results. The benchmark stopped on the exact requested date.

Candidate container metadata is validated before emission. Limits are 64
province-building slots, 4,096 province constructions, 100,000 country units or
scheduled mobilizations, 512 entries in each country relation vector, and 4,096
ongoing wars. Factory capture allows 512 states, 64 factories per state, 4,096
factories, 1,024 employment assignments per factory, and 16,384 input records
per selected country. Negative list sizes, inconsistent null pointers, reversed or
misaligned vector pointers, exceeded limits, and non-normalized candidate tags
suppress the affected selected-country poll and increment the family's `invalid`
count. Unselected factory groups are not traversed or validated.

`world.daily` is emitted once per selected sample date before country filtering.
It reports `country_slot_count`, `ai_scheduler_entry_count`, and
`human_control_present` with `provisional` quality. Country slots include
non-playable engine entries, and scheduler entries are not asserted to equal a
count of AI-controlled countries. These names expose the observed containers
without inventing stronger gameplay semantics.

`province.daily` is an opt-in provisional snapshot keyed by numeric province
ID. Its candidate fields come from the historical `CProvince` layout and remain
named as candidates until broader runtime correlation is complete. A one-day
vanilla probe (`dd4c7396-4fa0-4598-9b76-e1d43874d690`) correlated province ID,
owner, controller, colonial level, and life rating against `benchmark.v2` for
provinces 1, 425, and 549. Berlin (549) reported infrastructure raw `160` while
the save contained railroad level 1. That pair is evidence of correlation, not
yet a supported conversion from raw infrastructure to displayed level.

`province.production` is a provisional inventory of the province-level building
definition vector and construction list. `building_slot_count_candidate` is not
an active-building or factory count. Berlin reported three slots while the save
serialized fort and railroad entries and omitted naval base. Factories are
state-level objects exposed separately by `state.factory`; the province vector
remains unrelated to factory count.

The opt-in POP families use a bounded snapshot shared by all POP rules due on
the same game date. `pop.economy` and `pop.demographics` emit one record per POP,
identified by candidate province ID, candidate POP-type ID, and a snapshot-local
index. The index is not durable across dates. `pop.aggregate` emits one lower-
volume record per province and POP-type pair, summing POP count, size,
employment, money, and savings. It intentionally aggregates across culture and
religion because stable identifiers for those dimensions are not yet mapped.

POP money, savings, interest cash flow, and total cash flow reuse fields already
verified by the interest-fix work. Size, employment, consciousness, militancy,
literacy, province ID, and POP-type ID remain explicitly named as candidates.
The three rate fields are raw 48.15 fixed-point values. A one-day vanilla probe
(`4f40b617-b56a-4478-81b1-9e35b1d90b4e`) emitted 23 Berlin POPs in each detail
family with no drops or invalid records; its size and displayed rate fields
correlated with `benchmark.v2`.

A second one-day run (`3ac4d510-7dbe-45af-9701-1902379785df`) enabled all three
POP families together. It accepted 23 economy records, 23 demographic records,
and 10 aggregates with no drops or invalid records. Snapshot collection took
90,224 microseconds once; the later two rules reported zero additional snapshot
collection time because they reused the same date's bounded copy.

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
explicitly counted as dropped. Ordinary detail writes leave 16 slots, or one
eighth of smaller queues, reserved for reliable records. Explicit country,
province, or POP-province allowlists of at most 16 entities use reliable
in-memory publication;
unfiltered high-cardinality polling remains nonblocking. `telemetry.progress`, `world.daily`,
date-regression, economic snapshot, and opt-in interest-fix result records use
reliable bounded publication so lock contention alone cannot split their
evidence. The queue remains bounded, and publication still performs no file I/O.
The sole worker writes complete JSON Lines incrementally and flushes at least
once per second. Explicit plugin unload drains accepted records and waits for
the worker to empty the queue and join. It then appends a best-effort final
summary using post-drain statistics, performs the final flush, and closes the
file.

Before an opt-in native exit, `campaign_runner` resolves
`SmedleyTelemetryDrainV1` from the already loaded sibling telemetry module and
uses one five-second monotonic deadline. The API obtains exclusive sink
ownership, which waits for entered external calls and blocks new ones. It then
marks telemetry as draining, removes the daily handler, and waits for any
entered daily callback before fixing and draining the accepted queue boundary. The
worker empties that queue and joins; `telemetry.summary` is then appended when
lifecycle capture is enabled, followed by the final flush and file close.

Native exit follows a completed drain. It also follows an unavailable result for
compatibility when telemetry is absent, inactive, or lacks the drain symbol, but
that path has no final-summary or telemetry-durability guarantee. Busy, timeout,
or failure leaves the campaign paused and open. Timeout is not cancellation: if
shutdown has begun, ingress remains disabled; an already-started worker drain
continues. A later non-recursive call retries incomplete coordination or waits
for and joins the same drain rather than reopening ingress or starting a second
worker. If the deadline expires before exclusive sink ownership, no shutdown
transition occurred. The kernel still has no generic pre-exit plugin callback
and does not unload plugins from `DllMain`. Abrupt or non-Smedley exits can lose
up to the latest userspace queue and the one-second flush interval. Completed
prior lines remain independently parseable and no callbacks interleave output.

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

`SmedleyTelemetryDrainV1(timeout_ms)` is the optional process-exit coordination
boundary. Results are `unavailable`, `completed`, `busy`, `timeout`, and
`failed`. `timeout_ms` is milliseconds and `UINT32_MAX` means no deadline. A
finite call computes one monotonic deadline for sink ownership, drain-state
ownership, producer quiescence, queue consumption, worker join, final summary,
flush, and close. Completed means extension and daily ingress are stopped,
entered producers returned, every accepted payload record was consumed, the
worker joined, any enabled final summary was written, and the file was flushed
and closed. The summary is excluded from its own `accepted` and `written`
counters. Busy rejects a concurrent drain immediately. Timeout never detaches a
worker; once shutdown starts it does not reopen ingress, and a later call resumes
or joins the same drain. Callers must not invoke drain recursively or from a
telemetry producer. This is a telemetry-specific capability, not a generic
kernel plugin-lifecycle callback.

Runtime run `4d3d4e44-b4e5-4d7f-b10a-b50bd5b96cfb` exercised the bundled drain
before native quit. Its valid lifecycle trace ended with `benchmark.completed`
at sequence 10 and `telemetry.summary` at sequence 11; the summary reported 10
accepted, 10 written, zero dropped, and no write failure. The process exited and
the source save was unchanged.
This acceptance covers the bundled completed-drain path only; it does not
exercise timeout retry or legacy-plugin compatibility.

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
