# Telemetry

## Observation service API

`telemetry_observation_api.h` is the separately discoverable C observation API
for native telemetry consumers. It leaves the unchanged
`telemetry_game_api.h` v1 layouts and table intact, and does not schedule callbacks: consumers use
`event_api.h` daily events, then read on that callback's game thread.

The v1 observation table provides checked, caller-buffered reads for full world
market fields and ongoing wars; daily-event tag resolution; country metrics,
military, diplomacy, economy, and bounded creditor destinations; detailed POP
economy/demographics; POP identity, needs, artisan records and inputs; factory
records and bounded inputs; province daily and production fields with independent
availability flags; and RGO records by province ID. All records are
fixed-width, pointer-free, versioned, and require zero reserved fields.

Observation sessions open against an existing v1 telemetry session and are
owner-thread and game-session-epoch bound. POP and factory handles share that
parent session's opaque identity namespace with v1 hook `entity_id` values.
The high 32 bits are a nonzero parent-session ID and the low 32 bits are an
opaque per-session entity serial; native address bits are never encoded. Parent
session IDs never exceed 32 bits, so the encoding cannot truncate or alias.
Handles are invalidated when the parent closes or stales. Factory IDs exist solely
for hook correlation, alongside a per-read `observation_index`; no native
address is exposed. Market, artisan, factory, and RGO reads require a nonzero,
known group mask and traverse only requested reader groups. Every bulk
read uses caller-owned bounded buffers and returns `TRUNCATED` after writing the
available prefix. `INVALID_SOURCE` means the checked game reader rejected source
metadata; `UNAVAILABLE` means the requested game state is absent.

The service does not implement cadence, date regression, aggregation, lifecycle
diffing, valuation, reconciliation, or JSON shaping. Those policies remain
plugin-side. Calls make no allocations in game callbacks; adapters use bounded
static scratch storage and retain no native game address outside the session's
opaque POP map.

`telemetry` is Smedley's first-party, opt-in native JSON Lines plugin. It is a
trusted DLL, not a sandbox. Enable `telemetry_enabled` and select
`plugins/telemetry.toml`; the shared launcher preflight rejects an enabled
profile without plugin ID `telemetry`. It requires no user C++.

The plugin resolves `event_api`, `logging_api`, `telemetry_game_api`, and
`telemetry_observation_api` dynamically from `smedley_kernel.dll`; it does not
link against the kernel. Unavailable, stale, wrong-thread, invalid-source, and
truncated C reads are missing observations, never inferred numeric zeroes.

`smedley_telemetry_registry` is the immutable compiled authority for capture
families, selectable fields, identity, mapping/schema facts, filters, cadence
constraints, collector ownership, cost class, and admission priority. Launcher
validation and UI, plugin validation/runtime planning, and strict trace checks
query that same registry; it deliberately exposes no game offsets or raw fields.
Its event schemas use comma-separated field names in collector emission order:
`-` is an empty object, and a trailing `?` marks a field that an event may omit.
Each family's `events` array is the complete event-level catalog, including
entity and payload keys; the family table below is the user-facing selection
catalog rather than a duplicate event schema.

Contributors adding or changing telemetry update the registry entry, owning
collector emission, launcher-facing selection behavior, strict trace handling,
tests, and this document together. Registry cadence, required-filter, event
ownership, schema membership, admission, and hook-planning facts are enforced.
Cost class, collector identity, mapping, and quality remain descriptive metadata
that consumers report and verify where applicable.

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
| `telemetry_start_date_raw` | integer, optional | none | Inclusive legacy raw lower bound. The GUI uses `DD-MM-YYYY`. |
| `telemetry_end_date_raw` | integer, optional | none | Inclusive legacy raw upper bound; cannot precede start. The GUI uses `DD-MM-YYYY`. |
| `telemetry_sample_days` | integer | `1` | Legacy sample interval in days, 1 through 365. Explicit capture rules use their own cadence. |
| `telemetry_queue_capacity` | integer | `1024` | 64 through 32768 fixed record slots. |
| `telemetry_overwrite` | boolean | `false` | Required to replace an existing output. |
| `telemetry_gold_to_cash_rate` | number, optional | none | Required by `country.economy`; greater than 0 through 1000. Use the active mod's `GOLD_TO_CASH_RATE` (`0.5` in vanilla, `1.0` in GFM). |

Profiles may instead add up to 32 explicit capture rules. A family may appear
once. Explicit rules replace legacy `state` sampling; lifecycle category
selection remains global. Capture rules require the `state` category.

