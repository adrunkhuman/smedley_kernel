# Telemetry configuration

[Overview](README.md) · [Events](events.md) · [Trace tool](trace.md)

## Profile fields

Every present field is type-checked on load/save. Launch preflight repeats
range, category, output and selected-plugin checks.

| Field | Type | Default | Contract |
| --- | --- | --- | --- |
| `telemetry_enabled` | boolean | `false` | Requires selected plugin ID `telemetry` when injecting. |
| `telemetry_output` | optional string | per-run path | Non-directory `.jsonl` file; see output safety below. |
| `telemetry_categories` | string array | `"lifecycle", "state"` | Non-empty, unique; only `lifecycle` and/or `state`. |
| `telemetry_country_tags` | string array | empty | Unique normalized three-character uppercase ASCII alphanumeric tags, including dynamic tags such as `D01`. |
| `telemetry_start_date_raw` | optional integer | none | Inclusive legacy raw lower bound; GUI uses `DD-MM-YYYY`. |
| `telemetry_end_date_raw` | optional integer | none | Inclusive legacy raw upper bound; cannot precede start. |
| `telemetry_sample_days` | integer | `1` | Legacy interval, 1–365 days; explicit rules use their own cadence. |
| `telemetry_queue_capacity` | integer | `1024` | 64–32768 fixed record slots. |
| `telemetry_overwrite` | boolean | `false` | Required to replace an existing output. |
| `telemetry_gold_to_cash_rate` | optional number | none | Required by `country.economy`; greater than 0, at most 1000. Use the active mod's `GOLD_TO_CASH_RATE` (vanilla `0.5`, GFM `1.0`). |

Without explicit rules, `state` enables bounded world economic snapshots and
legacy sampling under the inclusive top-level bounds and `telemetry_sample_days`.
Top-level country filters apply to `country.daily`, not global world records.
A complete world POP walk costs materially more than treasury sampling.

## Capture rules

`telemetry_captures` accepts at most 32 rules, one per family, and requires the
`state` category. Explicit rules replace legacy state sampling; lifecycle
category selection remains global. Each rule accepts `family`, `cadence`,
`fields`, `country_tags`, `province_ids`, and optional inclusive raw
`start_date_raw`/`end_date_raw`. The GUI edits `telemetry_capture_v1` with human
`DD-MM-YYYY` bounds. Empty `fields` selects every family field.

```toml
[[telemetry_captures]]
family = "world.economy"
cadence = "monthly"
fields = ["health", "holdings"]

[[telemetry_captures]]
family = "province.daily"
cadence = "daily"
fields = ["owner_tag_candidate", "life_rating_candidate", "infrastructure_candidate_raw"]
province_ids = [1, 300, 549]

[[telemetry_captures]]
family = "pop.cashflow"
cadence = "daily"
fields = ["summary", "account", "components"]
country_tags = ["PRU"]
```

### Families and filters

`C` means country filters; `P` means province filters. Empty supported filters
select every entity. Unsupported filter kinds are rejected by launcher and
plugin. Individual `pop.cashflow` requires at least one country or province.
Country families observe the country supplied by `DailyUpdateEvent`, not a
separate country traversal. Global families have no country entity.

| Family | Selectable fields/groups | Filters |
| --- | --- | --- |
| `world.daily` | `country_slot_count`, `ai_scheduler_entry_count`, `human_control_present` | — |
| `world.economy` | `health`, `capacity`, `holdings`, `credit` | — |
| `world.military` | `ongoing_war_count_candidate` | — |
| `world.market` | `price`, `supply`, `demand`, `sales` | — |
| `country.daily` | `treasury_raw`, `treasury` | C |
| `country.metrics` | `power`, `politics` | C |
| `country.economy` | `totals`, `components`, `per_capita` | C |
| `country.military` | `unit_count_candidate`, `mobilized_candidate`, `scheduled_mobilization_count_candidate`, `leadership_candidate_raw`, `military_ranking_candidate` | C |
| `country.diplomacy` | `status`, `relations` | C |
| `state.factory` | `identity`, `employment`, `production`, `finance`, `inputs`, `flows`, `sales` | C |
| `province.daily` | `owner_tag_candidate`, `controller_tag_candidate`, `colonial_level_candidate`, `life_rating_candidate`, `infrastructure_candidate_raw` | C, P |
| `province.production` | `building_slot_count_candidate`, `construction_count_candidate` | P |
| `province.rgo` | `identity`, `employment`, `production`, `finance`, `modifiers`, `sales` | C, P |
| `pop.artisan` | `identity`, `production`, `inputs`, `finance`, `flows`, `sales` | C, P |
| `pop.economy` | `money_raw`, `savings_raw`, `interest_cash_flow_raw`, `total_cash_flow_raw` | C, P |
| `pop.demographics` | `size_candidate`, `employed_candidate`, `consciousness_candidate_raw`, `militancy_candidate_raw`, `literacy_candidate_raw` | C, P |
| `pop.identity` | `pop_type_tag_candidate`, `culture_tag_candidate`, `religion_tag_candidate` | C, P |
| `pop.needs` | `life_satisfaction_candidate_raw`, `everyday_satisfaction_candidate_raw`, `luxury_satisfaction_candidate_raw` | C, P |
| `pop.aggregate` | `pop_count`, `size_candidate`, `employed_candidate`, `money_raw`, `savings_raw` | C, P |
| `pop.lifecycle` | `summary`, `appeared`, `disappeared`, `scope_changed` | C, P |
| `pop.cashflow` | `summary`, `account`, `components` | C, P; required |
| `pop.cashflow.aggregate` | `summary`, `account`, `components` | C |

