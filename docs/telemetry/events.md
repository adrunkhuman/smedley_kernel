# Telemetry events

[Overview](../telemetry.md) · [Configuration](configuration.md) · [Trace tool](trace.md)

## JSONL envelope v1

Each physical line is one UTF-8 JSON object. Controls, quotes, backslashes and
invalid UTF-8 bytes are escaped; valid Unicode UTF-8 is preserved. Entity and
payload objects contain typed JSON, never serialized pointers.

| Field | Type | Contract |
| --- | --- | --- |
| `schema` | string | `smedley.telemetry` |
| `schema_version` | integer | `1` |
| `run_id` | string | Launcher-created run identifier. |
| `sequence` | integer | Increasing per emitted record; queue drops can leave gaps. |
| `wall_time_utc` | string | UTC ISO-8601 with milliseconds. |
| `monotonic_us` | integer | Process monotonic microseconds. |
| `game_date_raw` | integer or `null` | Mapped raw date when available. |
| `event_type` | string | Stable event name. |
| `category` | string | `lifecycle` or `state`. |
| `mapping_id` | string | `v2game-3.04` for the supported executable. |
| `quality` | enum | `verified-runtime`, `verified-current`, `verified-static-callsites`, `provisional`, `historical-unverified`, `historical-skeleton`. Built-ins currently use the first, second and fourth. |
| `entities` | object | Event-specific identifiers. |
| `payload` | object | Event-specific typed fields. |

Absent means the family does not define the field; `null` means defined but
unavailable; numeric `0` means an observed zero. Preserve all three states.
Daily state/date evidence remains provisional; a non-null date does not upgrade
quality. Missing game state suppresses country records and increments
`skipped_unsampleable`.

```json
{"schema":"smedley.telemetry","schema_version":1,"run_id":"9f0...","sequence":8,"wall_time_utc":"2026-07-31T12:34:56.789Z","monotonic_us":912345,"game_date_raw":12345,"event_type":"country.daily","category":"state","mapping_id":"v2game-3.04","quality":"provisional","entities":{"country_tag":"ENG"},"payload":{"treasury_raw":32768,"treasury":1.000000}}
```