```toml
telemetry_gold_to_cash_rate = 0.5

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

[[telemetry_captures]]
family = "country.economy"
cadence = "yearly"
fields = ["totals", "components", "per_capita"]
country_tags = ["ENG", "FRA", "PRU", "USA", "CHI"]
province_ids = []

[[telemetry_captures]]
family = "pop.cashflow"
cadence = "daily"
fields = ["summary", "account", "components"]
country_tags = ["PRU"]
province_ids = []

[[telemetry_captures]]
family = "pop.cashflow.aggregate"
cadence = "daily"
fields = ["summary", "account", "components"]
country_tags = []
province_ids = []
```

Each rule accepts `family`, `cadence`, `fields`, `country_tags`, `province_ids`,
and optional legacy raw `start_date_raw` and `end_date_raw`. The GUI edits the
fixed `telemetry_capture_v1` schema with human `DD-MM-YYYY` bounds. Empty `fields` selects every
field in that family. The registry distinguishes a supported filter from a
required filter: a supported empty filter selects all entities, while a required
filter must contain at least one country or province. Supplying a filter kind
that a family does not support is rejected by both launcher and plugin. Country
filters are valid for all `country.*` families,
`state.factory`, `province.rgo`, `pop.artisan`, `pop.economy`,
`pop.demographics`, `pop.identity`, `pop.needs`, `pop.aggregate`, `pop.lifecycle`, `pop.cashflow`, and
`pop.cashflow.aggregate`. Province filters are valid for `province.daily`,
`province.production`, `province.rgo`, `pop.artisan`, `pop.economy`,
`pop.demographics`, `pop.identity`, `pop.needs`, `pop.aggregate`, `pop.lifecycle`, and `pop.cashflow`. An empty entity filter means every
entity, including daily all-province or all-POP capture when explicitly
requested. Country families filter the country supplied by each
`DailyUpdateEvent`; they do not initiate a separate country traversal. Global
families such as `world.military` have no country entity and do not support
country filters; profiles that supply them are rejected before launch.

| Family | Selectable fields |
| --- | --- |
| `world.daily` | `country_slot_count`, `ai_scheduler_entry_count`, `human_control_present` |
| `world.economy` | record groups `health`, `capacity`, `holdings`, `credit` |
| `country.daily` | `treasury_raw`, `treasury` |
| `country.metrics` | record groups `power`, `politics` |
| `country.economy` | interval groups `totals`, `components`, `per_capita` |
| `country.military` | `unit_count_candidate`, `mobilized_candidate`, `scheduled_mobilization_count_candidate`, `leadership_candidate_raw`, `military_ranking_candidate` |
| `world.military` | `ongoing_war_count_candidate` |
| `country.diplomacy` | record groups `status`, `relations` |
| `state.factory` | record groups `identity`, `employment`, `production`, `finance`, `inputs`, `flows`, `sales` |
| `world.market` | record groups `price`, `supply`, `demand`, `sales` |
| `province.daily` | `owner_tag_candidate`, `controller_tag_candidate`, `colonial_level_candidate`, `life_rating_candidate`, `infrastructure_candidate_raw` |
| `province.production` | `building_slot_count_candidate`, `construction_count_candidate` |
| `province.rgo` | record groups `identity`, `employment`, `production`, `finance`, `modifiers`, `sales` |
| `pop.artisan` | record groups `identity`, `production`, `inputs`, `finance`, `flows`, `sales` |
| `pop.economy` | `money_raw`, `savings_raw`, `interest_cash_flow_raw`, `total_cash_flow_raw` |
| `pop.demographics` | `size_candidate`, `employed_candidate`, `consciousness_candidate_raw`, `militancy_candidate_raw`, `literacy_candidate_raw` |
| `pop.identity` | `pop_type_tag_candidate`, `culture_tag_candidate`, `religion_tag_candidate` |
| `pop.needs` | `life_satisfaction_candidate_raw`, `everyday_satisfaction_candidate_raw`, `luxury_satisfaction_candidate_raw` |
| `pop.aggregate` | `pop_count`, `size_candidate`, `employed_candidate`, `money_raw`, `savings_raw` |
| `pop.lifecycle` | record groups `summary`, `appeared`, `disappeared`, `scope_changed` |
| `pop.cashflow` | record groups `summary`, `account`, `components` |
| `pop.cashflow.aggregate` | record groups `summary`, `account`, `components` |

The GUI's family and field choices come from the same bounded shared catalog
used by launch validation. Cadences are `daily`, `weekly`, `monthly`, and `yearly`. Weekly capture is
anchored to the first eligible observed date and repeats every seven game days.
Monthly and yearly capture emits on the first observed date in each Victoria II
calendar period. The game calendar has 24 raw units per day, fixed 365-day
years, no leap day, and epoch `-5000.1.1`; `1836.1.2` is raw `59883384`.
Date regression resets each rule independently.
Producer `sales` capture is daily only because realized quantity requires the
previous day's closing inventory. Profiles selecting `sales` at another cadence
are rejected before launch and again by the plugin parser.
Both POP cash-flow families are also daily only. Individual `pop.cashflow`
requires at least one country or province filter because it emits high-cardinality
records; `pop.cashflow.aggregate` may cover every country.
`pop.lifecycle` is daily only because it reconciles consecutive complete POP
stocks; a gap or date regression restarts warm-up instead of inventing events.

