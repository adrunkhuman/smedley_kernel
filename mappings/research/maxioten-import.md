# Maxioten research import

These records summarize semantic findings from
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering),
pinned to
[`e299a0214d32274239402e8ee22039e1d02e101f`](https://github.com/maxioten/Victoria2-Reverse-Engineering/tree/e299a0214d32274239402e8ee22039e1d02e101f).
They preserve mechanisms, selected address/field anchors and disagreements with
Smedley's evidence—not a duplicate disassembler inventory.

All newly imported semantics remain `historical-unverified`. Existing Smedley
evidence keeps its claim-specific level and limits. No engine access, runtime
signatures, hooks, patches or public services are added.

## Read by subsystem

| Record | Knowledge retained |
| --- | --- |
| [Engine scheduling](engine-scheduling.md) | Frame/command/day boundaries, both country passes, deferred AI queue, ENet waits, pacing, RNG, threading, loading, checksum and performance leads. |
| [Script evaluation](script-evaluation.md) | Scopes, triggers, effect parsing, events and MTTH, decisions and queued execution, CB checks, flag textures and native UI leads. |
| [AI and technology](ai-and-technology.md) | Minister registry, daily/frame jobs, military movement, budget, diplomacy/crisis/sphere, national focus, research selection and separate UI/AI availability gates. |
| [Economy leads](economy-leads.md) | Daily versus load-time market processing, goods pools, prices, factory construction, employee needs, POP project investment and layout disagreements. |

Together these cover the mechanisms in all 32 subsystem Markdown records in
`Engine/`, `AI/`, `Economy/` and `Technology/`, plus the four Cheat Engine text
records. Each reference links immutable upstream sources. Raw symbol listings,
function-entry bytes and complete vtable listings remain upstream; they are
not maintained as a second local catalog. Source confidence labels and byte
matches do not establish semantic correctness, safe native calls or thread
safety.

## Material disagreements

| Upstream claim | Smedley interpretation / limit |
| --- | --- |
| World `+0xb3c` is special modifiers | [Wars-container candidate](../evidence/telemetry.md#country-and-province-candidates), not a verified full war layout. |
| Country `+0xe4c` is an AI score | [State-list count](../evidence/interest-payout.md#verified-traversal-and-mutation-boundaries). |
| Global VA `0x01258a48` is a unit manager | [Issue-option registry](../evidence/province-reconciliation.md#political-reform-selection). |
| VA `0x00523400` calculates interest | [Loan origination](../evidence/fiscal-credit-boundaries.md#origination), with principal and treasury effects. |
| Fastest pacing threshold is `0.0004` | Local [daily-pacing evidence](../evidence/speed-control.md#daily-pacing) identifies `0.0001`. |
| Daily callback means completed country/day work | Hook RVA `0x001085ae` is in the [earlier country pass](engine-scheduling.md#existing-dailyupdateevent-observes-the-earlier-pass), before later update VA `0x005091a0`. |
| Economy runs before the later country pass | The [daily call sequence](engine-scheduling.md#a-day-is-scheduled-across-frames-not-one-monolithic-update) places economy VA `0x00489990` after that pass. |
| POP stride `0x2a8`; links treated as object fields | [Payload `0x278`, node allocation `0x288`](../evidence/interest-payout.md#verified-traversal-and-mutation-boundaries); linked allocation is not a contiguous-array stride. |
| Factory leftover at `+0x200` | [Closing unsold output at `+0x1f8`](../evidence/telemetry.md#finance-inventory-and-merges); other offsets agree, so do not shift the entire layout. |
| “SFX lag” patch is an audio optimization | Its [target is network `select`](engine-scheduling.md#session-polling-can-block-even-in-single-player). No patch is imported. |
| Trigger write scan proves parallel safety | It excludes indirect writes and some calls; [thread safety remains unproven](script-evaluation.md#why-the-write-scan-does-not-prove-thread-safety). |

Country `+0xe80` remains unidentified. Source performance numbers retain their
external workload attribution; they are neither Smedley benchmarks nor promised
speedups. Additional source-internal disagreements are kept beside the relevant
mechanism in the subsystem records.

## Attribution and reuse boundary

The pinned upstream tree supplies no license file. These are independently
written summaries of attributed factual findings, not copies of upstream prose,
code, reconstructed function bodies, diagrams or patch implementations. No
permission to reuse upstream implementation or any rights in the game is
asserted.

Game assets, saves, dumps, profile artifacts, Cheat Engine `.CT` payloads,
upstream tools and gameplay experiments are not imported. Their implementation
reuse or behavioral changes are outside this knowledge transfer.

Promotion into the primary mapping catalog requires the usual
[claim-specific static and runtime evidence](../README.md#evidence-levels).
These research records are not authority for native calls or default mutation.
