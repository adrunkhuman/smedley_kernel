# Province reconciliation boundaries

Modifier lookup, bounded province/list traversal, current-owner policy
selection, and immediate native removal have narrow `verified-runtime`
correlations on the supported executable. They do not establish arbitrary
province mutation. The kernel service remains; the GFM-specific native consumer
and derivative package are retired. Pristine GFM retains its cleanup block.

See the [mapping catalog](../v2game-3.04.toml) for signatures and
[service boundary](../../docs/architecture.md) for current API ownership.
Addresses below are RVAs unless marked as preferred VAs or object offsets.

## GFM contract

Pinned GFM v3.0 commit `ee1486e251d3bb4dc5c355f5c2ec34abaea1078d`,
`events/V2ME Cleanup (GFM).txt:1127-1141`: country event `477877790` iterates
`any_land_province`, removing all four `slave_trade_decadence_1` through `_4`
when any stage is present and the current owner has `slavery = no_slavery`.
The event reschedules after 30 days at line 19020; this block has no randomness.
Separate RGO events create/advance permanent stages, and event `38506` removes
them while changing the RGO. Replacing this cleanup does not replace those
writers or the independent remover.

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
returned definition. Parser paths are `verified-static-callsites`; resolution
of the four pinned slave-trade keys to distinct non-null definitions is
`verified-runtime` during an active GFM campaign. Duplicate-key behavior and
lookup safety outside that lifecycle remain unverified.

## Province modifier storage

Presence evaluator RVA `0x004cdfc0` resolves province from scope `+0x10`, walks
its `+0xc8` list, and compares record `+0x08` to trigger `this+0x1c`.
Query/add/remove paths establish this narrow borrowed layout:

| Object | Field | Meaning |
| --- | --- | --- |
| `CProvince` | `+0xc8` / `+0xcc` / `+0xd0` | Active-modifier head / tail / count |
| `CProvince` | `+0xd4` | Nonzero defers physical unlinking |
| List node | `+0x00` / `+0x04` / `+0x08` / `+0x0c` | Record / previous / next / deferred-removal marker |
| Active modifier | `+0x08` / `+0x0c` | Definition pointer / expiry-date value used by add path |

Complete active-modifier type, ownership, expiry encoding, and save-serialization
interaction are unverified. Readability alone does not validate a list: previous
links, tail, and declared count must agree before removal.

## Modifier removal

`CRemoveProvinceModifierEffect::Execute`, RVA `0x00482290`, is reached through
virtual slot `+0x2c`. It resolves scope province `+0x10`, loads definition
`this+0x20`, and calls RVA `0x000b39a0` with borrowed province in `ESI` and one
stack definition pointer. The remover returns `ret 4`, with no success result.

It compares record `+0x08`. When province `+0xd4` is nonzero, it marks node
`+0x0c` rather than unlinking it. Otherwise it repairs previous/next/head/tail
links, frees the node, and decrements `+0xd0`. Traversal continues to remove
duplicate matches. These branches are `verified-static-callsites`; immediate
removal with guard zero and list postconditions are `verified-runtime` only at
the historical synchronous reconciliation boundary.

The service supplies its own synchronous game-thread authority. It returns
`DEFERRED` without mutation when the guard is nonzero, leaving reconciliation
pending. No native drain operation is mapped, and deferred unlink/drain was not
observed in-game. Marking a node is not proof of physical removal. This is not
an arbitrary daily-callback mutation contract.

## Political reform selection

| Path | `verified-static-callsites` contract |
| --- | --- |
| `CIssueGroupTrigger` parser, RVA `0x004c4a70` | Registry preferred VA `0x01258a48`, lookup RVA `0x00575ed0`, parsed MSVC string → option. Accept only option `+0x54 == this+0x20` group; retain at `this+0x24`. |
| Evaluator, RVA `0x004c4da0` | Group `this+0x20` supplies index `+0x50`, category `+0x5c`. Province owner key/index `+0x128` / `+0x12c` resolves country through database pointer preferred VA `0x012587e4`, array `+0x4`. Category 2 indexes the selected-option array pointed to by `CCountry+0x860` and compares with `this+0x24`. |
| `CSetPoliticalReformEffect::Execute`, RVA `0x004950b0` | Independently writes the selection array pointed to by `CCountry+0x860` using target option's group `+0x54` and group index `+0x50`. |

Pinned GFM defines political issue `slavery` and option `no_slavery`. Runtime
lookup correlated that option with group index 1, category 2; bounded indexing
correlated current-owner selection. With guard zero, a stage was removed from a
province whose owner selected `no_slavery` and preserved for an owner who did
not. This supports the owner-dependent cleanup rule, not live reform-transition
observation or authorization to write the reform array.

## Limits

Read-only correlations use the pinned GFM/PRU input described in
[GFM workload inputs](gfm-workload.md#immutable-inputs). That unmodified save
had no active slave-trade definitions, so it establishes registry/policy
selection and generic traversal, not target presence or removal.

Immediate-removal evidence comes from the historical reconciliation
implementation based on `e4befd2040961ff18968cceb74aca644ede27f57` with local
changes and only the pinned 15-line cleanup block disabled. It does not record
an exact clean-commit build or establish current package behavior.

Live evidence does not cover forced deferred drain, duplicate removal,
arbitrary threads/phases, or live reform transitions. Host validation of
`DEFERRED` and malformed lists does not extend runtime coverage. Broader
province/country layouts remain provisional.