Disabled or absent capture families do not install their hooks or traverse their
observation groups. A lifecycle-only configuration does not open or read game
observation state merely to report progress. State records remain bounded
by registry admission policy. `ReliableTerminal` families always use reliable
in-memory admission. `Important` families use it only when the combined country
and province allowlist contains 1 through 16 entries; empty/unbounded or larger
filters remain best-effort. `BestEffort` families are never promoted. Lifecycle
and terminal health records continue through the reliable queue path.
`telemetry.family.summary` includes
accepted and dropped formatted-record byte totals as well as record counters and
collection time. Nonzero dropped bytes make strict family health unhealthy.
No record ABI, field capacity, queue capacity, or fragmentation policy changed:
there is no retained workload evidence that would justify those changes.

`country.economy` treats cadence as an accounting interval rather than a
point-in-time sampling trigger. It reads daily factory, RGO, artisan,
population, and market state into fixed country accumulators, then emits the
completed daily, weekly, monthly, or yearly period and resets the accumulators.
Daily cadence therefore emits one compact country snapshot per day; yearly
cadence emits the sum of observed daily value added for the year rather than a
single January production rate. Instrumentation memory remains bounded by 512
country slots, 64 goods, and the existing factory boundary arrays.

The family emits:

- `country.economy.interval`: period bounds, observed/expected days, invalid
  days, completeness, and average population.
- `country.economy.total`: nominal GDP, base-price real GDP, base date, and the
  configured precious-metal cash rate.
- `country.economy.component`: factory, RGO, and artisan nominal/real value
  added.
- `country.economy.per_capita`: nominal and real period GDP divided by average
  population.
- `country.economy.quality`: count of factory snapshots whose cached output was
  positive although no settlement boundary ran during that day.

The first factory observation cannot establish consumption because it has no
previous closing stock. Its country/day is emitted with `complete=false`, not
omitted. A period remains present when a date, country, or producer boundary is
incomplete; consumers must use `observation_days`, `expected_days`,
`invalid_days`, and `complete` rather than treating partial GDP as verified.
Factory value added becomes complete after consecutive settlement boundaries.
After warm-up, absence of a factory settlement boundary means no verified
factory production for that day; the plugin does not value the factory's
cached `output_raw`. Such candidates remain visible through
`unsettled_output_candidates`. A present but unreconciled boundary, or any
hook capture drop, makes the country/day incomplete.
Real GDP uses market prices from the first successfully observed economy day as
its fixed base. Date regression clears both period accumulators and base prices.

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
`mappings/evidence/telemetry.md`; profiles and plugin arguments retain raw values
for compatibility, while end-user GUI bounds use human dates.

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
`filtered`, `dropped`, `invalid`, `collection_us`, `accepted_bytes`, and
`dropped_bytes`. Rejected finalized records contribute their attempted byte
count; filtered, invalid, and unavailable records contribute zero bytes. These
counters describe producer results, not records subsequently written by the
shared worker.

At session start, reliable `telemetry.capture.rule`,
`telemetry.capture.field`, and `telemetry.capture.country` records describe the
configured cadence, selected fields, date bounds, and entity filters. Strict
derived exports use these records to distinguish a genuine zero from a family
that was configured with incomplete scope. `projected_entity_count` is `-1`
only when `projection_bounded` is false; `operational_admission` reports the
selected delivery policy (`best-effort` or `reliable`) separately from the
registry `admission_priority`.

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
`inputs` emits one record per good present in either the factory stockpile or
requested-input pool. `stockpile_raw` is the current holding. The engine writes
the recipe requirement remaining after stockpile subtraction and a zero clamp
into this pool before market settlement; telemetry samples the retained value
after the daily boundary as `requested_raw`. Both use the 32,768 quantity scale.
`requested_raw` is demand, not fulfilled purchases or physical consumption.

Run `8b355d27-c585-4fb7-8c8f-f79caf33c025` validated two daily snapshots for
all seven Prussian factories. It accepted 92 factory records with zero invalid
or dropped results. On the second date, valuing every factory's
`requested_raw` quantities at same-date market prices reproduced its
`market_spending_expense_raw`; each of the seven differences was below 0.000081
currency. For Glass, the detailed difference was 0.000056 currency.
This establishes purchase demand and cost, but a shortage case is still needed
to distinguish requested from fulfilled quantities.

