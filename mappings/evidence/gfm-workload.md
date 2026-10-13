# GFM v3.0 workload boundaries

GFM's script structure identifies repeated policy and engine capability needs,
not runtime cost. The historical baseline below measures the whole campaign;
no reviewed boundary supports generic script-site attribution. There is no
production attribution API. Runtime observations concern historical
implementations without exact-built-commit provenance, not acceptance of later
builds. GFM classifications, eligibility, thresholds, and replacement policy
remain mod policy, not kernel semantics.

## Immutable inputs

| Input | Identity |
| --- | --- |
| GFM repository / release | `Historical-Expansion-Mod/Greater-Flavor-Mod`, `v3.0` |
| Commit | `ee1486e251d3bb4dc5c355f5c2ec34abaea1078d` |
| Executable SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| Local save | `smedley-gfm-v3.v2` |
| Save date / player / header | `1836.1.1`, `PRU`; `government=9`, `unit=2800`, `state=2207` distinguish it from a same-named vanilla save |
| Mod configuration | Pinned GFM, only `mod/GFM.mod` selected, submods disabled; main-menu checksum `ZAEJ` |

The local save and generated reports/traces are not committed. These identities
bound the observations; they do not establish compatibility with other GFM
versions or make the measurements independently auditable.

## Static script structure

The pinned event source contains the following counts, reproducible with
`tools/clausewitz_workload_inventory.py` over `GFM/events` and root `GFM`:

| Construct | Count | Construct | Count |
| --- | ---: | --- | ---: |
| Event definitions | 7,416 | Explicit scheduling edges | 7,146 |
| MTTH blocks | 3,883 | Flag reads / writes | 29,228 / 39,155 |
| Modifier mutations | 4,998 | `random_owned` | 13,447 |
| `any_country` | 8,999 | `any_pop` | 5,975 |
| `random_pop` | 839 | `any_land_province` | 681 |

Counts omit runtime trigger outcomes, candidate populations, load-order
overrides, execution frequency, and engine optimization. In
`events/V2ME Cleanup (GFM).txt`, recurring scheduling includes:

| Event | Definition line | Recurring edge |
| --- | ---: | --- |
| `477877787` | 31 | self after one day, line 384 |
| `84393532` | 388 | scheduled by `477877790` after one day, line 19021 |
| `477877790` | 1110 | self after 30 days, line 19020 |
| `477877791` | 30938 | self after 365 days, line 55552; also fans out to numbered country events |

### Province-modifier policy

Static analysis identifies 996 province-modifier workflows and 736
modifier-writing iterator candidates; 19 participate in explicit scheduling
cycles. Thirteen have no local random-selection, finite-duration,
ownership-mutation, or MTTH risk. That local filter does not prove redundancy
or authorize deletion.

| Blocks in cleanup file | Policy fact / constraint |
| --- | --- |
| Monthly casino repair, 19016–19019 | Scans land provinces to restore a fixed province's permanent casino modifier. |
| Annual capital removal, 31346–31357 | Exact copy of monthly removal at 1142–1153. |
| Slave-trade removal, 1127–1141 | Removes stale penalties after abolition according to the current owner; narrow lookup/removal evidence is in [province reconciliation](province-reconciliation.md). |
| Monthly capital removal, 1142–1153; annual addition, 31358–31369 | Maintains capital-mobilisation supply-limit state; replacement requires capital-transition semantics. |
| Three local industry additions, 18399–18481 | Backfill after one-shot country events. |
| Collectivisation removal, 18484–18493 | Removes stale bonus after government change. |
| Coffee addition, 18730–18752 | Ownership/setup backfill. |
| Two Indian-lands blocks, 18995–19015 | Gameplay normalisation, not generic engine semantics. |
| Annual `drang_nach_osten` addition, 31672–31702 | Acquisition backfill. |

Casino repair selects province `3251` producing precious goods without
permanent `casino`. `monte_carlo_casino` sets both in
`decisions/monacoflavor.txt:18-26`; no other active writer/remover exists in the
pinned source. Deleting repair abandons old or externally modified saves.

The slave-trade cleanup is retained in pristine GFM. Its historical native
replacement establishes only the narrow boundary documented separately, not
all thirteen policies or the current package. A downstream replacement must
add equivalent behavior and remove the corresponding pinned script atomically.
Script reduction or correctness can justify such a change independently of
measured cost; static repetition alone cannot establish a speedup.

### Derived state flags

Recurring flags have distinct contracts, not one removable-cache family:

| Flags | Policy contract |
| --- | --- |
| `medicine_invented`, `germs_invented`, `gaslights_invented`, `electricity_invented`, `modern_central_bank_system_invented` | Technology/invention mirrors read by POP modifiers and country policy; all reader scopes must remain covered. |
| `biologism_invented` | One country-scope technology reader. |
| `monarchy_government`, `theocratic_government` | Broad government classifications. |
| `new_world_nation_fr` | Government-derived New World emigration policy. |
| `can_use_monthly` | Deliberate decision pulse, not just cached state. |
| Accepted-culture flags | Provenance for colonial culture grants; replacement requires culture and relationship semantics, not merely recomputing a boolean. |
| `revolution_n_counter_researched`, `colonial_cores_africa_active` | Irreversible global phase/scheduler latches with save-visible state. |