`smedley_telemetry_registry`, the [compiled registry](../../telemetry_registry/telemetry_registry.cpp), supplies
the complete state event/entity/payload catalog in emission order. Its schema
strings use comma-separated field names, `-` for an empty object and trailing
`?` for optional fields. The [configuration catalog](configuration.md#families-and-filters)
lists selectable groups, not a duplicate event schema. Registry quality and
mapping metadata remain descriptive: a byte match or schema membership does
not prove gameplay semantics. Runtime provenance is in
[mapping evidence](../../mappings/evidence/telemetry.md).

## Capture and delivery health

`session.started`, `telemetry.progress` and best-effort `telemetry.summary`
are lifecycle records with `verified-current` quality. Progress/summary report:

| Field | Meaning |
| --- | --- |
| `accepted`, `written` | Payload records accepted/written; excludes `telemetry.summary` itself. |
| `dropped` | Records rejected by bounded delivery. |
| `high_water` | Peak queue occupancy. |
| `write_failed` | Writer failure. |
| `callback_enqueue_format_us_total`, `callback_enqueue_format_us_mean` | Total/mean callback enqueue and formatting time, microseconds. |
| `callback_count` | Callback attempts. |
| `skipped_unsampleable` | Samples skipped for unavailable mapped state. |

Coordinated drain attempts one `telemetry.family.summary` per configured family:
`polls_due`, `collection_attempts`, `accepted`, `filtered`, `dropped`, `invalid`,
`collection_us`, `accepted_bytes`, `dropped_bytes`. These are producer results,
not subsequent worker writes. Rejected finalized records contribute attempted
bytes; filtered, invalid and unavailable records contribute zero bytes.

Reliable startup `telemetry.capture.rule`, `.field` and `.country` records
identify cadence, selected fields, date bounds and entity filters. Strict
exports use them to distinguish genuine zeroes from incomplete configured
scope. The v1 rule payload is exactly `cadence`, `all_fields`,
`country_filter_count`, `province_filter_count`, `bounded_dates`,
`projected_entity_count`, `operational_admission`. Projection is `-1` for empty,
unbounded allowlists; its sign establishes boundedness. Admission is
`best-effort` or `reliable`. Do not expect `cost_class`, `admission_priority` or
`projection_bounded`: planning metadata is internal to bundled C++ registry
consumers, and the publication ABI permits only eight combined entity/payload
fields.

## Campaign lifecycle and benchmarks

These events require `campaign_runner` and an active sink. They obey the
`lifecycle` category only, never state country/date filters. They make no
political-event, AI-reasoning or reload claim.

| Event | Typed data | Quality / observation boundary |
| --- | --- | --- |
| `session.started` | `plugin` | `verified-current`; writer started. |
| `campaign.save_selection_requested` | `source="campaign_runner"` | `verified-runtime`; filename and selection flags written; no save name/path emitted. |
| `campaign.save_load_completed` | none | `verified-runtime`; controller completion before play dispatch, not campaign entry. |
| `campaign.entered` | observer/speed/pause requests | `verified-runtime`; first successful `CInGameIdler` RTTI readback. |
| `observer.configured` | `viewing_country`; full AI/map booleans | `verified-runtime`; no human control, AI scheduling, FOW disabled and valid view; pending switches delay emission. |
| `speed.configured` | previous/current/requested speed | `verified-runtime`; handler readback, including no-op. |
| `pause.configured` | previous/current/requested pause | `verified-runtime`; final readback once, not observer setup pauses. |
| `date.regressed` | previous/current/delta raw date | `provisional`; lower daily date observed, not proof of reload. |
| `benchmark.started` | start/target raw date, requested days, timeout seconds | `provisional`; fully configured interval begins; record date is start raw date. |
| `benchmark.resources` | process CPU microseconds; available start/end working set/private committed bytes; process-lifetime peak working set | `verified-current`; optional sample immediately before terminal event. CPU is benchmark user+kernel delta; memory is sampled, not interval peak. |
| `benchmark.completed` | start/target/actual raw date, game days, elapsed microseconds, zero overshoot, paused | `provisional`; exact-target pause readback. |
| `benchmark.failed` | start/target raw date, elapsed microseconds, reason; actual date/paused when readable | `provisional`; reasons below. |

Failure reasons: `timeout`, `date_overshoot`, `idler_unavailable`,
`invalid_pause_state`, `pause_failed`, `observer_invariant_failed`,
`date_regressed`, `unexpected_pause`, `game_over`, `timer_unavailable`, or
standalone `invalid_target`. `game_over` takes precedence over exact-target
completion and pause recovery.

Telemetry outcomes never change benchmark outcome. Missing resource records in
older traces remain valid/unavailable. Queue drops can create gaps; unavailable
and filtered ingress are quiet. Lifecycle transitions retry only while the sink
is unavailable; accepted, filtered, dropped and invalid results are terminal.
One-shot actions can remain unobserved if telemetry activates afterward.

## World and country snapshots

| Family | Meaning and limits |
| --- | --- |
| `world.daily` | Once per selected date before country filtering: country slots, AI scheduler entries, human-control presence. Slots include non-playable entries; scheduler count is not an AI-country count. |
| `country.daily` | Country tag plus `treasury_raw`, `treasury = treasury_raw / 32768.0` (48.15 fixed point). No shadow treasury, pointers, AI intentions, scores or decision reasoning. |
| `country.metrics.power` | Prestige/infamy raw candidates and overall/military/industrial/prestige rank candidates. |
| `country.metrics.politics` | Plurality, war exhaustion, diplomatic points, research points, leadership raw candidates. Metric scalars use 1/1000 except leadership, which uses /32768. |
| `country.military`, `world.military` | Bounded unit-list/mobilization/schedule/leadership/rank candidates; global ongoing-war count has no entity. No unit, regiment, war, side, battle, casualty or movement identities. |
| `country.diplomacy.status`, `.relations` | Substate/vassal flags, overlord/sphere-leader candidates; sphereling/vassal/ally/guarantee/neighbor counts. Influence and opaque diplomatic actions/statuses unavailable. |
| `province.daily` | Province ID and provisional owner/controller/colonial/life-rating/infrastructure candidates. Infrastructure raw is not a supported building-level conversion. |
| `province.production` | Building-definition slots and construction count; slots are not active buildings or factories. Factories are state-level. |

World economic scans attempt selected records under these conditions:

| Event | Payload / condition |
| --- | --- |
| `world.economy.health` | Structural completeness, snapshot/collection/credit flags and country/state/province/POP traversal counts; attempted after every scan. |
| `world.economy.capacity` | Fixed limits, basis-point utilization, collection microseconds; structurally complete zero-flag scan only. Limits: 512 countries, 4096 provinces, 100000 POPs. |
| `world.economy.holdings` | Treasury, POP money/savings, bank interest accumulator, positive-balance and negative-treasury counts; complete scan only, provisional. |
| `world.economy.credit` | Creditor/paid counts and creditor/state candidate aggregates; structural and credit flags clear, provisional. |

Credit flags do not invalidate independently complete holdings/capacity. POP
money and savings are separate storage categories; savings, creditor claims and
state aggregates can double-count liquid balances. The bank interest accumulator
is a temporary interest destination, **not bank cash**. No `world_money_supply`
is emitted.

## Producers

### Factories

`state.factory` reads the owning country's bounded state list. Identity is
country tag + serialized persistent state ID + factory-definition key. Identity
records also carry shared `state_region_key`, anchor-province candidate, level,
subsidy and closed state. Split-owner state instances share a region key, not a
persistent state ID. Verified merge attribution covers non-duplicates and
recipient-wins equal-level duplicates only; different levels and more than eight
distinct types remain untested.

| Group | Contract |
| --- | --- |
| `employment` | Employees plus craftsmen/clerk assignment totals. |
| `production` | Current `output_raw`, output-good key/ordinal, `base_output_raw`; /32768 quantity scale. Cached output alone does not prove same-day settlement. |
| `inputs` | Per good in stock or request pool: `stockpile_raw` holding and `requested_raw` recipe demand after stock subtraction/zero clamp. Both /32768; requested is not fulfilled purchase or consumption. |
| `finance` | Nonnegative `budget_raw`, `market_spending_expense_raw`, `sales_income_raw`, `paychecks_expense_raw`, `investment_income_raw`; currency /32768000. UI supplies expense minus signs. |
| `flows` | `state.factory.input.flow.summary` marks post-consumption, pre-purchase, primary/secondary delivery boundaries and settlement count; `.flow` gives their quantities per nonempty good. Also emits `state.factory.input.consumption.summary` and nonzero `state.factory.input.consumption` details; omitted goods do not imply zero consumption. Ordinary `inputs` installs no flow hooks. |

Last-day UI balance is sales + investment − market spending − paychecks. Main
factory-list profit is a different computed display and remains unavailable.
Paycheck cadence is unclaimed; `FACTORY_PAYCHECKS_LEFTOVER_FACTOR` is 0.25 in vanilla, 0.3 in GFM.
Upgrade progress/material acquisition are unavailable; policy, ownership,
funds, level and existing projects prevent inferring upgrade availability.

### RGOs and artisans

| Family/group | Contract |
| --- | --- |
| `province.rgo.identity`, `.employment` | Production type/output key and ordinal; capacity and assigned workers. Identity: country/province. |
| `province.rgo.production` | Recipe output per size, base size, efficiency, throughput, gross output. Multiply efficiency × throughput × recipe output × size, shifting 15 bits after each step. `base_size_raw_candidate` remains the v1 alias of verified `base_size_raw`. |
| `province.rgo.modifiers`, `.finance` | Owner POP population, state RGO capacity, ratio `owner_output_modifier_raw`; income. Quantities/modifiers /32768, income /32768000. |
| `pop.artisan.identity`, `.production` | Country/province/`pop_id`, recipe/output identity; base recipe output × actual `current_producing_raw`, shifting 15 bits. |
| `pop.artisan.inputs` | Current stock and recipe coefficient `need_raw` per good. Consumption = coefficient × current production factor, shifted 15 bits, not stock delta across recipe changes. |
| `pop.artisan.flows` | Post-consumption/pre-purchase/primary/secondary delivery quantities; validates recipe accounting. Recipe switches legitimately replace input-good sets. |
| `pop.artisan.finance` | `last_spending_raw`, `needs_cost_raw`, `production_income_raw`, `percent_afforded_raw`, `throttle_raw` for an active snapshot. |
| Any artisan group | `pop.artisan.inactive` means readable but no active production; `pop.artisan.invalid` reports stable reason/offending raw value and increments family invalid health. Neither invents production/finance. |

Precious-metal RGO income is not gold cash creation. GDP values quantity using
the active mod's explicit `GOLD_TO_CASH_RATE`, not market price or RGO income.

### Sales accounts

Daily `sales` emits `{state.factory,province.rgo,pop.artisan}.sales.summary` and,
when complete, a paired `.quantity`/`.revenue` record.

| Record | Fields / contract |
| --- | --- |
| Factory summary | `settlement_seen`, `settlement_count`, `complete`; missing/duplicate/invalid boundaries remain explicit. |
| RGO/artisan summary | `settlement_seen`, `opening_inventory_seen`, `complete`; first day, gaps, ownership/output-good changes (and artisan recipe switches) break continuity. |
| Quantity | `output_good_ordinal`, `opening_inventory_raw`, `produced_raw`, `sold_raw`, `closing_inventory_raw`; /32768 and `sold = opening + produced - closing`. |
| Revenue | `proceeds_raw`; /32768000. RGO/artisan also retain `percent_sold_domestic_raw`, `percent_sold_export_raw`. |

Factory identity is country/state/type; RGO country/province; artisan
country/province/POP ID. Warm-up is incomplete, not zero sales. Domestic/export
values do not independently allocate total sold quantity. Export clearing can
exceed 32768 and is not a bounded percentage. Historical annual false-bound
failures and subsequent healthy validation are retained in mapping evidence;
producer record quality remains provisional, not upgraded by that repair.

### Market

`world.market` emits selected groups per active zero-based goods ordinal:
`price` current/previous price, `supply` current/previous supply/world stock,
`demand` nominal/real demand, `sales` actual sold/world-market subset. All use
/32768. Ordinals follow mod load order; a portable goods-name catalog remains
unavailable. Global totals do not attribute sales to producers or countries.

## Country economy

`country.economy` is an in-game daily accumulation account, separate from the
strict offline [country-gdp export](trace.md#country-gdp).

| Event | Payload |
| --- | --- |
| `country.economy.interval` | `period_start_raw`, `period_end_raw`, `observation_days`, `expected_days`, `invalid_days`, `complete`, `population_average` |
| `country.economy.total` | `nominal_gdp`, `real_gdp`, `base_date_raw`, `gold_to_cash_rate` |
| `country.economy.component` | `component` identity; `nominal_value_added`, `real_value_added` for factory/RGO/artisan |
| `country.economy.per_capita` | `nominal_gdp_per_capita`, `real_gdp_per_capita` |
| `country.economy.quality` | `unsettled_output_candidates` |

First factory observation has no opening consumption stock: its country/day
remains present with `complete=false`. Gaps, missing country/producer boundaries,
unreconciled boundaries or hook drops make periods incomplete. After warm-up,
no settlement means no verified factory production that day; cached positive
output is counted in `unsettled_output_candidates`, not valued as production.
Use day counts and `complete`, not partial GDP as verified GDP. Consecutive
settlement boundaries establish factory completeness. Real GDP fixes prices at
the first successful economy day; date regression clears periods and base
prices. Storage is bounded by 512 countries, 64 goods and factory boundary arrays.

## POP snapshots, identity and lifecycle

POP rules due on the same date share a bounded snapshot. Individual economy
and demographic records use province/type candidates plus engine `pop_id`;
country filtering uses province owner without duplicating country in the
eight-field record. Money/savings/interest/total cash-flow fields have narrow
runtime evidence; size, employment, consciousness, militancy, literacy,
province/type IDs remain candidates. Rate fields use 48.15 (/32768).

`pop_id` identifies one current in-process object, not a permanent historical
person/cohort. Ordinary consecutive stability is observed, but location changes
can replace IDs; promotion/demotion/split/merge/deletion/reload/reuse semantics
remain unestablished. `pop.identity` adds normalized type/culture/religion keys;
even adding artisan recipe does not make that tuple a universal individual key.
`pop.aggregate` sums count/size/employment/money/savings by province/type across
culture and religion.

`pop.needs` exposes life/everyday/luxury satisfaction candidates in [0,32768],
not costs, goods demand, fulfilled quantities or spending. Broader POP/UI
correlation remains needed. Its separate bounded reader can reject a candidate
without invalidating the established shared snapshot.

`pop.lifecycle` compares consecutive complete world stocks by `pop_id`.
`pop.lifecycle.observed_appeared`/`pop.lifecycle.observed_disappeared` mean ID
entered/left stock; `pop.lifecycle.scope_changed` means country/province/type
candidate changed for a surviving ID.
The daily summary reconciles opening/closing counts and diff classes globally;
filters affect detail only. First complete stock emits warm-up with
`opening_seen=false`, `complete=false`, no opening stock. Gaps/regression restart
warm-up. These events do not classify demographic causes.

## POP cash flow

Daily hooks observe `CPop::GiveMoney`, retaining posted amount and actual money
delta after clamping. Indices: `0 needs`, `1 welfare`, `2 salary`, `3 expenses`,
`4 events`, `5 projects`, `6 bank`, `7 interest`. Taxes/tariffs lack independent
attribution and must not be inferred from needs/expenses.

| Event group | Identity / payload |
| --- | --- |
| `pop.cashflow.{summary,account,component}` | Country/province/type candidate/`pop_id`; summary: `opening_money_seen`, `capture_complete`, `reconciled`, `call_count`. |
| `pop.cashflow.aggregate.{summary,account,component}` | Country/type candidate. |
| `pop.cashflow.country.{summary,account,component}` | Country only; CSV uses type `-1`, JSONL does not invent it. |
| Aggregate/country summaries | `opening_pop_count`, `closing_pop_count`, `opening_money_seen`, `reconciled`. |
| Every account | `opening_money_raw`, `closing_money_raw`, `money_delta_raw`, `residual_raw`. |
| Every component | `posted_raw`, `money_delta_raw`; individual components add `cash_flow_index`, `component` identity. |

`closing_money_raw - opening_money_raw = money_delta_raw + residual_raw`.
First date is warm-up with no
account. Direct redistribution can bypass the hook, so individual/type residuals
are evidence, not invalid records. Equal/opposite type residuals can cancel;
country reconciliation requires zero residual and complete bounded capture.

## Reader and hook bounds

Malformed sizes/pointers/links, unreadable spans, misalignment, exceeded bounds
or non-normalized tags suppress affected reads and increment family `invalid`.
Unselected factory groups are not traversed/validated.

| Boundary | Limit |
| --- | --- |
| Province buildings / constructions | 64 / 4096 |
| Country units / scheduled mobilizations | 100000 each |
| Country relation vectors / ongoing wars | 512 each / 4096 |
| Factory states / factories per state / factories per country | 512 / 64 / 4096 |
| Employment assignments per factory / input records per country | 1024 / 16384 |
| Direct factory flows per day | 512 factories, 2048 hook records; overflow invalidates strict export. |
| Artisan purchase flows | At most 16 country tags, 8192 hook records/date. |

## Interest-fix diagnostics

**These attempted records are not currently written by the bundled sink:** it
rejects their `metadata-degraded` quality with `SMEDLEY_TELEMETRY_INVALID`.
Use [CSV/log diagnostics](../interest-fix.md#diagnose-a-run) instead.

The worker attempts publication independently of `interest_fix_debug`. Both
records use the `state` category, `country_index` entity, and unavailable game
date.

| Event | Payload / contract |
| --- | --- |
| `interest.fix.health` | `status` for each queued initialization/payout/failure result. |
| `interest.fix.value` | `state_id`, `payout_raw`, `mutation_result`; only for `paid` results. |

Reliable publication prevents contention-only loss; unavailable, filtered, full,
or invalid telemetry never changes payout. Older sinks without
`SmedleyTelemetryEmitReliableV1` receive best-effort publication. CSV diagnostics
are separately opt-in and truncate `<GAME_DIR>/interest_bug_fix.csv`. Current CSV
and events do not include the older callback timing or telemetry-result fields.
See [interest-fix diagnostics](../interest-fix.md#diagnose-a-run).