Belgian run `a859d772-6846-4749-9fb6-d12467774133` supplied that shortage case.
It accepted 123 factory records across six factories and three dates with zero
invalid or dropped results. Cement's requested-input value exceeded actual
market spending by 0.09938 currency on the second date and 0.41326 on the third;
Canned Food missed by 0.23857 on the third date. Fully supplied factories still
matched within 0.00009. The retained pool therefore records requested demand,
not fulfilled purchases.

Daily intermediate consumption value can be estimated without assigning the
fulfilled purchase shortfall to individual goods:

```text
intermediate consumption = market spending
                         + opening input stock valued at current prices
                         - closing input stock valued at current prices

factory value added = current output * current output price
                    - intermediate consumption
```

Using current prices for both inventory endpoints avoids introducing
revaluation into the stock-change term. The estimate assumes the snapshots
bracket the same purchase/consumption interval as market spending and that no
other stock transfer or write-off occurs. Market spending remains the engine's
transaction value, which can differ from current-price valuation when prices
move. Thirty-day Belgian run `3aacdf40-e989-4d52-92c8-f052fa2a78d8` captured
1,230 accepted factory records with zero invalid or dropped results. Its 29
complete daily intervals retained the same six factories through sustained
shortages. Every one of the 174 factory-interval results had positive value
added. Country totals averaged 119.3726 gross output, 99.8497 intermediate
consumption, and 19.5229 value added; daily value added ranged from 16.2549 to
20.7850. This supports the interval alignment but does not turn transaction
spending and current-price inventory valuation into the same price basis, so
the derived result remains provisional.

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

The optional factory `sales` group emits `state.factory.sales.summary` for every
sampled factory. `settlement_seen`, `settlement_count`, and `complete` expose
missing, duplicate, and invalid finance boundaries rather than silently omitting
the factory. A complete summary has one paired `sales.quantity` and
`sales.revenue` record. Quantity reports output-good ordinal, opening inventory,
production, realized sold quantity, and closing inventory, all at the 32,768
scale. Revenue reports settlement proceeds at the 32,768,000 money scale.
The enforced identity is `sold = opening + produced - closing`.

RGO and artisan sales use the same three-record shape under
`province.rgo.sales.*` and `pop.artisan.sales.*`. Factory summaries contain
`settlement_seen`, `settlement_count`, and `complete`; RGO and artisan summaries
contain `settlement_seen`, `opening_inventory_seen`, and `complete`. Every
complete `.quantity` record contains `output_good_ordinal`,
`opening_inventory_raw`, `produced_raw`, `sold_raw`, and
`closing_inventory_raw`. Every `.revenue` contains `proceeds_raw`; RGO and
artisan revenue also retains `percent_sold_domestic_raw` and
`percent_sold_export_raw`. Factory identity is country/state/type, RGO identity
is country/province, and artisan identity is country/province/`pop_id`.

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

Country GDP is a strict offline production account exported by `smedley_trace
country-gdp`; no in-game GDP record is invented. It combines direct factory
consumption, ordinary RGO gross output, precious-metal output valued using the
active mod's explicit `GOLD_TO_CASH_RATE`, artisan recipe consumption, and
resident POP sizes. Realized sales, worker
compensation, capitalist income, inventory change, and subsidies remain
separate distribution and financing measures rather than production value.

Four-day run `a4edf016-5d61-40e0-83e7-37b9df278e2a` disproved deriving sales
from output, income, and market price alone: inferred sold/output ratios ranged
from 0.14 to 2.16. The later finance-boundary mapping resolves this with direct
opening and closing output inventory. Across the earlier run, every factory and
day satisfied the exact cash-flow identity
`budget_delta = sales_income + investment_income - market_spending - paychecks`.

`province.rgo` emits selected groups from the bounded global `CStateEmployment`
vector. Identity reports the production-type key and output-good key/ordinal;
employment reports capacity and assigned workers; production reports recipe
output per size, base size, output efficiency, throughput, and gross output;
finance reports RGO income. Quantity fields and modifiers use the
32,768 scale, while income uses the 32,768,000 money scale.
`base_size_raw_candidate` remains as the v1 alias of verified `base_size_raw`.
The daily `sales` group adds a settlement summary and, after one warm-up
observation, paired quantity and revenue records. It reconciles previous
leftover inventory plus current gross output against current leftover inventory.
`opening_inventory_seen` distinguishes warm-up, date gaps, ownership changes,
and output-good changes from a failed inventory reconciliation.
Revenue retains the engine's domestic and export sold fractions as raw evidence;
it does not claim that either fraction directly allocates the emitted total
quantity. The export-clearing value can exceed `32768`; unlike the domestic
fraction, it is not validated as a bounded percentage.

