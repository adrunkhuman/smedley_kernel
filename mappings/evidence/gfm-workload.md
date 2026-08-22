# GFM v3.0 workload discovery

Issue [#9](https://github.com/adrunkhuman/smedley_kernel/issues/9) uses Greater
Flavor Mod as a requirements and measurement workload. GFM policy is not a
kernel compatibility contract.

## Immutable inputs

| Input | Identity |
| --- | --- |
| GFM repository | `Historical-Expansion-Mod/Greater-Flavor-Mod` |
| Release tag | `v3.0` |
| Commit | `ee1486e251d3bb4dc5c355f5c2ec34abaea1078d` |
| Executable SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| Save | `smedley-gfm-v3.v2`, SHA-256 `3c70553984e4d94773483076ee4d83ac351095042f66a9920a3ee4770f7763f4` |
| Save date and player | `1836.1.1`, `PRU` |

The save is a local legal fixture and is not committed. Its GFM-sized state is
distinguishable from an unrelated same-named vanilla save by the hash above,
`government=9`, `unit=2800`, and `state=2207` in its header.

The installed `GFM` directory was compared with the detached checkout using:

```powershell
robocopy "<gfm-checkout>\GFM" "<game-dir>\mod\GFM" /MIR /L
```

The command returned exit code `0`: it reported no files needing copy, no extra
destination files, and no failures. The shipped submods remained separate and
disabled. With only `mod\GFM.mod` selected under the supported executable, the
game reached the main menu and reported checksum `ZAEJ`.

## Static inventory

The generic inventory command was:

```powershell
python tools/clausewitz_workload_inventory.py `
  "<gfm-checkout>\GFM\events" `
  --root "<gfm-checkout>\GFM" `
  --output gfm-v3-events-inventory.json
```

The report was generated with Python 3.14.7 and is not committed. Its summary
was:

| Lead | Count |
| --- | ---: |
| Event definitions | 7,416 |
| Explicit scheduling edges | 7,146 |
| MTTH blocks | 3,883 |
| Flag reads | 29,228 |
| Flag writes | 39,155 |
| Modifier mutations | 4,998 |
| `random_owned` blocks | 13,447 |
| `any_country` blocks | 8,999 |
| `any_pop` blocks | 5,975 |
| `random_pop` blocks | 839 |
| `any_land_province` blocks | 681 |

These counts are source-discovery leads, not performance measurements. They do
not account for trigger outcomes, candidate populations, load-order overrides,
event execution, or engine optimization.

The parser independently recovered the central cleanup chain in
`events/V2ME Cleanup (GFM).txt`:

| Event | Definition line | Recurring edge |
| --- | ---: | --- |
| `477877787` | 31 | self after one day at line 384 |
| `84393532` | 388 | scheduled by `477877790` after one day at line 19021 |
| `477877790` | 1110 | self after 30 days at line 19020 |
| `477877791` | 30938 | self after 365 days at line 55552 |

The annual event also fans out to many numbered country events. This confirms
that the inventory can locate recurring and partitioned script work. It does
not establish that the chain is expensive or that its effects are generic.

## Candidate classification

| Candidate | Provisional classification | Static evidence | Required runtime evidence |
| --- | --- | --- | --- |
| Script-site attribution | Reviewed leads rejected; capability remains missing | The selected-option executor is too narrow, and the automatic-event leads below are not effect execution boundaries | A different boundary with controlled identity correlation and measured hook overhead |
| Province-modifier reconciliation | Missing mutation/operation capability | Cleanup contains repeated province scans and checked modifier additions | Visits, matched provinces, mutations, and attributed time |
| Derived state flags | Performance-workaround hypothesis | GFM mirrors technology, invention, existence, and government facts into flags | Read/write executions and trigger-time comparison |
| Ghost unit and relationship repair | Engine-bug-workaround hypothesis | Cleanup explicitly repairs residual entities and relationships | Reproduction without repair and verified native entity identity |
| Dismantlement slot variables and scope bridges | Engine-limitation/workaround hypothesis | Numbered variables and repeated scope traversal emulate collections and transactions | Executed paths and behavior-preserving operation contract |
| `Naval Fix.txt` redistribution | Ordinary GFM-specific gameplay policy | Treasury debit plus repeated random POP transfers | Three-way economic matrix before any overlap claim |
| Historical RGO, capital, government, and annexation work | Ordinary GFM-specific gameplay policy | Eligibility and outcomes are defined by GFM | Not a kernel replacement target |

## Runtime baseline

The baseline uses pure GFM v3.0, the pinned PRU save, speed 5, normal player
control, `campaign_runner`, and lifecycle-only telemetry. Each run advances
exactly 30 game days and exits natively. The source-save hash remained unchanged.

| Runtime input | Identity |
| --- | --- |
| Build | MSVC x86 Release |
| `smedley_cli.exe` | SHA-256 `7d46547b3f7ecb4e93ca1d601538c6c01a9dfbf5a81587b54ead56e63afdc055` |
| `smedley_kernel.dll` | SHA-256 `7fb8f2bfe512d574c11b19c6020a61a694abd41b8914394be2da9a2fdf8e9a5f` |
| `campaign_runner.dll` | SHA-256 `21a55be5db0a69b716b0ba000a53aead3c5051f57296698466bdffe016609f3d` |
| `telemetry.dll` | SHA-256 `076d3c234700404a6e0a6c9bc9562eee977f5e489a6d462249b0365a9d5ab363` |

```powershell
& "<game-dir>\smedley_cli.exe" `
  --game-dir "<game-dir>" --mod "mod\GFM.mod" `
  --plugin "plugins\campaign_runner.toml" --plugin "plugins\telemetry.toml" `
  --telemetry --telemetry-category lifecycle `
  --save "<gfm-save>" `
  --speed 5 --run-days 30 --quit-after-run --run-timeout-seconds 600 --detach
```

| Sample | Elapsed | Process CPU | Peak working set | Peak private bytes | Peak virtual bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 5.340159 s | 6.453125 s | 2,898,456,576 | 3,300,118,528 | 3,773,878,272 |
| 2 | 5.410516 s | 7.484375 s | 2,851,201,024 | 3,298,156,544 | 3,801,116,672 |

Mean elapsed time is 5.375338 seconds, or 5.581 game days per second. Both
traces contain `benchmark.completed` and `telemetry.summary` with zero drops.
This is a workload baseline, not evidence that any static candidate is costly.
The generated report and traces are not committed, so the measurements are
recorded observations rather than independently auditable artifacts.

Lifecycle-only telemetry configures no state collector. The telemetry DLL uses
lazy collector construction and lazy population scratch buffers; its PE
`SizeOfImage` is `0x50000` instead of carrying an approximately 16 MiB TLS
template.

A two-day GFM run with daily PRU-scoped `pop.aggregate` capture completed the
campaign benchmark, but its trace contained no state records and reported
`callback_count=0`; no failure reason was recorded. It used the baseline command
with `--telemetry-category state`,
`--telemetry-capture "pop.aggregate|daily|pop_count|PRU|||"`, and
`--run-days 2`. The source save remained unchanged. The same capture path
emitted valid state records from the smaller vanilla fixture. GFM state capture
is therefore unverified and is not part of this baseline.

## Script application boundary

RVA `0x00507e00` is a verified runtime selected-event-option action executor.
Static review identifies a normal MSVC x86 `__thiscall` entry with the action
object in `ECX`; it synchronously calls the narrower option executor at RVA
`0x004a61f0`. The action object carries the selected option index at `+0x64`,
an embedded event snapshot at `+0x68`, opaque source-key words at `+0x70` and
`+0x74`, and resolver-key words at `+0x40` and `+0x44`. The eight-byte entry
signature is relocation-free; the later exception-handler operand is relocated
under ASLR and must not be matched as a literal runtime byte sequence.

A bounded, opt-in observation probe correlated two disposable no-op country
events after a verified daily subscription boundary. Event `990001` option 0
produced resolver words `(39, 990001)`. Event `990002` option 1 produced
resolver words `(39, 990002)`. Both produced source words `(5128522, 221)`,
ran on the same observed game thread at depth 0, and completed with two calls,
two copied records, no invalid layouts, and no drops. This verifies that
resolver word 1 is the event ID for these controlled country events and that
`+0x64` identifies the selected option. Source-key semantics remain opaque.
The disposable source save remained unchanged at SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

The probe accepted only the exact supported executable and the relocation-free
entry signature. Its callback used fixed-capacity atomic reservation for 1,024
records, copied eight scalar values, reported invalid layouts and drops, and
performed no allocation, waiting, formatting, or filesystem work. No pointer
crossed the C ABI or survived the synchronous call. Both controlled calls ran
on the same thread at observed recursion depth 0. That establishes the
exercised object lifetime and thread but does not prove broader thread ownership
or nested reentrancy behavior. Repeated enabled runs installed and removed the
hook and exited through the verified native lifecycle without error.

Paired fixed-interval runs used the same executable, plugins, saves, speed 5,
30-day target, and lifecycle-only telemetry. Three vanilla pairs had no probe
calls; disabled mean elapsed and process CPU were 2.007435 s and 2.833333 s,
while enabled means were 1.903971 s and 2.479167 s. One GFM pair also had no
probe calls: disabled elapsed and CPU were 7.221915 s and 9.109375 s; enabled
elapsed and CPU were 6.769081 s and 9.156250 s. The GFM enabled-minus-disabled
deltas were -6.3 percent elapsed and +0.5 percent CPU. This single GFM pair
detected no fixed-interval regression. Because none of these runs invoked the
probe, they measure only the installed idle path during the campaign interval,
not installation time or active callback overhead. All runs reached the exact
target with no telemetry drops. The GFM source save remained unchanged at SHA-256
`3c70553984e4d94773483076ee4d83ac351095042f66a9920a3ee4770f7763f4`.

Static review suggested RVA `0x004a8120` was accepted-event delivery, but a
30-day GFM run, direct event 18540, and nested event edge 32509 to 32510 all
produced zero calls. Runtime evidence therefore rejects it as the selected-option
boundary; the delivery interpretation remains only a static search lead.

RVA `0x00507e00` is therefore verified for the narrow selected-event-option
executor claim, not as a generic event, trigger, or effect attribution service.
Only controlled country events were correlated, source-key semantics remain
opaque, and nested reentrancy was not established. Those limits do not support
a reusable production API. The exploratory selected-option hook, mod fixture,
probe-specific telemetry API, and runtime emission path were removed after
correlation.

## Automatic event dispatch leads

Issue [#86](https://github.com/adrunkhuman/smedley_kernel/issues/86)
reviewed preferred RVAs `0x0048d370` and `0x0048d6b0` as possible automatic
country- and province-event effect attribution boundaries.

| Claim | Evidence level | Evidence |
| --- | --- | --- |
| Entry bytes | `verified-current` | Both relocation-free signatures match the exact supported executable. |
| x86 ABI | `verified-static-callsites` | Both are `__thiscall` entries with two stack arguments and `ret 8`. |
| Not an effect boundary | `verified-static-callsites` | Both are dispatch/setup leads rather than the narrower option-effect executor; the province path can defer delivery. |
| Runtime country/province role | `provisional` | The probe recorded zero calls, but controlled fixture execution was not independently proved. |

| Lead | Entry signature |
| --- | --- |
| Country, `0x0048d370` | `55 8b ec 83 e4 f8 83 ec 5c 53 56 57 8b 7d 08 8b` |
| Province, `0x0048d6b0` | `55 8b ec 83 e4 f8 83 ec 50 56 8b 75 08 8b 46 40` |

Entry timing at either lead would include scheduling or delivery work without
proving that an option's effects ran. The reviewed object words and context
fields did not establish stable event identity or country/province scope
semantics. This rejects the proposed effect-attribution contract statically; it
does not establish what complete runtime paths use either entry.

A temporary probe accepted only the exact supported executable and both entry
signatures. Each entry detour copied ten scalar observations into a fixed 4,096
record buffer, tracked thread and recursion depth, and reported invalid layouts
and drops. The callback allocated no memory and exported no pointer. It was
enabled only by `SMEDLEY_HIDDEN_EVENT_PROBE=1`.

The exploratory build was based on commit `f73e7ad` plus uncommitted probe
source. No hash of that exact probe DLL was retained. This fails the project's
exact-built-commit provenance requirement, so the runtime observations below
cannot promote either candidate to a supported runtime claim.

All runs used executable mapping `v2game-3.04`, `campaign_runner`, `telemetry`,
speed 5, a queue capacity of 1,024, lifecycle and state categories, and daily
AUS `treasury_raw` capture. Fixture runs additionally set
`SMEDLEY_HIDDEN_EVENT_PROBE=1`. The source saves remained unchanged, and the
Victoria II error log was empty when inspected after the fixture runs.
The vanilla source-save SHA-256 was
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`;
the GFM source-save identity is recorded in the immutable-input table above.

The country fixture configured automatic event `990100` for AUS to invoke
triggered-only country event `990101`, whose option contained
`treasury = 123`. The fixture trace changed from raw `479189538` to
`356444112` across the expected date. The no-mod control changed from the same
`479189538` to `356415061`. The traces differ by only `29051` raw at that date,
not the `4030464` raw corresponding to 123 at the verified 15-bit treasury
scale. The large decrease therefore came from the base simulation and does not
prove that the fixture event executed. The ten-day fixture completed 2,548
callbacks with no drops and zero country-candidate records; the no-mod control
completed 2,539 callbacks with no drops. The zero-call result cannot reject
`0x0048d370` at runtime.

Province runtime correlation was also not established. A province-619 MTTH
root did not produce its independent treasury marker during the 60-day run.
Shipped vanilla scripts provided no example of one event directly invoking a
`province_event` effect, and automated console input did not establish that a
manual command was accepted. These negative controls do not prove that
`0x0048d6b0` is absent from a real province delivery path.

A ten-day GFM run completed with `callback_count=0`, so it supplied no
attributable hook evidence. No active-callback overhead measurement was possible
because neither candidate recorded a call. Stable identity, scope, recursion,
and active-cost contracts remain unproven. The automatic-event probe, its public
hook bit and telemetry integration, and the disposable mod fixtures were
removed. The post-removal x86 Release build passed all 257 CTest tests, and no
production API or executable mapping for either automatic-event candidate was
retained.

## Ownership conclusion

If attribution is verified, the kernel may own the hook, copied opaque IDs,
timing, bounds, lifecycle, and loss reporting. A generic profiler may own
allowlists and aggregation. GFM event identities, classifications, eligibility,
thresholds, and replacement decisions remain mod or plugin policy.
