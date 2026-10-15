# Mapping catalog

Reverse-engineered facts about the supported English Heart of Darkness 3.04
executable. [User and developer guides](../README.md#documentation) describe
current Smedley behavior; this catalog records why engine claims are trusted
and where they remain uncertain.

## Catalogs

| File | Contents |
| --- | --- |
| [v2game-3.04.toml](v2game-3.04.toml) | Executable identity, native symbols, signatures, layouts, and provenance. |
| [gui-controls-3.04.toml](gui-controls-3.04.toml) | Names from shipped `.gui` definitions and reviewed executable references. Names alone do not identify native handlers. |

Validate the executable and instruction signatures before using an address:

```powershell
python tools/validate_mappings.py "C:\path\to\Victoria 2\v2game.exe"
```

A byte match proves code is present—not its name, type, calling convention,
object lifetime, thread requirements, or safe mutation boundary. RVA addresses
are module-relative; VAs use preferred base `0x00400000`. Runtime code must
account for ASLR.

## Evidence levels

| Level | Meaning |
| --- | --- |
| `verified-runtime` | Observed in the supported live game and independently correlated. |
| `verified-current` | Byte-matched to the cataloged executable and used successfully by current source. |
| `verified-static-callsites` | Reviewed callers support the semantics; runtime coverage is incomplete. |
| `provisional` | Hypothesis; must not drive default mutation. |
| `historical-unverified` | Recovered from history; requires current validation. |
| `historical-skeleton` | Search lead, not a usable layout contract. |
| `definition-verified` | GUI name exists in shipped definitions. |
| `verified-static-xref` | GUI name has reviewed executable references. |

Evidence levels apply to specific claims, not entire objects or features.
An observed field or path does not establish adjacent layout, arbitrary-input
behavior, or acceptance of a different implementation.

## Evidence by boundary

| Boundary | Record |
| --- | --- |
| Save loading, observer, bounded runs, native exit | [Campaign automation](evidence/campaign-automation.md) |
| Native speed and daily pacing | [Speed control](evidence/speed-control.md) |
| Observation fields, hooks, units, and accounting limits | [Telemetry](evidence/telemetry.md) |
| Bank/state interest pipeline and POP payout | [Interest payout](evidence/interest-payout.md) |
| Taxes, loans, repayment, default, conservation | [Fiscal and credit](evidence/fiscal-credit-boundaries.md) |
| Province modifiers and checked removal | [Province reconciliation](evidence/province-reconciliation.md) |
| GFM static workload and runtime attribution limits | [GFM workload](evidence/gfm-workload.md) |
| Copied Lua events and queued pause | [Scripting](evidence/scripting.md) |

## Research

| Lead | Scope |
| --- | --- |
| [Maxioten research import](research/maxioten-import.md) | Attributed engine, script, AI, technology and economy findings, selected mapping anchors and explicit conflicts. Research only, not runtime bindings. |
| [AI static leads](research/ai-static-leads.md) | Unverified findings recovered from the Player Military AI package. |
| [Retired engine mirrors](research/retired-engine-mirrors.md) | Historical model provenance; not supported C++ bindings. |

`tools/research/find_xrefs.py` finds direct x86 references to ASCII strings;
`tools/research/find_calls.py` finds direct relative calls to an RVA. Their
nearest-prologue guesses require manual disassembly review.

## Maintaining evidence

Organize records as **contract → mechanism → limits**. State what a field or
function means, how values are calculated or transferred, and where the claim
stops. Keep executable identity, primary instruction/callsite addresses, units,
and source references needed to inspect the mechanism.

Do not narrate test execution or retain fixture names, run inventories, capture
bookkeeping, opaque IDs, or private-artifact hashes. Include a numerical example
or measurement only when it explains a rule, exception, or consequential cost;
state the conditions needed to interpret it. Keep unknowns beside the relevant
claim and link shared facts instead of copying them.

Update conclusions when evidence changes. Remove superseded implementation
narration; Git retains it. Keep unresolved facts beside the relevant claim and
track proposed work in issues, not a parallel priority queue here.