Berlin 549 resolved `orchard`/`fruit`, capacity 195,625, employment 110,896,
output efficiency 1.90, and throughput 0.9703. Görlitz 687 resolved
`coal_mine`/`coal`, capacity 60,000, employment 54,730, output efficiency 1.45,
and throughput 0.5472. Gross output follows the engine's fixed-point operation
order: multiply efficiency by throughput, recipe output, then size, truncating
by 15 fractional bits after each multiplication. This gives raw 676,588
(20.648 displayed) for Berlin and 187,206 (5.713 displayed) for Görlitz.
The `modifiers` group reports the owner POP population, total state RGO employment
capacity, and their fixed-point ratio as `owner_output_modifier_raw`. Berlin's
4,275 aristocrats / 457,875 capacity gives raw 305 (0.0093 displayed), while
Görlitz's 848 / 60,000 gives raw 463 (0.0141 displayed).

One-day run `ea77637b-f661-47f5-b85f-18d48de2a5a0` emitted all four groups for
Berlin and Görlitz: eight accepted records with zero filters, drops, or invalid
results. The benchmark stopped on the exact requested date.

Follow-up run `b08cf98f-3ece-4467-9aec-6afe3284ab14` verified the exact gross
output fields for both provinces with the same zero-invalid, zero-drop result.
Run `e5f0655c-7a87-4342-a77a-96c2b2d47492` added the separate modifier records
and accepted all ten selected records with zero invalid or dropped results.

`pop.artisan` is country- and province-filterable. `identity` reports the
persistent POP ID, production type, and output good. `production` reports the
recipe output, actual `current_producing_raw` factor, and their fixed-point
product. `inputs` reports current stock and the active recipe coefficient
`need_raw` per good. Intermediate consumption is the recipe coefficient times
`current_producing_raw`, truncating by 15 fractional bits, not a stock-delta
estimate. For Berlin POP 8845, raw factor 23,565 and recipe inputs 65,536,
65,536, and 163,840 produce consumed quantities 47,130, 47,130, and 117,825;
the direct settlement trace independently left residual stocks 2, 2, and 5
from prior closing stocks 47,132, 47,132, and 117,830.

The optional artisan `flows` group records post-consumption stock, pre-purchase
stock, and primary/secondary deliveries at the supported executable's market
settlement callsites. It validates the recipe account but is not used to derive
consumption across recipe changes: artisans can switch recipes, which
legitimately replaces the input-good set between dates. A country-filtered
three-day run emitted complete four-boundary settlements for all nine selected
Berlin artisans after warm-up, with 40 flow records and no gaps, drops, or
invalid records.

Artisan `sales` uses the same summary, quantity, and revenue shape. POP ID,
province, country, and output-good continuity are required across consecutive
days. First observations, date gaps, and recipe switches emit `complete=false`
without a quantity or revenue pair.

Five-day run `662343cc-fc1a-4ad9-a95a-4215541612c6` emitted 21 factory,
184 RGO, and 498 artisan quantity/revenue pairs, plus 230 RGO and 630 artisan
summaries. All producer families reported zero invalid or dropped records. The
missing first-day RGO and artisan pairs are explicit inventory warm-up, not
zero sales. Final thirty-day run `dc58f6ce-369a-48c3-a1ad-fdae369b0193` exported
5,152 strict rows: 196 factory, 1,334 RGO, and 3,622 artisan accounts, with zero
gaps, drops, writer failures, family invalid records, or failed reconciliations.

Annual run `7212971a-f622-488a-9283-e02056e5c001` captured 387,773 records.
Factory and RGO remained healthy; a Machine Parts factory explicitly reported
199 post-warm-up days without a settlement, exercising the incomplete/shutdown
path without inventing zero sales. All 44,881 emitted artisan pairs reconciled,
but the family reported 213 other invalid collection attempts later in the run,
so strict export correctly rejected the annual trace. Producer events remain
`provisional` until those long-run artisan collection failures are mapped.

Metz 412 verifies the special precious-metal case. Its runtime RGO emitted
gross output raw 262,128 (7.9995 units) and income raw 7,385,056,000, exactly
matching save `last_income=225374.02344` after save serialization. That income
is not gold cash creation. Precious-metal GDP is quantity times the active
mod's `GOLD_TO_CASH_RATE`; vanilla defines 0.5 and GFM defines 1.0. The strict
exporter requires this mod-dependent rate explicitly whenever a selected
country produces precious metal.

