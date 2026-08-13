# Retired engine mirror provenance

Issue [#72](https://github.com/adrunkhuman/smedley_kernel/issues/72)
removed the generated and handwritten `v2`, `clausewitz`, and `std` C++ mirror
trees. They mixed unverified research candidates with active implementation and
were not a trustworthy mapping catalog or compatibility surface. This note
preserves the deleted research inputs and identifies the evidence that remains
authoritative.

## Archived inputs

The parent of the retirement commit retains every deleted file. These Git blob
IDs identify the exact historical inputs without keeping them in the build or
source include graph.

| Historical input | Git blob |
| --- | --- |
| `codegen/codegen/__init__.py` | `e15213be4b06734fefbce18a5b367b284456ec64` |
| `codegen/generated_outputs.txt` | `58a3eb0ee35f3a5b647c17b923dd1159c71535dc` |
| `codegen/models/clausewitz/functions/lua.toml` | `410806bb1343666398439b1107b8e80f1696890d` |
| `codegen/models/v2/classes/CCountry.toml` | `6cba0351615c4613503150a4974cd6166e9ba980` |
| `codegen/models/v2/classes/CCountryDataBase.toml` | `aaec2993c6cf8665f57959d561b73c2cf14362ae` |
| `codegen/models/v2/classes/CCultureDataBase.toml` | `b1a4af7827969d629c4a42b53cbac8e838e0269a` |
| `codegen/models/v2/classes/CCurrentGameState.toml` | `33ae7d76be7031787dc59d3b2936efe45c860e34` |
| `codegen/models/v2/classes/CTraitDefinitionArray.toml` | `5f23742ca8a615b77e547b3c5c82c0d91fbe0b4a` |

The country model also listed many guessed wrappers. Only the following native
boundaries remain active catalog entries or retained evidence:

| Candidate | RVA | Current evidence |
| --- | ---: | --- |
| `CCountry::Annex` | `0x00118620` | `mappings/evidence/campaign-automation.md` |
| `CCountry::DailyUpdate` | `0x00108590` | `mappings/evidence/interest-payout.md` |
| `CCountry::TakeLoan` | `0x00122910` | `mappings/evidence/fiscal-credit-boundaries.md` |
| `CCountry::RepayLoan` (`PayBackLoan` in the model) | `0x001238d0` | `mappings/evidence/fiscal-credit-boundaries.md` |
| `CCountry::PayDailyInterest` | `0x00123c30` | `mappings/evidence/interest-payout.md` |
| `CCountry::RemoveDebts` | `0x00111340` | `mappings/evidence/fiscal-credit-boundaries.md` |

Model-only wrappers, including `GetLuaState`, database singleton accessors,
`AddToSphere`, `MonthlyUpdate`, and `Westernize`, are not retained mappings.
Their names, offsets, signatures, calling conventions, and semantics are
historical-unverified and unsupported. The archived blobs are search history,
not permission to regenerate wrappers or call those candidates.

The kernel also formerly exported `luaL_loadstring`, `lua_pcall`, and
`lua_tolstring` through unchecked Victoria II import-table slots at RVAs
`0x0088a478`, `0x0088a4a8`, and `0x0088a548`. No internal consumer remained
after scripting moved to its private Lua 5.1.5 runtime, so the shims and bundled
Victoria II-facing Lua 5.1.4 headers were removed. Those RVAs are
historical-unverified and are not supported mappings or plugin compatibility
surfaces.

## Current game-state singleton

The retired `CCurrentGameState` model supplied preferred VA `0x012588e8`, which
corresponds to RVA `0x00e588e8`. The catalog retains it as `provisional`.
Kernel-owned readers resolve the pointer defensively; pointer readability does
not verify the surrounding object layout. Runtime observations for individual
fields are recorded separately below.

## CGameState layout

The whole layout remains `provisional`. Each field keeps its own evidence level;
evidence for one field does not promote adjacent fields.

| Field | Offset | Evidence | Retained source |
| --- | ---: | --- | --- |
| `country_ais` | `0x00a4` | `verified-runtime` for scheduler count and observer invariants | `mappings/evidence/campaign-automation.md`, `mappings/evidence/telemetry.md` |
| `provinces` | `0x0acc` | `provisional`; bounded vector reads with independently checked province records | `mappings/evidence/telemetry.md` |
| `countries` | `0x0adc` | `provisional`; bounded slot count and checked country records | `mappings/evidence/scripting.md`, `mappings/evidence/telemetry.md` |
| `player_nations` | `0x0aec` | `verified-runtime` for zero-human observer invariant | `mappings/evidence/campaign-automation.md` |
| `current_date` | `0x0b0c` | `provisional`; progression correlated in retained runs | `mappings/evidence/scripting.md`, `mappings/evidence/telemetry.md` |
| `idler` | `0x0b24` | `verified-runtime` with in-game RTTI and lifecycle transitions | `mappings/evidence/campaign-automation.md` |
| `speed_index` | `0x0b28` | `verified-runtime` with per-step readback | `mappings/evidence/speed-control.md` |
| `player_tag` | `0x0b5c` | `verified-runtime` as observer viewing perspective | `mappings/evidence/campaign-automation.md` |
| `world_market` | `0x0bcc` | `verified-runtime` for the checked market prefix only | `mappings/evidence/telemetry.md` |

## CCountry layout

The whole layout and fields not independently promoted remain `provisional`.

| Fields | Offsets | Evidence | Retained source |
| --- | --- | --- | --- |
| `expenses`, `incomes` | `0x00b0`, `0x00d0` | `provisional` accounting arrays | `mappings/evidence/fiscal-credit-boundaries.md` |
| `flags` | `0x01b0` | `verified-static-callsites` | `mappings/research/ai-static-leads.md` |
| `ai` | `0x0208` | `verified-runtime` for observer AI participation | `mappings/evidence/campaign-automation.md` |
| `states` | `0x0e44` | `verified-runtime` for bounded interest destination traversal | `mappings/evidence/interest-payout.md` |
| `treasury` | `0x0e78` | `verified-runtime` | `mappings/evidence/fiscal-credit-boundaries.md` |
| `field_0xe80_candidate` | `0x0e80` | unavailable; unidentified candidate | `mappings/evidence/fiscal-credit-boundaries.md` |
| `bank`, `creditors` | `0x0e88`, `0x0e8c` | `verified-runtime` for bounded credit and payout paths | `mappings/evidence/fiscal-credit-boundaries.md`, `mappings/evidence/interest-payout.md` |
| `stockpile` | `0x118c` | `provisional` checked telemetry candidate | `mappings/evidence/telemetry.md` |
| `tariffs` | `0x1440` | `verified-static-callsites` | `mappings/evidence/fiscal-credit-boundaries.md` |
| `tax_base` | `0x1560` | `provisional` checked telemetry candidate | `mappings/evidence/telemetry.md` |

## CProvince layout

The whole layout remains `provisional`; telemetry validates only the bounded
subsets it reads.

| Fields | Offsets | Evidence | Retained source |
| --- | --- | --- | --- |
| `owner`, `controller` | `0x0128`, `0x0130` | `provisional` checked country references | `mappings/evidence/telemetry.md` |
| `state` | `0x0188` | `verified-static-callsites` for production-state traversal | `mappings/evidence/telemetry.md` |
| `pops` | `0x0194` | `verified-runtime` as a vector of 16-byte POP-list records | `mappings/evidence/telemetry.md` |
| `num_pops` | `0x01a8` | `provisional` checked count candidate | `mappings/evidence/telemetry.md` |
| `rgo_employment_capacity` | `0x01ac` | `verified-runtime` | `mappings/evidence/telemetry.md` |
| `infrastructure` | `0x02b8` | `provisional` checked fixed-point candidate | `mappings/evidence/telemetry.md` |

## Retirement boundary

The live frontend and console consumers use narrow layouts under `game_state/src`
with compile-time x86 size and offset assertions. Those layouts are private
implementation records, not a replacement engine-object library. New access
requires a responsibility-specific reader or operation, exact executable
preflight, bounded memory checks, and retained evidence under `mappings/`.
