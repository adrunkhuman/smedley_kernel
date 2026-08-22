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
- modifier mutations and other mutation-shaped keys.

Counts use the terminology `lead` throughout the report. A large file, common
key, broad scope, or recurring edge is only a static lead. Establishing a
performance problem requires repeated runtime measurements from the same save,
mod revision, executable, plugin set, and run interval.

The parser recognizes top-level `country_event`, `province_event`, `event`, and
`news_event` blocks with direct scalar IDs. MTTH records are direct children of
those event blocks. Workload categories use key-name heuristics rather than a
complete Victoria II command catalog. The parser does not implement trigger
semantics, scope resolution, event eligibility, random selection, or mod
load-order merging. Run it on the intended effective source directories and
interpret the result alongside the mod's policy.

Run the focused tool tests from the repository root:

```powershell
python tools/tests/clausewitz_workload_inventory_test.py
```

The complete CTest suite registers the same test module as
`smedley_clausewitz_workload_inventory_tool_tests`.
