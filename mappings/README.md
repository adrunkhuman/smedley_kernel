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
Historical runs establish only their recorded implementation, fixture, and
scope; moving code or passing host tests does not extend those claims.

## Evidence by boundary

| Boundary | Record |
| --- | --- |
| Save loading, observer, bounded runs, native exit | [Campaign automation](evidence/campaign-automation.md) |
| Native speed and daily pacing | [Speed control](evidence/speed-control.md) |
| Observation fields, hooks, units, capture validation | [Telemetry](evidence/telemetry.md) |
| Bank/state interest pipeline and POP payout | [Interest payout](evidence/interest-payout.md) |
| Taxes, loans, repayment, default, conservation | [Fiscal and credit](evidence/fiscal-credit-boundaries.md) |
| Province modifiers and checked removal | [Province reconciliation](evidence/province-reconciliation.md) |
| GFM static workload and runtime attribution limits | [GFM workload](evidence/gfm-workload.md) |
| Copied Lua events and queued pause | [Scripting](evidence/scripting.md) |

## Research

| Lead | Scope |
| --- | --- |
| [AI static leads](research/ai-static-leads.md) | Unverified findings recovered from the Player Military AI package. |
| [Retired engine mirrors](research/retired-engine-mirrors.md) | Historical model provenance; not supported C++ bindings. |

`tools/research/find_xrefs.py` finds direct x86 references to ASCII strings;
`tools/research/find_calls.py` finds direct relative calls to an RVA. Their
nearest-prologue guesses require manual disassembly review.

## Maintaining evidence

Organize records as **established claims → supporting evidence → limits**.
Keep identities, addresses, units, commands, counterexamples, and uncertainty
needed to reproduce or assess a claim. For runtime evidence, record the source
revision, fixture, configuration, result, and limitations; mark missing
provenance rather than guessing. Keep executable identity and reproducible code
references. Omit opaque run IDs, private-artifact hashes, and investigation
chronology. Keep comparable observations in a table and link shared evidence
instead of copying it.

Update conclusions when evidence changes. Remove superseded implementation
narration; Git retains it. Keep unresolved facts beside the relevant claim and
track proposed work in issues, not a parallel priority queue here.