`Naval Fix.txt` redistributes a treasury debit through random POP transfers;
historical RGO, capital, government, and annexation scripts likewise encode GFM
gameplay policy. Their presence is not evidence of a replaceable engine bug.
Ghost-unit/relationship repairs remain engine-bug-workaround hypotheses, not
verified native defects. Numbered dismantlement variables and repeated scope
bridges do not establish a native collection or transaction contract.

## Historical campaign cost

Two measurements of the pinned GFM/PRU input used an MSVC x86 Release build,
normal player control, native speed 5, `campaign_runner`, and lifecycle-only
telemetry over an exact 30-game-day interval. No state collector was configured;
collector/POP scratch allocation was lazy, and telemetry PE
`SizeOfImage=0x50000`. These are necessary build-specific memory conditions.
No exact-built-commit provenance is recorded.

| Metric | Measurement |
| --- | --- |
| Elapsed interval | 5.340159–5.410516 s; mean 5.375338 s |
| Mean throughput | 5.581 game days/s |
| Process CPU | 6.453125–7.484375 s |
| Peak working set | 2,851,201,024–2,898,456,576 bytes |
| Peak private bytes | 3,298,156,544–3,300,118,528 bytes |
| Peak virtual bytes | 3,773,878,272–3,801,116,672 bytes |

This measures whole-campaign cost, not individual scripts or current-build
performance. CPU can exceed elapsed time because Windows sums across process
threads. Two samples do not bound variance or support optimization claims.
Historical GFM POP state capture was not established on that collector path;
this is not a claim about the [current collector](../../docs/telemetry/README.md).

## Region scope iteration boundary

GFM's `any_land_province` is a custom region in `map/region.txt`, not a native
operator: 3,385 unique IDs, range 1–3,986. Uses compile to generic
`CRegionScopeEffect` / `CRegionScopeTrigger` objects.

| Boundary | `verified-static-callsites` contract |
| --- | --- |
| Effect RVA `0x00488ba0` | x86 `__thiscall`, two stack arguments, `ret 8`; `this+0x3c` region. Traverse `CRegion+0x8`, construct province scope, evaluate `this+0x20` filter, run `this+0x8` child effect chain for matches. Timing includes traversal, filtering, and effects. |
| Trigger RVA `0x004c5da0` | x86 `__thiscall`, one stack argument, `ret 4`; `this+0x1c` region. Nested triggers per province; first match returns true, exhaustion false. Region cardinality is not the short-circuited visit count. |

Entry signatures stop before ASLR-relocated exception-handler operands; use the
[mapping catalog](../v2game-3.04.toml). Stable script-site identity, visit counts,
and active overhead are not established. Trigger execution remains unobserved;
effect execution lacks source-site correlation. Neither symbol is promoted to
`verified-runtime`.

## Selected-event-option boundary

RVA `0x00507e00` is `verified-runtime` only as the selected-event-option action
executor, not generic event/trigger/effect attribution. Static x86 MSVC
`__thiscall` uses `ECX` action and synchronously calls option executor
`0x004a61f0`. Its signature `55 8b ec 83 e4 f8 6a ff` is relocation-free;
the later exception-handler operand is not safe to match literally under ASLR.

| Action field | Meaning / limit |
| --- | --- |
| `+0x64` | Selected option index, correlated only for two country-event actions. |
| `+0x68` | Embedded event snapshot; not a general lifetime or ownership guarantee. |
| `+0x70` / `+0x74` | Opaque source words; source-key semantics unresolved. |
| `+0x40` / `+0x44` | Resolver words; second word matched the country-event ID in those two actions, not established for other scopes. |

Country-only identity correlation, opaque source words, and unverified nested
reentrancy prevent a reusable attribution contract. Observed thread/lifetime
are not general ownership guarantees. Active-callback cost is unknown;
measurements with no calls cannot bound it.

RVA `0x004a8120` is not the selected-option boundary: selected-option execution,
including nested country-event execution, did not call it. Other possible
accepted-event-delivery semantics remain unverified.

## Automatic event dispatch leads

RVA `0x0048d370` / `0x0048d6b0` include dispatch/setup rather than only effect
execution; the province path can defer delivery. Inclusive handler time
therefore cannot be treated as script-effect time. Object/context fields do not
establish stable event identity or country/province scope.

| Lead | Entry signature | Evidence |
| --- | --- | --- |
| Country `0x0048d370` | `55 8b ec 83 e4 f8 83 ec 5c 53 56 57 8b 7d 08 8b` | Relocation-free byte match recorded as `verified-current`; ABI/dispatch interpretation `verified-static-callsites`. |
| Province `0x0048d6b0` | `55 8b ec 83 e4 f8 83 ec 50 56 8b 75 08 8b 46 40` | Same levels; both `__thiscall`, two stack arguments, `ret 8`. |

Runtime roles remain `provisional`. Automatic delivery has not been independently
correlated at either candidate. Absence of callbacks without demonstrated event
execution cannot reject a candidate at runtime. Stable identity, scope,
recursion, and active cost remain unknown; no production mapping/API for these
leads is retained.
