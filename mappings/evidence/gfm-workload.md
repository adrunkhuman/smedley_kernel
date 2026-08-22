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
robocopy C:\Users\padni\Documents\Python\vic2\gfm_upstream\GFM `
  C:\Users\padni\Documents\Python\vic2\game\mod\GFM /MIR /L
```

The command returned exit code `0`: it reported no files needing copy, no extra
destination files, and no failures. The shipped submods remained separate and
disabled. With only `mod\GFM.mod` selected under the supported executable, the
game reached the main menu and reported checksum `ZAEJ`.

## Static inventory

The generic inventory command was:

```powershell
python tools/clausewitz_workload_inventory.py `
  C:\Users\padni\Documents\Python\vic2\gfm_upstream\GFM\events `
  --root C:\Users\padni\Documents\Python\vic2\gfm_upstream\GFM `
  --output gfm-v3-events-inventory.json
```

The analyzer source SHA-256 is
`121725cb9fb2aadbb196b3d81f480b7dbc07bff4285c031154473771b251a39c`.
The generated report is not committed. Its SHA-256 is
`f2d824ed72798bc22a5ad01e7a0ac2a636ab4876105b8c0402655cbce969efdd`.
It was generated with Python 3.14.7 and can be identified with
`Get-FileHash -Algorithm SHA256 gfm-v3-events-inventory.json`. Its summary was:

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
| Script-site attribution | Missing observation/query capability | No current boundary attributes time or execution to Victoria II script sites | Controlled identity correlation and measured hook overhead |
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
& "C:\Users\padni\Documents\Python\vic2\game\smedley_cli.exe" `
  --game-dir "C:\Users\padni\Documents\Python\vic2\game" --mod "mod\GFM.mod" `
  --plugin "plugins\campaign_runner.toml" --plugin "plugins\telemetry.toml" `
  --telemetry --telemetry-category lifecycle `
  --save "C:\Users\padni\Documents\Paradox Interactive\Victoria II\GFM\save games\smedley-gfm-v3.v2" `
  --speed 5 --run-days 30 --quit-after-run --run-timeout-seconds 600 --detach
```

| Run | Trace SHA-256 | Elapsed | Process CPU | Peak working set | Peak private bytes | Peak virtual bytes |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `ef146a3b-38d3-44e1-b0ba-563107516c70` | `7cf18bbb5d7124e247c4659576a6de547567a1882277095e1f41df4c10ea96cc` | 5.340159 s | 6.453125 s | 2,898,456,576 | 3,300,118,528 | 3,773,878,272 |
| `d4e2b230-39c1-4964-95c3-34a9b2f79c9a` | `db40ba017a8aeac8f42df3e72a7a07640236e1d61c14895fc8c491b8571f0daa` | 5.410516 s | 7.484375 s | 2,851,201,024 | 3,298,156,544 | 3,801,116,672 |

Mean elapsed time is 5.375338 seconds, or 5.581 game days per second. Both
traces contain `benchmark.completed` and `telemetry.summary` with zero drops.
This is a workload baseline, not evidence that any static candidate is costly.
The report and traces are local generated artifacts and are not committed; their
hashes identify the retained files but do not replace those files for audit.

Lifecycle-only telemetry configures no state collector. The telemetry DLL uses
lazy collector construction and lazy population scratch buffers; its PE
`SizeOfImage` is `0x50000` instead of carrying an approximately 16 MiB TLS
template.

A two-day GFM run with daily PRU-scoped `pop.aggregate` capture completed the
campaign benchmark, but its trace contained no state records and reported
`callback_count=0`; no failure reason was recorded. It used the baseline command
with `--telemetry-category state`,
`--telemetry-capture "pop.aggregate|daily|pop_count|PRU|||"`, and
`--run-days 2`. The source save remained unchanged. Run
`d298fc90-e03d-4773-94f8-664c063a2d59` produced trace SHA-256
`c9fea4f8fe4523916c0d6ddc6406de9af1fc203665cc3dbb7d8f9531e5fa7e07`.
The same capture path emitted valid state records from the smaller vanilla
fixture in run `b8089fd8-10a7-41e4-8948-a2879c9074de`, trace SHA-256
`1926e646cf324a75a5e0ac593ed7a2bc770db5b129152d75039e07f0453faea7`.
GFM state capture is therefore unverified and is not part of this baseline.

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
ran on thread `16032` at depth 0, and completed with two calls, two copied
records, no invalid layouts, and no drops. This verifies that resolver word 1
is the event ID for these controlled country events and that `+0x64` identifies
the selected option. Source-key semantics remain opaque. The trace SHA-256 is
`d82d1a89a1f8163a578fc4446b2d70f05a467074d05dc62856d64684bb11897f`.
The disposable source save remained unchanged at SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

The probe accepted only the exact supported executable and the relocation-free
entry signature. Its callback used fixed-capacity atomic reservation for 1,024
records, copied eight scalar values, reported invalid layouts and drops, and
performed no allocation, waiting, formatting, or filesystem work. No pointer
crossed the C ABI or survived the synchronous call. Both controlled calls ran
on thread `16032` at observed recursion depth 0. That establishes the exercised
object lifetime and thread but does not prove broader thread ownership or nested
reentrancy behavior. Repeated enabled runs installed and removed the hook and
exited through the verified native lifecycle without error.

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
target with no telemetry drops. The disabled and enabled GFM trace SHA-256 values
are `f4c1b187b202a5a7f25a1bcceb43d9d195a9e786f6757becb7cf7a80f649532a`
and `5eaba5a4416b7537679ffc181cc2f013667dfaf91b73fbca0559df0c86f56d02`.
The GFM source save remained unchanged at SHA-256
`3c70553984e4d94773483076ee4d83ac351095042f66a9920a3ee4770f7763f4`.

Static review suggested RVA `0x004a8120` was accepted-event delivery, but a
30-day GFM run, direct event 18540, and nested event edge 32509 to 32510 all
produced zero calls. Runtime evidence therefore rejects it as the selected-option
boundary; the delivery interpretation remains only a static search lead.

RVA `0x00507e00` is therefore verified for the narrow selected-event-option
executor claim, not as a generic event, trigger, or effect attribution service.
Only controlled country events were correlated, source-key semantics remain
opaque, and nested reentrancy was not established. Those limits do not support
a reusable production API. The exploratory hook, mod fixture, telemetry API,
and runtime emission path were removed after correlation.

## Ownership conclusion

If attribution is verified, the kernel may own the hook, copied opaque IDs,
timing, bounds, lifecycle, and loss reporting. A generic profiler may own
allowlists and aggregation. GFM event identities, classifications, eligibility,
thresholds, and replacement decisions remain mod or plugin policy.