The shared compiled [registry](../../telemetry_registry/telemetry_registry.cpp)
is authoritative for selections, filters, event schemas and admission. Disabled
families do not install hooks or traverse groups unless another enabled family
needs that capability. Lifecycle-only capture does not open/read observation
state merely to report progress.

### Cadence and dates

| Cadence | Trigger |
| --- | --- |
| `daily` | Every eligible observed day. |
| `weekly` | First eligible date, then every seven game days. |
| `monthly` / `yearly` | First observed date in each game calendar period. |

The calendar uses 24 raw units/day, fixed 365-day years, no leap day and epoch
`-5000.1.1`; `1836.1.2` is raw `59883384`. Date regression resets each rule
independently. Producer `sales`, both POP cash-flow families and `pop.lifecycle`
require daily cadence. Sales need previous closing inventory; lifecycle needs
consecutive complete stocks and restarts warm-up on gaps/regression.

`country.economy` is an exception to point sampling: cadence selects an
**accounting interval**. It reads producers, population and market daily, then
emits the completed daily/weekly/monthly/yearly period. Yearly output sums
observed daily value added, not one January production rate. See
[interval completeness](events.md#country-economy).

### Admission and cost

| Registry priority | Admission |
| --- | --- |
| `ReliableTerminal` | Always reliable in-memory publication. |
| `Important` | Reliable only when combined country/province allowlists contain 1–16 entries; otherwise best-effort. |
| `BestEffort` | Never promoted by filters. |

Lifecycle and terminal health records use the reliable path. Reliable still
means bounded: full/stopped queues can drop records. Ordinary detail publication
reserves 16 slots, or one eighth of smaller queues, for reliable records. Family
summaries report records, attempted formatted bytes and collection time;
nonzero dropped bytes make strict family health unhealthy. See
[delivery counters](events.md#capture-and-delivery-health).

## CLI

| Option | Effect |
| --- | --- |
| `--telemetry` / `--no-telemetry` | Enable/disable. |
| `--telemetry-output PATH` | Choose output. |
| `--telemetry-category lifecycle\|state` | Repeat to select categories. |
| `--telemetry-country TAG` | Repeat legacy country filters. |
| `--telemetry-start-date-raw N`, `--telemetry-end-date-raw N` | Inclusive legacy bounds. |
| `--telemetry-sample-days N` | Legacy interval. |
| `--telemetry-queue-capacity N` | Record slots. |
| `--telemetry-overwrite` | Allow replacement. |
| `--telemetry-capture "family\|cadence\|fields\|countries\|provinces\|start\|end"` | Repeat to replace profile capture rules. Commas separate fields/entity IDs; empty components allowed. |

Set `telemetry_gold_to_cash_rate` in the profile for `country.economy`; the
[trace exporter](trace.md#country-gdp) has its own `--gold-to-cash-rate` option.

## Output safety

Outputs must end in `.jsonl` (case-insensitive). Existing files require explicit
overwrite. Directories, reparse points and collisions with the source save,
executable, kernel, selected manifests/modules or mod descriptors are rejected.
The plugin repeats validation immediately before opening with `CREATE_NEW`, or
`CREATE_ALWAYS` only with explicit overwrite.

Injected launches derive `%LOCALAPPDATA%\Smedley\traces\<run-id>.jsonl` when
needed. After `BuildLaunchPlan`, the launcher creates the run ID, appends safely
quoted `-smedley-run-id=<run-id>` and any derived output argument before
`CreateProcessW`, and stores the exact child command line and trace link in the
run record. Dry runs have no run ID; safe-mode launches have no telemetry
arguments or trace link.
