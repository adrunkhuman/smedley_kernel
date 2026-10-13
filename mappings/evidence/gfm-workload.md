# GFM v3.0 workload discovery

GFM is a requirements/measurement workload for
[issue #9](https://github.com/adrunkhuman/smedley_kernel/issues/9), not a kernel
compatibility contract. Static inventory identifies reusable capability needs;
it does not establish cost. The recorded baseline measures the whole campaign.
No reviewed probe supports generic script-site attribution; exploratory probes
and fixtures were removed, with no production attribution API retained.

## Immutable inputs

| Input | Identity |
| --- | --- |
| GFM repository / release | `Historical-Expansion-Mod/Greater-Flavor-Mod`, `v3.0` |
| Commit | `ee1486e251d3bb4dc5c355f5c2ec34abaea1078d` |
| Executable SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| Local save | `smedley-gfm-v3.v2`, SHA-256 `3c70553984e4d94773483076ee4d83ac351095042f66a9920a3ee4770f7763f4` |
| Save date / player / header | `1836.1.1`, `PRU`; `government=9`, `unit=2800`, `state=2207` distinguish it from a same-named vanilla save |
| Installation acceptance | Detached checkout matched installed GFM; only `mod/GFM.mod` selected, submods disabled; main-menu checksum `ZAEJ` |

The legal local save and generated reports/traces are not committed. Recorded
measurements are not independently auditable artifacts. Installation comparison:

```powershell
robocopy "<gfm-checkout>\GFM" "<game-dir>\mod\GFM" /MIR /L
```

Exit code 0: no files needing copy, extras, or failures. `/L` made this read-only.

## Static inventory

Generated with Python 3.14.7:

```powershell
python tools/clausewitz_workload_inventory.py `
  "<gfm-checkout>\GFM\events" `
  --root "<gfm-checkout>\GFM" `
  --output gfm-v3-events-inventory.json
```

| Lead | Count | Lead | Count |
| --- | ---: | --- | ---: |
| Event definitions | 7,416 | Explicit scheduling edges | 7,146 |
| MTTH blocks | 3,883 | Flag reads / writes | 29,228 / 39,155 |
| Modifier mutations | 4,998 | `random_owned` | 13,447 |
| `any_country` | 8,999 | `any_pop` | 5,975 |
| `random_pop` | 839 | `any_land_province` | 681 |

Counts omit runtime trigger outcomes, candidate populations, load-order
overrides, execution, and engine optimization. The parser independently found
this cleanup chain in `events/V2ME Cleanup (GFM).txt`:

| Event | Definition line | Recurring edge |
| --- | ---: | --- |
| `477877787` | 31 | self after one day, line 384 |
| `84393532` | 388 | scheduled by `477877790` after one day, line 19021 |
| `477877790` | 1110 | self after 30 days, line 19020 |
| `477877791` | 30938 | self after 365 days, line 55552; also fans out to numbered country events |

### Province-modifier reconciliation

Inventory found 996 province-modifier workflows and 736 modifier-writing
iterator candidates; 19 participate in explicit scheduling cycles. Thirteen
have no local random-selection, finite-duration, ownership-mutation, or MTTH
risk. That filter is not deletion authorization. Source review of all thirteen:

| Blocks in cleanup file | Contract / route |
| --- | --- |
| Monthly casino repair, 19016–19019 | Fixed-province compatibility repair; direct-province smoke candidate, too small to justify an API alone |
| Annual capital removal, 31346–31357 | Exact copy of monthly 1142–1153; source simplification, not an API driver |
| Slave-trade removal, 1127–1141 | Stale penalty after abolition; historical native slice verified iteration, owner policy, checked removal; [evidence](province-reconciliation.md) |
| Monthly capital removal, 1142–1153, annual addition, 31358–31369 | Capital-mobilisation supply-limit invariant; needs mapped capital transitions |
| Three local industry additions, 18399–18481 | Backfill after one-shot country events; province queries/checked addition candidate |
| Collectivisation removal, 18484–18493 | Stale bonus after government change; transition observation/removal candidate |
| Coffee addition, 18730–18752 | Ownership/setup backfill; ownership transitions/addition candidate |
| Two Indian-lands blocks, 18995–19015 | Gameplay normalisation; keep policy in downstream script |
| Annual `drang_nach_osten` addition, 31672–31702 | Acquisition backfill; reuse after generic province capability is proven |

Casino repair scans every land province for province `3251` producing precious
goods without permanent `casino`. `monte_carlo_casino` sets both in
`decisions/monacoflavor.txt:18-26`; no other active writer/remover exists.
Deleting repair abandons old/externally modified saves. A direct-province
operation could preserve it, but four lines do not justify that API alone.

Slave-trade cleanup was the first slice because its explicit invariant exercises
reusable queries/removal also needed by other backfills.
[Issue #91](https://github.com/adrunkhuman/smedley_kernel/issues/91) / PR #92
validated the historical replacement; its derivative/native consumer were
retired, and pristine GFM retained the block. Script reduction/correctness can
justify capability independently of measured cost; downstream replacements must
add behavior and remove equivalent pinned source atomically.

### Derived state flags

Recurring flags have distinct contracts, not one removable-cache family:

| Flags | Contract / route |
| --- | --- |
| `medicine_invented`, `germs_invented`, `gaslights_invented`, `electricity_invented`, `modern_central_bank_system_invented` | Technology/invention mirrors read by POP modifiers/country policy; country-fact/transition candidates, retain script until all scopes covered |
| `biologism_invented` | One country-scope technology reader; source simplification, too small for a native API alone |
| `monarchy_government`, `theocratic_government` | Broad reusable classifications; country-fact consumers, classifications remain GFM policy |
| `new_world_nation_fr` | Government-derived New World emigration policy; country-fact consumer, policy remains GFM |
| `can_use_monthly` | Deliberate decision pulse; retain unless decision scheduling is adopted end to end |
| Accepted-culture flags | Provenance for colonial culture grants; substantial country catalog/culture/relationship/checked-mutation case, requiring broader verified state |
| `revolution_n_counter_researched`, `colonial_cores_africa_active` | Irreversible global phase/scheduler latches; concise save-visible script, low priority |

## Candidate classification

| Candidate | Conclusion | Evidence gate |
| --- | --- | --- |
| Script-site attribution | Missing: selected-option executor too narrow; automatic-event leads are not effect execution | Controlled identity correlation at another boundary, measured overhead |
| Province modifiers | Reusable API family; thirteen workflows share iteration/owner/add-remove operations | Historical slave-trade validation is not blanket acceptance for new consumers |
| Derived flags | Split backlog: simple mirrors may stay in source; technology/colonial provenance need broader capabilities | Rank script reduction/reuse; preserve GFM policy and save-visible state |
| Ghost units/relationships | Engine-bug-workaround hypothesis: scripts repair residual entities/relationships | Reproduce without repair; verify native identity |
| Dismantlement variables/scope bridges | Engine-limitation hypothesis: numbered variables and repeated traversal emulate collections/transactions | Executed paths, behavior-preserving contract |
| `Naval Fix.txt` redistribution | GFM gameplay policy: treasury debit plus random POP transfers | Three-way economic matrix before overlap claims |
| Historical RGO/capital/government/annexation work | GFM gameplay policy | Not kernel replacement targets |

## Runtime baseline

Historical MSVC x86 Release build; pure pinned GFM/PRU save, normal player
control, speed 5, runner plus lifecycle-only telemetry, exact 30 days and native
exit. Source hash unchanged. Reproduction controls and profiles are in
[launcher](../../docs/launcher.md); the measurement invocation was:

```powershell
& "<game-dir>\smedley_cli.exe" `
  --game-dir "<game-dir>" --mod "mod\GFM.mod" `
  --plugin "plugins\campaign_runner.toml" --plugin "plugins\telemetry.toml" `
  --telemetry --telemetry-category lifecycle --save "<gfm-save>" `
  --speed 5 --run-days 30 --quit-after-run --run-timeout-seconds 600 --detach
```

| Binary | SHA-256 |
| --- | --- |
| `smedley_cli.exe` | `7d46547b3f7ecb4e93ca1d601538c6c01a9dfbf5a81587b54ead56e63afdc055` |
| `smedley_kernel.dll` | `7fb8f2bfe512d574c11b19c6020a61a694abd41b8914394be2da9a2fdf8e9a5f` |
| `campaign_runner.dll` | `21a55be5db0a69b716b0ba000a53aead3c5051f57296698466bdffe016609f3d` |
| `telemetry.dll` | `076d3c234700404a6e0a6c9bc9562eee977f5e489a6d462249b0365a9d5ab363` |

| Sample | Elapsed | Process CPU | Peak working set | Peak private bytes | Peak virtual bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 5.340159 s | 6.453125 s | 2,898,456,576 | 3,300,118,528 | 3,773,878,272 |
| 2 | 5.410516 s | 7.484375 s | 2,851,201,024 | 3,298,156,544 | 3,801,116,672 |

Mean 5.375338 s / 5.581 days/s; both traces had `benchmark.completed`,
`telemetry.summary`, zero drops. This measures workload, not individual scripts.
The measured telemetry build configured no state collector and used lazy
collector/POP scratch allocation; PE `SizeOfImage=0x50000`, not the earlier
approximately 16 MiB TLS template. Those are build-specific memory conditions.

A historical two-day run substituted state capture
`pop.aggregate|daily|pop_count|PRU|||`: benchmark completed, but no state
records, `callback_count=0`, no failure reason, source unchanged. The same path
worked on smaller vanilla state. This run did not verify GFM state capture and
is not evidence of the current collector's behavior; see
[current telemetry](../../docs/telemetry.md).

## Region scope iteration boundary

GFM's `any_land_province` is a custom region in `map/region.txt`, not a native
operator: 3,385 unique IDs, range 1–3,986. Uses compile to generic
`CRegionScopeEffect` / `CRegionScopeTrigger` objects.

| Boundary | `verified-static-callsites` contract |
| --- | --- |
| Effect RVA `0x00488ba0` | x86 `__thiscall`, two stack arguments, `ret 8`; `this+0x3c` region. Traverse `CRegion+0x8`, construct province scope, evaluate `this+0x20` filter, run `this+0x8` child effect chain for matches. Timing includes traversal/filter/effects. |
| Trigger RVA `0x004c5da0` | x86 `__thiscall`, one stack argument, `ret 4`; `this+0x1c` region. Nested triggers per province; first match returns true, exhaustion false. Cardinality is not the short-circuited visit count. |

Both entry signatures stop before ASLR-relocated exception-handler operands.
A bounded region-key-filtered probe found no target calls in normal 30-day GFM.
Controlled overlay exercised effects with 622 process-local node tokens in one
burst, without source-site correlation. No trigger calls, recursion, invalid
layouts, or drops. Visits, stable identity, overhead, and committed-tree build
provenance were not established; neither symbol is promoted to runtime-verified.

## Script application boundary

RVA `0x00507e00` is `verified-runtime` only as the selected-event-option action
executor, not generic event/trigger/effect attribution. Static x86 MSVC
`__thiscall` uses `ECX` action and synchronously calls option executor
`0x004a61f0`. Its eight-byte signature is relocation-free; the later exception
handler operand is not safe to match literally under ASLR.

| Action field | Meaning / controlled correlation |
| --- | --- |
| `+0x64` | Selected option: event `990001` option 0; `990002` option 1 |
| `+0x68` | Embedded event snapshot |
| `+0x70` / `+0x74` | Opaque source words `(5128522, 221)` for both |
| `+0x40` / `+0x44` | Resolver words `(39, 990001)` / `(39, 990002)`; second word is event ID for these country fixtures |

Two disposable no-op country events after a verified daily subscription yielded
two calls/copied records, same observed thread, depth 0, no invalid layouts or
drops. Vanilla source SHA-256 remained
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.
Exact executable/signature gates and a 1,024-record atomic reservation buffer
copied eight scalars without allocation, waits, formatting, I/O, exported
pointers, or pointers retained beyond the synchronous call. Repeated enabled
runs installed/removed the hook and exited natively without error.

Paired speed-5, 30-day, lifecycle-only runs used identical executable/plugins/save
within each pair and reached exact targets without telemetry drops:

| Fixture | Pairs | Disabled elapsed / CPU | Enabled elapsed / CPU | Probe calls |
| --- | ---: | --- | --- | ---: |
| Vanilla | 3 | Means 2.007435 / 2.833333 s | Means 1.903971 / 2.479167 s | 0 |
| GFM | 1 | 7.221915 / 9.109375 s | 6.769081 / 9.156250 s | 0 |

GFM deltas −6.3% elapsed / +0.5% CPU detected no regression in that pair;
source unchanged. Zero calls measure only the installed idle campaign path,
not installation or active-callback cost. Country-only identity correlation,
opaque source words, and untested nested reentrancy prevent a reusable API;
observed thread/lifetime are not general ownership guarantees.

RVA `0x004a8120`, a static accepted-event-delivery lead, had zero calls during
30-day GFM, direct event `18540`, and nested `32509` → `32510`. This rejects it
as the selected-option boundary, not all possible delivery semantics.

## Automatic event dispatch leads

[Issue #86](https://github.com/adrunkhuman/smedley_kernel/issues/86) rejected
RVA `0x0048d370` / `0x0048d6b0` as effect-attribution contracts: they include
dispatch/setup, and the province path can defer delivery. Object/context fields
did not establish stable event identity or country/province scope.

| Lead | Entry signature | Evidence |
| --- | --- | --- |
| Country `0x0048d370` | `55 8b ec 83 e4 f8 83 ec 5c 53 56 57 8b 7d 08 8b` | Relocation-free byte match recorded as `verified-current`; ABI/dispatch interpretation `verified-static-callsites` |
| Province `0x0048d6b0` | `55 8b ec 83 e4 f8 83 ec 50 56 8b 75 08 8b 46 40` | Same levels; both `__thiscall`, two stack arguments, `ret 8` |

Runtime roles remain `provisional`. Temporary `SMEDLEY_HIDDEN_EVENT_PROBE=1`
probe gated exact executable/signatures, copied ten scalars to a fixed 4,096
record buffer, tracked thread/depth/invalid layouts/drops, allocated no callback
memory and exported no pointer. Build base `f73e7ad` plus uncommitted probe;
no exact DLL hash retained. This fails exact-built-commit provenance.

Common settings: `v2game-3.04`, runner/telemetry, speed 5, queue 1,024,
lifecycle/state categories, daily AUS `treasury_raw`; probe variable only for
fixtures. Vanilla source hash was `f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`;
GFM hash is above. Sources stayed unchanged; game error log was empty after
fixture inspection.

| Attempt | Negative result / interpretation |
| --- | --- |
| Country, 10 days | Automatic AUS `990100` was configured to invoke triggered-only `990101` with `treasury = 123`. Fixture raw `479189538` → `356444112`; no-mod control → `356415061`. Difference 29,051, not expected 4,030,464 at 15-bit scale: base simulation explains the large decrease, fixture execution unproved. Fixture/control callbacks 2,548 / 2,539, no drops, zero country-candidate records; cannot reject the candidate at runtime. |
| Province, 60 days | Province-619 MTTH root lacked independent treasury marker. Shipped scripts supplied no direct `province_event` invocation example; automated console input did not prove manual acceptance. No real delivery correlation. |
| GFM, 10 days | `callback_count=0`, no attributable hook evidence. |

Neither active cost nor stable identity/scope/recursion was established. Probe,
public hook bit, telemetry integration, and fixtures were removed; post-removal
x86 Release passed 257 CTest tests. No production mapping/API for these leads
remains. Host success does not promote their runtime claims.

## Ownership conclusion

A future verified boundary may put hooks, copied opaque IDs, timing, bounds,
lifecycle, and loss reporting in the kernel; generic aggregation/allowlists in
a profiler. GFM classifications, eligibility, thresholds, and replacement policy
stay in mod scripts or a justified native feature plugin. All probe results here
are bounded historical evidence, not current instrumentation behavior.