Candidate container metadata is validated before emission. Limits are 64
province-building slots, 4,096 province constructions, 100,000 country units or
scheduled mobilizations, 512 entries in each country relation vector, and 4,096
ongoing wars. Factory capture allows 512 states, 64 factories per state, 4,096
factories, 1,024 employment assignments per factory, and 16,384 input records
per selected country. Direct flow capture has a separate 512-factory daily
limit (2,048 hook records); exceeding it increments `invalid` and makes strict
export fail. Artisan purchase-flow capture is country-filtered to at most 16
tags and buffers at most 8,192 hook records per date. Negative list sizes,
inconsistent null pointers, reversed or
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
vanilla runtime validation (`dd4c7396-4fa0-4598-9b76-e1d43874d690`) correlated province ID,
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
identified by candidate province ID, candidate POP-type ID, and the engine
`pop_id`; country filters are applied through the province owner without
duplicating that derived identity in the eight-field record. The ID is stable
across ordinary consecutive observations. A migration-like runtime correlation
retained type, culture, religion, and size while moving a French craftsmen
cohort through three provinces under IDs `27606`, `28227`, and `28266`, showing
that location transitions can replace the ID. The native migration boundary
itself is not yet instrumented.
Behavior across promotion, demotion, split, merge, deletion, and pointer reuse
remains to be established before treating it as a permanent historical
identity. `pop.aggregate` emits one lower-
volume record per province and POP-type pair, summing POP count, size,
employment, money, and savings. It intentionally aggregates across culture and
religion; use `pop.identity` when those cohort dimensions are required.

`pop.identity` emits the normalized POP-type, culture, and religion keys for
each engine `pop_id`. The engine ID identifies one current POP object; the
dimension tuple is descriptive, not a universal substitute for it. In
particular, Victoria II can retain multiple artisan POPs with the same province,
type, culture, religion, and even production recipe. Consumers may aggregate by
the dimensions when that is the intended analysis, but must not infer that one
tuple always denotes one POP.

`pop.needs` emits the three bounded 48.15 satisfaction candidates associated
with life, everyday, and luxury needs. They are proportions from zero through
32,768, not costs, quantities requested, quantities purchased, or cash spent.
The field names remain candidates until broader POP types and visible UI values
are correlated. The family uses the same province/type/`pop_id` identity and
country/province filters as the individual economy and demographic families.

`pop.lifecycle` compares consecutive complete snapshots by engine `pop_id`.
`pop.lifecycle.observed_appeared` and `.observed_disappeared` mean only that an
ID entered or left the observed stock. `.scope_changed` reports changes in
candidate country, province, or POP type for an ID present on both dates. These
records do not claim migration, promotion, demotion, split, merge, birth, or
death without matching identity and conservation evidence. The correlated
migration-like case appeared as a size-preserving disappearance/appearance pair;
`scope_changed` is reserved for a surviving ID. The daily summary reports
opening/closing counts and every diff class. The first complete captured POP
snapshot is emitted as a warm-up summary with no opening stock; `opening_seen`
and `complete` are both false. Country or province filters affect detail events,
while the summary reconciles the complete world stock.

The daily POP cash-flow families observe every call to the supported
`CPop::GiveMoney` boundary and compare those calls with consecutive POP money
balances. Component indices are `0 needs`, `1 welfare`, `2 salary`, `3 expenses`,
`4 events`, `5 projects`, `6 bank`, and `7 interest`. Each component reports the
posted amount and the actual money change after engine clamping. The account
identity is `closing_money_raw - opening_money_raw = money_delta_raw + residual_raw`.
The first date is a warm-up and has no account record.

`pop.cashflow` identifies a POP with durable runtime `pop_id` plus its current
country, province, and candidate type. Promotion, demotion, split, merge, and
other direct redistribution paths can occur outside `GiveMoney`, so an individual
residual is evidence rather than an invalid family record. `pop.cashflow.aggregate`
emits both candidate POP-type accounts and country-total accounts. Type residuals
can be equal-and-opposite identity transfers; a country account is reconciled only
when its residual is zero and the bounded hook reported complete capture. Taxes
and tariffs have no independently verified POP field or cash-flow index and are
therefore not reported as separate components.

The individual events are `pop.cashflow.{summary,account,component}`. Their
identity is country, province, candidate POP type, and `pop_id`; component
records add `cash_flow_index` and `component`. Individual summaries report
`opening_money_seen`, `capture_complete`, `reconciled`, and `call_count`.
`pop.cashflow.aggregate.{summary,account,component}` replaces individual
identity with country and candidate POP type, while
`pop.cashflow.country.{summary,account,component}` uses country alone.
Aggregate and country summaries report `opening_pop_count`, `closing_pop_count`,
`opening_money_seen`, and `reconciled`. Every account reports
`opening_money_raw`, `closing_money_raw`, `money_delta_raw`, and `residual_raw`;
every component reports `posted_raw` and `money_delta_raw`. The CSV exporter
represents country rows with `pop_type_id_candidate=-1`; JSONL country events do
not invent that entity.

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

