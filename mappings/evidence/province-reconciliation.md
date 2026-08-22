# Province reconciliation boundaries

Issue [#91](https://github.com/adrunkhuman/smedley_kernel/issues/91) uses
GFM's slave-trade cleanup to drive generic province-modifier and country-policy
capabilities. This document records static executable evidence, read-only
runtime correlation, and controlled production mutation on the supported
executable.

## GFM contract

The source contract is pinned to GFM v3.0 commit
`ee1486e251d3bb4dc5c355f5c2ec34abaea1078d`. Country event `477877790`
iterates `any_land_province` at
`events/V2ME Cleanup (GFM).txt:1127-1141`. A province qualifies when it has at
least one of `slave_trade_decadence_1` through `_4` and its current owner has
`slavery = no_slavery`. The block removes all four modifier names.

The event reschedules itself after 30 days at line 19020. The reconciliation
block contains no random operation. Separate RGO events create and advance the
four permanent stages, and event `38506` independently removes them while
changing the RGO. The generated GFM-smedley source must therefore disable only
lines 1127-1141; the writers and independent remover remain part of the
standalone mod.

## Modifier lookup

The executable contains RTTI for `CAddProvinceModifierEffect`,
`CRemoveProvinceModifierEffect`, and `CHasProvinceModifierTrigger`. Their parser
paths resolve script names through the same registry pointer at preferred VA
`0x0125a69c` and lookup function at RVA `0x001661b0`:

| Parser | Resolved field | Evidence |
| --- | ---: | --- |
| Add effect, RVA `0x0048132d` | effect `+0x24` | Loads the registry pointer, passes the parsed string to RVA `0x001661b0`, and retains the returned definition pointer. |
| Remove effect, RVA `0x00482170` | effect `+0x20` | Uses the same registry and lookup path. |
| Presence trigger, RVA `0x004cdea0` | trigger `+0x1c` | Uses the same registry and lookup path. |

The lookup takes its registry in `EAX`, one stack-passed MSVC string pointer,
and returns an opaque definition pointer. Registry identity, lifetime, duplicate
key behavior, and lookup safety outside script loading remain unverified.

## Province modifier storage

`CHasProvinceModifierTrigger::Evaluate`, RVA `0x004cdfc0`, resolves the current
province from scope `+0x10`, reads the province's list head at `+0xc8`, and walks
a linked list. Each node points to an active modifier record; record `+0x08` is
compared with the trigger's resolved definition pointer at `this+0x1c`.

The linked-list contract recovered from the query, add, and remove paths is:

| Object | Field | Meaning |
| --- | ---: | --- |
| `CProvince` | `+0xc8` | Active-modifier list head |
| `CProvince` | `+0xcc` | Active-modifier list tail |
| `CProvince` | `+0xd0` | Active-modifier count |
| `CProvince` | `+0xd4` | Nonzero while physical unlinking must be deferred |
| List node | `+0x00` | Active modifier record pointer |
| List node | `+0x04` | Previous node |
| List node | `+0x08` | Next node |
| List node | `+0x0c` | Deferred-removal marker |
| Active modifier | `+0x08` | Modifier definition pointer |
| Active modifier | `+0x0c` | Expiry/date value used by the add path |

The active modifier's complete type, ownership, expiry encoding, and interaction
with save serialization remain unverified.

## Modifier removal

`CRemoveProvinceModifierEffect::Execute`, RVA `0x00482290`, is the effect-chain
entry reached through virtual slot `+0x2c`. It resolves the current province from
scope `+0x10`, loads the definition pointer from `this+0x20`, places the
`CProvince*` in `ESI`, passes the definition on the stack, and calls RVA
`0x000b39a0`.

The remover traverses the list and compares active-record `+0x08` with the
requested definition. When province `+0xd4` is nonzero it sets node `+0x0c`
instead of unlinking. Otherwise it repairs previous, next, head, and tail links,
frees the node, decrements `CProvince+0xd0`, and continues so duplicate matching
records are also removed. It returns with `ret 4`.

The nonstandard internal ABI is therefore:

- `ESI`: borrowed `CProvince*`;
- stack argument 0: borrowed modifier-definition pointer;
- no explicit success result;
- removal may be deferred by the province iteration guard.

No reviewed callsite establishes that this function is safe from an arbitrary
daily callback. The guard also means immediate absence is not a valid
postcondition in every phase. Thread, phase, lifetime, and deferred-drain
behavior require runtime evidence before a checked mutation can be exposed.

## Political reform selection

`CIssueGroupTrigger` parses its option key at RVA `0x004c4a70`. It loads the
registry pointer at preferred VA `0x01258a48`, passes the parsed MSVC string to
lookup RVA `0x00575ed0`, and receives an issue-option pointer. The parser accepts
the option only when option `+0x54` equals the trigger's group at `this+0x20`,
then retains it at `this+0x24`.

`CIssueGroupTrigger::Evaluate`, RVA `0x004c4da0`, compares an expected issue
option at `this+0x24` against the current country's selected option. The issue
group at `this+0x20` supplies its group index at `+0x50` and category at `+0x5c`.
For a province scope, owner key/index words are stored at `CProvince+0x128` and
`+0x12c`. The evaluator uses the index to resolve `CCountry*` through the
country database pointer at preferred VA `0x012587e4`, whose pointer array is at
database `+0x4`. Category `2` then reads the selected-option array pointer from
`CCountry+0x860` and indexes that array by the issue-group index.

`CSetPoliticalReformEffect::Execute`, RVA `0x004950b0`, independently writes the
selected option into the array referenced by `CCountry+0x860`. It obtains the
index from the target option's group pointer at option `+0x54`, then group
`+0x50`. This establishes the selected-political-reform array and issue/group
relationship at `verified-static-callsites`.

The pinned GFM source defines `slavery` as a political issue and `no_slavery` as
one option. Static lookup by the option key is established, and the returned
option supplies its owning group. Registry lifetime, country identity, array
bounds, and live transition behavior remain unverified.

## Read-only runtime correlation

Launcher run `56e41037-8f4a-4196-b8a6-df7cbad44668` loaded the pinned GFM v3.0
save for one game day at speed 5 with `campaign_runner` and a temporary
read-only probe. The probe ran once at the verified before-bank-interest
callback, accepted the exact executable and both lookup signatures, bounded all
foreign vectors and lists, and performed no engine write. It copied results for
a worker thread to format and write.

| Runtime input | Identity |
| --- | --- |
| Repository HEAD before the uncommitted issue work | `e4befd2040961ff18968cceb74aca644ede27f57` |
| `v2game.exe` | SHA-256 `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| `smedley_kernel.dll` | SHA-256 `46e76f0ef78e9307a8ba414dc2d60fa4a94ff6b79f50c50c1d7b80c4461fab9b` |
| `campaign_runner.dll` | SHA-256 `a86058e05c043dd6502e68d2bd2da1922a3949c0ad44f258377ee09b30eb88fe` |
| Temporary probe DLL | SHA-256 `28a9c8edb74d9386f12c06d0cbdd2c09042e47c607a74f7af564e08b3cfecac5` |
| Source save before and after | SHA-256 `3c70553984e4d94773483076ee4d83ac351095042f66a9920a3ee4770f7763f4` |

All four `slave_trade_decadence_*` lookups returned distinct non-null
definitions. `no_slavery` resolved to a non-null option whose owning group had
index `1` and category `2`. The selected-option path found 311 countries with
that exact option among 653 country slots.

The province vector contained 3,987 readable slots. Traversal visited 805
active-modifier nodes, agreed with every province's declared list count, found
no active iteration guard, and reported no invalid record. No active record used
one of the four slave-trade definitions in this save, so the run cannot validate
their presence correlation or any removal behavior. GFM's error log remained
empty and the game exited through the verified native request.

The temporary source, manifest, DLL, output, and compiler artifacts were
removed. Because the probe was external to the committed tree and the fixture
contained no target modifier, these observations corroborate lookup lifetime,
policy selection, and generic list traversal but do not promote the mappings or
authorize production mutation.

## Current packaging contract

The current implementation generates standalone `mod/GFM-smedley` directly
from pinned GFM commit `ee1486e251d3bb4dc5c355f5c2ec34abaea1078d`.
It preserves GFM's five `replace_path` directives, writes the alphanumeric
descriptor filename `GFMSmedley.mod`, retains visible mod and user-directory
identity `GFM-smedley`, and removes only event `477877790`'s pinned 15-line
reconciliation block. `mod/GFM` is neither an input nor a launch dependency.

The generated `smedley-required-plugins.txt` declares
`required_plugin=gfm_adoption`. Launcher preflight rejects GFM-smedley in safe
mode or without that plugin. Plugin initialization runs before the suspended
game process resumes; an initialization failure terminates the child. A checked
service failure after campaign start is fatal because continuing would leave
the simplified mod without its required reconciliation behavior. `DEFERRED`
performs no mutation and leaves the cadence pending for a later eligible
callback. Requirement enforcement applies only to launches through Smedley;
direct game launches can bypass launcher preflight.

### Standalone production reconciliation runtime

Startup run `2ac35102-c1da-41b8-82b4-5f974d626b94` selected only
`mod/GFMSmedley.mod` and the required `gfm_adoption` plugin. Victoria II
created the separate `GFM-smedley` user directory and map cache, loaded GFM's
province history, decisions, and events, and reached the frontend with checksum
`BBSV`. The plugin accepted simplified event SHA-256
`34709cb3ae8c40cba441a05c6cba57952e84d5c6a234464879e14254dc1546c0`.

Run `aae4ca66-18dc-4e1d-83e0-72cb61041421` then selected
`mod/GFMSmedley.mod`, `campaign_runner`, and `gfm_adoption`. It loaded a
disposable fixture derived from a fresh GFM-smedley Prussia campaign, advanced
exactly three days at speed 5 from `1836.1.1` through `1836.1.4`, and paused at
the exact target. The resulting campaign was saved before normal exit.
Post-review run `d8746d73-370e-47bd-a68c-7f249d0077ff` replayed the same
fixture with the final kernel binary, reached the same target without a
reconciliation failure, and requested native game exit.

| Runtime input | Identity |
| --- | --- |
| Repository base plus issue work | Base `e4befd2040961ff18968cceb74aca644ede27f57` |
| `v2game.exe` | SHA-256 `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| Final replay `smedley_kernel.dll` | SHA-256 `598b0501c82531bf2338e7a63d03decc56a81435ff1694164cbe369713e81a4b` |
| Saved-result `smedley_kernel.dll` | SHA-256 `11df3cb1afc188d289a2915e7dd506c862c978d3b43a41b604568772a29fdaf4` |
| `gfm_adoption.dll` | SHA-256 `6f20d9041eda33315f8ac16d58ac0d1015e68f062581a71b3e0aa2f7ce02ec52` |
| `campaign_runner.dll` | SHA-256 `a86058e05c043dd6502e68d2bd2da1922a3949c0ad44f258377ee09b30eb88fe` |
| Fresh source save before and after | SHA-256 `b32dc06746be5a77512081d778a6cc7b4d060c9f28e77cd6f0939153c33de1ac` |
| Disposable input fixture | SHA-256 `0d1c453c7835ecadde6d7b6acea08207c8eaa7bfea54f519bb7269b6b2e6e165` |
| Saved result | SHA-256 `1d9ed0c7f0346191a3ed88ac906894564bdb10f95d927aa85440102d3c0f27d0` |

The fixture added stage 1 to Berlin province `549`, whose Prussian owner had
selected `no_slavery`, and stage 2 to Washington province `2146`, whose USA
owner had not. The saved result contained no managed stage on Berlin and
retained only `slave_trade_decadence_2` on Washington. No reconciliation
failure was logged. This establishes the final standalone package, separate
save namespace, current-owner policy branch, native removal, and policy-preserve
branch together. The immutable fresh source proves the fixture and runtime did
not alter the user's baseline save.

The earlier ordered-overlay runtime below remains supporting evidence for the
native service's bounded traversal counters and read-only postcondition probe.

## Prototype production reconciliation runtime

Launcher run `c479cc7c-47eb-4bdb-a133-0d8b9950d1c0` exercised the production
`gfm_adoption` plugin and public reconciliation service for three game days at
speed 5. The launch selected the original `mod/GFM.mod` first and the generated
one-file `mod/SmedleyGFMAdoption.mod` overlay second. The overlay removed only
GFM event `477877790`'s 15-line slave-trade reconciliation block; its 30-day
reschedule and all other GFM files remained active.

| Runtime input | Identity |
| --- | --- |
| Repository base plus uncommitted issue work | Base `e4befd2040961ff18968cceb74aca644ede27f57` |
| `v2game.exe` | SHA-256 `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| `smedley_kernel.dll` | SHA-256 `871ffc7efc03fcf498ffa8b02655a9cb792a2bcdd7898be726019110bb661243` |
| `campaign_runner.dll` | SHA-256 `a86058e05c043dd6502e68d2bd2da1922a3949c0ad44f258377ee09b30eb88fe` |
| `gfm_adoption.dll` | SHA-256 `9b40744b6c361da62e69bbd65e42c6a9f24b12d060e4aa94da9786e5bcd8765d` |
| Temporary read-only postcondition probe | SHA-256 `3f54511ebd068c85112464ab21b8db9006fc0309162271ec321409de6a4dc20d` |
| Source save before and after | SHA-256 `b032fbd3fa00c62994e7450c44699ca9fb56c50fe2fc67392df3d7ee8127d2ce` |

A control run against the same immutable fixture observed 3,987 readable
province slots, 807 active modifier nodes, stage 1 on Berlin province `549`,
and stage 2 on Washington province `2146`. Berlin's current owner selected the
resolved `no_slavery` option; Washington's owner did not.

The production run initialized the pinned plugin without a service-failure
log, completed through the verified native exit, and produced this read-only
postcondition on the third country-zero before-interest boundary:

| Observation | Result |
| --- | ---: |
| Readable province slots | 3,987 |
| Active modifier nodes | 806 |
| Berlin stage mask | `0` |
| Washington stage mask | `2` |
| Remaining managed-stage provinces | 1 |
| Active iteration guards | 0 |
| Invalid list or policy records | 0 |

The one-node decrease, Berlin's verified absence, and Washington's preserved
stage establish that the native remover, modifier identities, current-owner
resolution, and selected political-reform path work together at this callback
boundary. The temporary probe performed no mutation. Source-save identity
before and after establishes that all runtime mutation was confined to the
loaded disposable campaign.

The service still treats a nonzero province iteration guard as `DEFERRED`; this
path is covered by policy tests but was not forced in the live game. Host tests
also reject malformed previous links, tail pointers, and declared counts before
native removal. Broader `CProvince` and `CCountry` fields outside the narrow
reconciliation view remain provisional.
