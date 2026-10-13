# Province reconciliation boundaries

The narrow province-modifier/current-owner policy boundary is
`verified-runtime` for lookup, bounded traversal, policy selection, and native
removal in the controlled fixtures below. The kernel service remains; the
GFM-specific native consumer and generated derivative from
[issue #91](https://github.com/adrunkhuman/smedley_kernel/issues/91) / PR #92
were retired. Pristine GFM retains its original cleanup block. Historical
package acceptance does not establish current package behavior.

See the [mapping catalog](../v2game-3.04.toml) for signatures and
[service boundary](../../docs/architecture.md) for current API ownership.

## GFM contract

Pinned GFM v3.0 commit `ee1486e251d3bb4dc5c355f5c2ec34abaea1078d`,
`events/V2ME Cleanup (GFM).txt:1127-1141`: country event `477877790` iterates
`any_land_province`, removing all four `slave_trade_decadence_1` through `_4`
when any stage is present and the current owner has `slavery = no_slavery`.
The event reschedules after 30 days at line 19020; this block has no randomness.
Separate RGO events create/advance permanent stages, and event `38506` removes
them while changing the RGO. The historical replacement disabled only the
15-line cleanup block, leaving those writers and independent remover active.

## Modifier lookup

RTTI identifies `CAddProvinceModifierEffect`, `CRemoveProvinceModifierEffect`,
and `CHasProvinceModifierTrigger`. Their parsers share registry pointer
preferred VA `0x0125a69c` and lookup RVA `0x001661b0`.

| Parser RVA | Retained definition | Static evidence |
| --- | --- | --- |
| Add `0x0048132d` | effect `+0x24` | Loads registry, passes parsed string to shared lookup, stores returned pointer. |
| Remove `0x00482170` | effect `+0x20` | Same registry/lookup. |
| Presence `0x004cdea0` | trigger `+0x1c` | Same registry/lookup. |

The lookup ABI is registry in `EAX`, one stack MSVC-string pointer, opaque
returned definition. Static paths are `verified-static-callsites`; runtime
resolution of the four keys is recorded below. Duplicate-key behavior and
lookup safety outside the exercised lifecycle remain unverified.

## Province modifier storage

Presence evaluator RVA `0x004cdfc0` resolves province from scope `+0x10`, walks
its `+0xc8` list, and compares record `+0x08` to trigger `this+0x1c`.
Query/add/remove paths establish this narrow layout:

| Object | Field | Meaning |
| --- | --- | --- |
| `CProvince` | `+0xc8` / `+0xcc` / `+0xd0` | Active-modifier head / tail / count |
| `CProvince` | `+0xd4` | Nonzero defers physical unlinking |
| List node | `+0x00` / `+0x04` / `+0x08` / `+0x0c` | Record / previous / next / deferred-removal marker |
| Active modifier | `+0x08` / `+0x0c` | Definition pointer / expiry-date value used by add path |

Complete active-modifier type, ownership, expiry encoding, and save-serialization
interaction are unverified; the runtime tests validate only this borrowed view.

## Modifier removal

`CRemoveProvinceModifierEffect::Execute`, RVA `0x00482290`, is reached through
virtual slot `+0x2c`. It resolves scope province `+0x10`, loads definition
`this+0x20`, and calls RVA `0x000b39a0` with borrowed province in `ESI` and one
stack definition pointer. The remover returns `ret 4`, with no success result.

It compares record `+0x08`, marks node `+0x0c` when province `+0xd4` is nonzero,
otherwise repairs previous/next/head/tail links, frees the node, and decrements
`+0xd0`. Traversal continues to remove duplicate matches.
This is `verified-static-callsites`; controlled immediate removal/list
postconditions are `verified-runtime` below. It is not an arbitrary daily-callback
mutation contract. The service supplies its own synchronous game-thread authority;
a nonzero guard returns `DEFERRED` without mutation, leaving consumer cadence
pending. Deferred unlink/drain behavior was not forced in-game.

## Political reform selection

| Path | `verified-static-callsites` contract |
| --- | --- |
| `CIssueGroupTrigger` parser, RVA `0x004c4a70` | Registry preferred VA `0x01258a48`, lookup RVA `0x00575ed0`, parsed MSVC string → option. Accept only option `+0x54 == this+0x20` group; retain at `this+0x24`. |
| Evaluator, RVA `0x004c4da0` | Group `this+0x20` supplies index `+0x50`, category `+0x5c`. Province owner key/index `+0x128` / `+0x12c` resolves country through database pointer preferred VA `0x012587e4`, array `+0x4`. Category 2 indexes the selected-option array at `CCountry+0x860` and compares with `this+0x24`. |
| `CSetPoliticalReformEffect::Execute`, RVA `0x004950b0` | Independently writes `CCountry+0x860` selection using target option's group `+0x54` and group index `+0x50`. |

Pinned GFM defines political issue `slavery` and option `no_slavery`. Runtime
lookup, bounded array indexing, and current-owner selection are correlated below;
live reform transitions are not.

## Read-only runtime correlation

One-day run on base `e4befd2040961ff18968cceb74aca644ede27f57`, supported
executable SHA-256 `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648`,
and unchanged source save SHA-256
`3c70553984e4d94773483076ee4d83ac351095042f66a9920a3ee4770f7763f4`:
both lookup signatures accepted, foreign vectors/lists bounded, no engine writes.

| Observation | Result / limit |
| --- | --- |
| Four slave-trade keys | Distinct non-null definitions |
| `no_slavery` | Non-null option; owning group index 1, category 2; selected by 311 of 653 country slots |
| Province/list traversal | 3,987 readable slots, 805 nodes; all declared counts agreed; zero guards or invalid records |
| Target presence | No target definitions active: cannot validate their presence or removal from this save |
| Lifecycle | Empty GFM error log, verified native exit |

This corroborates exercised registry lifetime, policy selection, and generic
traversal, not mutation authorization by itself.

## Historical standalone reconciliation runtime

The retired standalone fixture selected only `mod/GFMSmedley.mod` and the
required `gfm-smedley` consumer (`gfm_adoption` before rename). It reached
checksum `BBSV` and accepted simplified event SHA-256
`34709cb3ae8c40cba441a05c6cba57952e84d5c6a234464879e14254dc1546c0`.
These package names identify historical test inputs, not installation advice;
current launch/preflight behavior is in [launcher](../../docs/launcher.md).

A disposable fixture from a fresh GFM-smedley Prussia campaign added stage 1 to Berlin province `549`
(owner selected `no_slavery`) and stage 2 to Washington `2146` (USA owner had
not). `campaign_runner` advanced at speed 5 from `1836.1.1` to `1836.1.4` and
saved. Berlin had no managed stage in the result; Washington retained only
`slave_trade_decadence_2`. No reconciliation failure was logged. Post-review
final-kernel and rename replays reached the same target and requested native
exit; the rename replay left input unchanged.

| Runtime input | Identity |
| --- | --- |
| Repository base plus issue work | `e4befd2040961ff18968cceb74aca644ede27f57` |
| Rename replay source | Merged base `479aef63fe3f9f3e982559d3586d6e958a3c639b` plus rename work |
| `v2game.exe` | SHA-256 `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| Rename replay kernel | SHA-256 `ffee3cccf2837a53f59e57ce27e3c2a2ed5b7c50ec3cfea84737e9cf61f7f70e` |
| Pre-rename replay kernel | SHA-256 `d88cec1f13bbd1d89b4903d90aa1ac21b572fb991f0338d0669824e20ccbb770` |
| Saved-result kernel | SHA-256 `11df3cb1afc188d289a2915e7dd506c862c978d3b43a41b604568772a29fdaf4` |
| `gfm-smedley.dll` | SHA-256 `1ff4b2a8105aa95a6d430090cc1d9851488b85a13746fa3897d2668420aeddf1` |
| Pre-rename `gfm_adoption.dll` | SHA-256 `96083bcd46bfc3c0e31c73d477d73e497af11b6ae17077dc58edfb7c9eb815ab` |
| `campaign_runner.dll` | SHA-256 `a86058e05c043dd6502e68d2bd2da1922a3949c0ad44f258377ee09b30eb88fe` |
| Fresh source before/after | SHA-256 `b32dc06746be5a77512081d778a6cc7b4d060c9f28e77cd6f0939153c33de1ac` |
| Disposable input | SHA-256 `0d1c453c7835ecadde6d7b6acea08207c8eaa7bfea54f519bb7269b6b2e6e165` |
| Saved result | SHA-256 `1d9ed0c7f0346191a3ed88ac906894564bdb10f95d927aa85440102d3c0f27d0` |

This verifies current-owner policy removal/preservation and native removal in
that implementation. The immutable fresh source shows fixture/runtime work did
not alter the baseline save; it does not prove an exact clean-commit build or
newer implementation acceptance.

## Supporting traversal evidence

An earlier ordered-overlay run of the same service supplied counters not
recoverable from the standalone saved result:

| Observation | Result |
| --- | --- |
| Readable province slots / active nodes | 3,987 / 806 |
| Berlin / Washington stage masks | `0` / `2` |
| Remaining managed-stage provinces | 1 |
| Active guards / invalid list or policy records | 0 / 0 |
| Immutable source SHA-256 | `b032fbd3fa00c62994e7450c44699ca9fb56c50fe2fc67392df3d7ee8127d2ce` |

The one-node decrease, Berlin absence, Washington preservation, and list
integrity corroborate modifier identity, current-owner resolution, political
selection, and removal at this callback boundary.

## Limits

Live evidence covers immediate removal with guard zero, not forced deferred
drain, duplicate removal, arbitrary threads/phases, or live reform transitions.
Host policy tests cover `DEFERRED`; malformed previous links, tails, and counts
are rejected before removal. Those tests do not extend runtime coverage.
Broader province/country layouts remain provisional.
