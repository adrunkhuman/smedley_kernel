# Clausewitz workload inventory

`tools/clausewitz_workload_inventory.py` produces a deterministic static
inventory of Victoria II Clausewitz scripts. It identifies source locations
that may deserve runtime measurement; it does not profile the game or estimate
execution cost.

Run it against explicit script directories:

```powershell
python tools/clausewitz_workload_inventory.py `
  C:\path\to\mod\events `
  C:\path\to\mod\decisions `
  --root C:\path\to\mod `
  --output workload.json
```

Inputs may be files or directories. Directories are searched recursively for
`.txt` files; other extensions are ignored. `--output` writes UTF-8 JSON and
requires an existing parent directory. The tool rejects an output path that is
also one of the discovered input scripts.

The JSON report contains:

- event definitions and source lines;
- explicit event scheduling edges and delay values;
- `mean_time_to_happen` blocks;
- iterator and random-selector key counts;
- flag reads and writes;
- modifier mutations and other mutation-shaped keys;
- source-spanned province-modifier workflows with event context, block ancestry,
  cadence, iterator and random scopes, flags, variables, and semantic risk flags.

Schema version 2 adds `province_modifier_workflows`. Each record describes one
event that reads or writes a province modifier. It preserves the event and
modifier-command source ranges, exact containing block path, modifier duration
when statically available, compact event-level dependency sets, and explicit
risks for random selection, finite duration, state-scoped writes, and ownership
mutation. MTTH events are marked as stochastic polling, while statically
resolved incoming event schedules and explicit scheduling cycles are retained
separately.

`province_modifier_scope_candidates` narrows those records to outer iterator
blocks that contain province-modifier writes. Its dependency and risk fields are
local to the iterator block, including predicate keys below `limit`, avoiding
unrelated event logic when a large cleanup event contains many independent
tasks.

| Field | Meaning |
| --- | --- |
| `event_id`, `event_kind` | Enclosing top-level event definition |
| `event_source` or `source` | Inclusive `start_line` and `end_line` in the reported path |
| `modifier_reads`, `modifier_writes` | Source-spanned commands, block ancestry, static target, and optional duration |
| `incoming_schedules` | Statically resolved event commands targeting the event; omitted when none are found |
| `recurrence` | `engine_polled_mtth` and/or `explicit_schedule_cycle` when detected |
| dependency arrays | Sorted event- or iterator-local flags, variables, iterators, random selectors, and ownership mutations |
| `predicate_keys` | Sorted command keys below the iterator's `limit` blocks |
| `risk_flags` | Any of `finite_modifier_duration`, `mtth_polling`, `ownership_mutation`, `random_selection`, or `state_scope_write` |

Workflow records may also contain `mtth_cadence`; it is omitted when the event
has no direct MTTH block. Scope-candidate records represent one outer entity
iterator containing a province-modifier write. `random_list` is a weighted
choice, not an iterator scope.

Counts use the terminology `lead` throughout the report. A large file, common
key, broad scope, or recurring edge is only a static lead. Establishing a
performance problem requires repeated runtime measurements from the same save,
mod revision, executable, plugin set, and run interval.

The parser recognizes top-level `country_event`, `province_event`, `event`, and
`news_event` blocks with direct scalar IDs. MTTH records are direct children of
those event blocks. Workload categories use key-name heuristics rather than a
complete Victoria II command catalog. The parser does not implement trigger
semantics, scope resolution, event eligibility, random selection, or mod
load-order merging. Workflow records are source-semantic candidates, not proof
that an event is eligible, a command is valid in its inferred scope, or a
modifier operation executes. Run the tool on the intended effective source
directories and interpret the result alongside the mod's policy.

Run the focused tool tests from the repository root:

```powershell
python tools/tests/clausewitz_workload_inventory_test.py
```

The complete CTest suite registers the same test module as
`smedley_clausewitz_workload_inventory_tool_tests`.