Some selectable groups also emit status or boundary events that are easy to
misread without the registry catalog:

| Selection | Additional events | Interpretation |
| --- | --- | --- |
| `state.factory` `flows` | `state.factory.input.consumption.summary`, `state.factory.input.consumption` | The summary states whether a valid daily consumption boundary was observed. Detail records contain only nonzero consumed goods and never imply zero for omitted goods. |
| `pop.artisan` `finance` | `pop.artisan.finance` | Reports the five finance candidates in the registry payload schema for an active artisan snapshot. |
| Any selected `pop.artisan` group | `pop.artisan.inactive`, `pop.artisan.invalid` | `inactive` identifies a POP that is readable but has no active artisan production. `invalid` reports a failed artisan read with a stable reason and offending raw value; it also increments family invalid health. Neither event invents production or finance values. |

POP money and POP savings are different storage categories. Savings and
creditor/state values may be financial claims or bookkeeping aggregates; adding
them to liquid balances can double-count value. `bank_interest_accumulator_raw`
is the verified temporary destination of charged interest, not a national-bank
cash balance. The plugin deliberately does not emit `world_money_supply`.

With `interest_fix_debug` enabled and both `interest_bug_fix` and `telemetry`
selected, the fix emits:

| Event | Payload | Contract |
| --- | --- | --- |
| `interest.fix.health` | status, flags, state ID, POP count, verified POP count, callback microseconds | One record for initialization and each nonzero state-pool outcome. Complete detail remains in `interest_bug_fix.csv`. |
| `interest.fix.value` | state ID, consumed native state pool, derived POP payout, or initialization discard | Emitted for successful initialization and complete state payouts. Failed or partial payouts emit no value record. |

Both records use `verified-runtime` quality. They use the reliable bounded
emitter so lock contention alone cannot hide a fix result; unavailable,
filtered, full-queue, or invalid telemetry remains independent of mutation and
never changes whether the fix pays POPs. `interest_bug_fix.csv` records the two
telemetry result codes for independent diagnosis. This guarantee requires the
bundled `SmedleyTelemetryEmitReliableV1`; an older compatible telemetry plugin
without that symbol receives best-effort nonblocking publication.

The production interest fix creates no CSV, worker thread, or interest telemetry.
Enable its advanced **interest-fix diagnostics** setting (CLI:
`--interest-fix-debug`) only for a bounded investigation. That mode opens and
truncates `<GAME_DIR>/interest_bug_fix.csv`; telemetry output paths and overwrite
policy do not change that fixed diagnostic file.

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
smedley_trace factory-value-added run.jsonl factory-va.csv --country BEL
smedley_trace producer-sales run.jsonl producer-sales.csv --country FRA
smedley_trace pop-cashflow run.jsonl pop-cashflow.csv --country FRA
smedley_trace pop-stock-lifecycle run.jsonl pop-stock.csv --country FRA
smedley_trace country-gdp run.jsonl gdp.csv --country FRA --gold-to-cash-rate 0.5
smedley_trace export-trace run.jsonl eng.jsonl --country ENG
```

Malformed complete records, invalid envelopes, mixed run IDs, and non-increasing
sequences fail validation. An incomplete final line is warned and ignored.
Exports create a new file by default. Export commands accept `--overwrite`:
the tool writes and flushes a sibling temporary file, then atomically replaces
the destination only after the complete source snapshot validates. It rejects
reparse paths, hard-linked destinations, and input/output aliases. CSV text
cells beginning with `=`, `+`, `-`, or `@` receive a leading apostrophe to avoid
spreadsheet formula interpretation; JSON numeric cells remain numeric.

The opt-in `state.factory` `flows` group installs three observational callsite
hooks for the supported executable. `state.factory.input.flow.summary` reports
whether each factory exposed its post-consumption stock, pre-purchase stock,
primary delivery, and secondary delivery boundaries, plus the settlement count.
`state.factory.input.flow` reports the four corresponding quantities for every
nonempty goods ordinal. Selecting ordinary `inputs` does not install these
hooks.

`factory-value-added` derives one country row for each complete daily interval.
The trace must contain daily `state.factory` production and direct flow groups
plus `world.market` prices. The first snapshot is normally a hook warm-up
boundary, so an N-day capture yields at most N-2 rows. For each input, direct
consumption is the previous flow's reconciled closing stock minus current
post-consumption stock;
the command values that physical quantity and current output at the closing
date's prices. It rejects sequence or date gaps, telemetry drops or writer
failure, missing or duplicate records, incomplete settlement boundaries,
negative consumption, failed stock-flow reconciliation, missing prices,
non-daily intervals, factory-set changes, unsupported mappings, and traces
without final healthy factory, market, and writer summaries instead of treating
unavailable data as zero. Rows use `verified-runtime` quality for the supported
executable and mapping.

When a factory skips purchase settlement, the opening boundary is its previous
post-consumption stock rather than a nonexistent delivery total. This preserves
consumption across shutdown, input shortage, or other no-purchase days.

`producer-sales` exports one row per complete factory, RGO, or artisan sales
account. It requires reconciled quantity/revenue pairs, matching summaries,
healthy terminal summaries for every captured producer family and the writer,
monotonic dates, and no sequence gaps. Incomplete warm-up rows are checked but
not exported. The CSV leaves inapplicable identity and market-fraction columns
empty; it never converts an unavailable split to zero. Export retains only one
game date in memory and spools validated rows to the transactional temporary
file, so memory does not grow with campaign duration.

`pop-cashflow` exports complete daily candidate POP-type and country accounts
from `pop.cashflow.aggregate`. Country-total rows use `pop_type_id_candidate=-1`.
The CSV includes opening and closing balances, the observed component total,
residual, reconciliation status, and posted/actual values for all eight
components. It requires healthy terminal aggregate and writer summaries, no
sequence or date gaps, matching summaries and accounts, and arithmetic
reconciliation. Warm-up rows are checked but not exported. Individual detail
remains in filtered JSONL because a world-wide per-POP CSV is intentionally not
a supported low-volume export.

`pop-stock-lifecycle` exports daily `pop.aggregate` stock rows together with
`pop.lifecycle` warm-up, reconciliation, appearance, disappearance, and scope-
change rows. The source capture must be daily, unbounded, and unfiltered for
both families, with aggregate POP count, size, and employment plus every
lifecycle group. This lets the exporter prove that aggregate POP counts equal
the lifecycle closing stock and that detail counts match each reconciled daily
summary before an optional `--country` output filter is applied. Warm-up is
retained with `opening_seen=false` and `complete=false`; it is not represented
as a zero-change interval. The command requires matching daily poll counts,
healthy family and writer summaries, monotonic dates, and no sequence gaps.
Unlike permissive inspection, it rejects an incomplete final line or any other
trace warning. A country filter retains current-country stock, appearance, and
disappearance rows; it retains a scope-change row when either its previous or
current country matches. Lifecycle summary rows always describe the complete
world stock and remain present in filtered output.
Lifecycle row names remain observational: appearance, disappearance, or a
scope change does not by itself prove birth, death, migration, promotion,
demotion, split, or merge.

`country-gdp` requires three consecutive daily snapshots and healthy terminal
summaries for `world.market`, `state.factory`, `province.rgo`, `pop.artisan`,
`pop.aggregate`, and the writer. It also requires matching daily poll counts and
capture-contract metadata proving the required fields, an unbounded date range,
the selected country, and no partial province filter. Nominal GDP values current physical output and
consumption at current prices. Real GDP values the same quantities at the first
snapshot's prices, or at `--base-date RAW`. The CSV includes factory, RGO,
precious-metal, and artisan components, resident population, nominal and real
GDP, and both per-capita values. `--gold-to-cash-rate` is mandatory only when
the selected scope contains precious-metal output. The command rejects gaps,
drops, invalid family records, missing prices or identities, incomplete factory
boundaries, producer entrants without opening factory flow evidence, non-daily
intervals, and missing active-mod gold valuation.

A minimal unbounded PRU capture uses these rules; change all four `PRU` filters
together for another country:

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

Do not set `start_date_raw`, `end_date_raw`, or `province_ids` on these rules.
After at least three captured dates, export with
`smedley_trace country-gdp run.jsonl gdp.csv --country PRU`; add
`--gold-to-cash-rate RATE` when the selected scope contains precious-metal
output.

Live three-day PRU run `001c25f0-f5a4-429d-89b7-fafd126a3487` emitted capture
contracts and 2,859 total records with zero gaps, drops, writer failure, or family invalid records and
exported nominal GDP 1,422.356650989, real GDP 1,415.854841757, and population
3,564,450. Live FRA run `64dc24a5-e811-4ed5-b3e9-90fa1943fe79` emitted capture contracts and healthy
records for 87 RGOs, 164 active artisans, population, market prices, and factory
flows per date. With vanilla rate 0.5 it exported nominal GDP 3,997.530945404,
real GDP 3,985.369406416, population 8,784,157, and precious-metal value added
4.024536133.

Four-day flows-only run `b7c6d433-2404-49f7-a12f-738810579949` selected only
factory `production`, factory `flows`, and market `price`. It emitted 24
production records, 24 complete flow summaries, and 69 flow records with zero
gaps, drops, invalid records, or writer failure. After the initial hook warm-up,
the two exported country intervals produced value added of 17.719507277 and
19.813987492 with `verified-runtime` quality.

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
exercise timeout retry.

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
