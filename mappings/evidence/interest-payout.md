# Native interest pipeline and POP payout evidence

The supported executable charges borrowers, credits named creditor banks, and
allocates bank interest to state pools. **No state-to-POP consumer was identified
in the reviewed static paths or sampled native cash-flow phases**; this does not
exclude later, indirect, or transient payouts. The opt-in `interest_bug_fix`
supplies that last stage, consuming native state pools rather than bypassing native bank
policy. See the [user guide](../../docs/interest-fix.md) for current operation.

This document records mapping claims, counterexamples, and historical runtime
support. It is not acceptance evidence for the current binary: the century and
performance runs below predate the C-service port (`2f38c6b`). Revisions not
recorded by the original evidence are marked unrecorded, not inferred from
later commits.

## Target and provenance

All addresses are **RVAs**, unless explicitly labelled VA, for the supplied
English Victoria II: Heart of Darkness 3.04 x86 executable: SHA-256
`62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648`, file size
12,294,656 bytes, preferred base `0x00400000`, image size `0x01092000`.
Resolve against the loaded module; a signature match alone does not prove
semantics, ownership, calling convention, or safe mutation.

Expected bytes and field provenance live in the
[catalog](../v2game-3.04.toml); evidence levels are defined in
[the mapping guide](../README.md). Reproduction/triage commands from the repository
root (these are not a claim that each historical run used these exact commands):

```powershell
python tools/validate_mappings.py "C:\path\to\Victoria 2\v2game.exe"
python tools/research/find_calls.py "C:\path\to\Victoria 2\v2game.exe" --target-rva 0x00123c30
python tools/research/find_calls.py "C:\path\to\Victoria 2\v2game.exe" --target-rva 0x000f5bf0
python tools/research/find_calls.py "C:\path\to\Victoria 2\v2game.exe" --target-rva 0x0055a5f0
```

Direct-call scans are triage, not complete cross-reference analysis; manually
review instructions and callers. The complete direct-displacement state-pool
scan discussed below has no retained invocation in the original notes.
Historical `794c98e` was only an investigation lead: its generated 469,301-line
layout, provisional writes, and floating-point allocation without conservation
tests do not establish a supported fix. Retired `interest_probe` and
`pop_money_fixture` sources/manifests are no longer available in the worktree;
their historical results do not provide currently runnable probes.

The community [Loans wiki, permanent revision 22984](https://vic2.paradoxwikis.com/index.php?title=Loans&oldid=22984)
describes POP deposits, domestic-before-foreign borrowing, domestic-last
repayment, daily debt-times-effective-rate interest divided by 30, and omitted
POP interest. It also notes that the omission may be an intentional money sink.
This motivates savings-weighted payout and opt-in behavior; it is not proof of
engine internals or a measured world-money effect. Preserve the engine's exact
integer calculation and distinct domestic/foreign modifiers.

## Established pipeline and units

### Borrower → named bank

`CCountry::PayDailyInterest` reads creditor entries through country `+0xe8c`,
multiplies debt `+0x18` by interest `+0x10`, and updates paid byte `+0x20`.
Destination tag/ordinal are creditor `+0x08/+0x0c`. A nonzero ordinal resolves a
country and credits its bank pending interest `+0x20`; VA `0x00524022` skips
this bank credit for ordinal zero.

| Claim | Evidence level and basis |
| --- | --- |
| Domestic debt is a named self-creditor | `verified-static-callsites`: `TakeLoan` RVA `0x00122910` passes the debtor's own country tag/ordinal (`+0x1c/+0x20`) through `CanTakeLoanFrom` and `TakeLoanFrom`; it uses the same nonzero-ordinal credit path as foreign debt. |
| Ordinal-zero interest has no depositor destination or bank pending credit | `verified-runtime` path plus static creation: fallback writes literal `---`, ordinal zero at VA `0x00522c24`–`0x00522c27`, then calls `TakeLoanFrom` at VA `0x00522c6a`. A diagnostic captured 22 entries with key `0x2d2d2d`, ordinal zero, paid byte one. Localisation `SHADOWY_INVESTOR` displays “Private Investors”; vanilla `SHADOWY_FINANCIERS_MAX_LOAN_AMOUNT` is 1500. Leave this flow untouched/unallocated. |
| Creditor rate/principal units | `verified-runtime`: SWE serialized interest `0.01999` → raw `655`, debt `14969.11719` → raw `490508032`. The repayment fixture bracketed 12 exact requested/treasury/principal reductions with retained identities. |
| Bank money/lent/pending fields are distinct | `verified-runtime`: bank `+0x10` is serialized `money`, `+0x18` is `money_lent`, `+0x20` is temporary interest, not bank cash. Repayment reduces lender `money_lent` only for resolved nonzero ordinals and clamps at zero; reviewed ordinal-zero repayment skips that reduction. New and existing ordinal-zero loan origination differ; see [fiscal evidence](fiscal-credit-boundaries.md#origination). |
| Immediate named transfer | `verified-runtime`: 12 calls and 40 individual positive destination deltas establish the bank credit; see the read-only run table. Exact debtor/bank conservation held in those fixtures, not universally. |

**Counterexample: treasury delta is not a universal conservation signal.** The
insufficient-funds helper prepares debtor, zero, and the requested 64-bit amount
before `TakeLoan` at VA `0x005257a8`. Its shortfall/default path can refund canceled
construction amounts inside `PayDailyInterest`. Net debtor treasury movement
therefore includes unrelated changes and must not be labelled Private Investor
interest or used to infer named-transfer conservation.

### Named bank → state pool

`CBank::DistributeInterest` (RVA `0x000f5bf0`) has one direct caller, the
post-country bank loop at RVA `0x00285f0f` (VA `0x00685f0f`). It loads each
country's `+0xe88` bank after country daily updates. Owner is bank `+0x08`;
pending amount is signed 64-bit `+0x20/+0x24`, and reserve/denominator is
`+0x10/+0x14`.

For positive pending interest, native code:

1. Adds it to bank `+0x10` if that field is at most the integer conversion of
   `33.268` (33).
2. Otherwise walks country states and weights the **original** pending amount
   by state savings `+0x258`, clears the low 15 product bits, divides by the bank
   denominator, and adds the share to state interest `+0x260`. Signed 64-bit
   helpers truncate division toward zero.
3. Clears bank pending interest on every return path.

The local remainder starts as the pending amount. An ordinary share is added and
subtracted from it; an oversized share is clamped to the complete remainder,
but that branch **does not zero the remainder before continuing**. A positive
final remainder is discarded. Thus truncation can lose residual interest, and a
denominator smaller than traversed savings can cause an early clamp and
potential over-allocation. This is a static constraint, not a claim that the
runtime fixtures triggered over-allocation. Do not reproduce this formula or
assume that state increments sum to the cleared bank amount.

**Counterexample: bank clearing is not state-pool conservation.** Five complete
cross-date brackets cleared 71,785 raw pending-bank units but added only 8,555
state units. Recapitalization and discarded remainder are separate native reasons
why state pools need not receive all charged interest; this bracket alone does not
identify the contribution of each. The superseded direct-creditor fix bypassed
this policy; its conservation tests do not validate the current state-pool design.

### State pool → POP: missing native consumer and verified scale

Save parsing/formatting names adjacent state fields `savings` (`+0x258`) and
`interest` (`+0x260`). A complete direct-displacement scan found construction,
copying, parsing, formatting, and the bank writer, but no gameplay subtraction,
clear, or POP payout consumer. An aliased indirect access cannot be ruled out
by that negative scan alone. The seven-day no-fix cash-flow capture corroborated
the missing consumer: accumulating state interest, 12,805 component records
(including 1,135 bank records), and **zero interest-index-7 records**.

POP savings `+0x250` are signed 64-bit values at 1,000 times state money scale.
The conversion uses global RVA `0x00b0b168`, raw `32768000` (`1000.0` with 15
fractional bits). Live summed per-POP truncated `/1000` savings tracked state
`+0x258` within 19–108 units in the detailed scale run (19–116 across the broader
12-sample evidence). First SWE sample: raw POP savings `1,909,522,541`, per-POP
truncated sum `1,909,394`, stored state savings `1,909,375`.

**Counterexample: recomputed POP savings do not exactly equal stored state
savings.** This rounding/bookkeeping drift forbids treating the two as
interchangeable. Preserve native state allocation; for payout of an already
created state pool, normalize against summed positive POP savings instead.

## Verified traversal and mutation boundaries

These are narrow verified fields, not a fully verified general engine layout.
The catalog's broader state object remains provisional.

| Boundary | Level | Basis / limits |
| --- | --- | --- |
| `DailyUpdate` RVA `0x00108590`; interest call `0x00108d3e` | `verified-current` function, `verified-runtime` boundary | Sole direct `PayDailyInterest` call, country pointer already on stack. Checked trampoline preserves registers/flags, invokes original (`ret 4`) once, resumes at `0x00108d43`; 1,897 ordered pairs and empty-registry run reached exact targets. |
| Post-country bank call `0x00285f0f` | `verified-static-callsites` catalog boundary; historical runtime use below | Complete five-byte `e8 dc fc e6 ff`, bank in EDI, continuation `0x00285f14`. Current trampoline preserves registers/flags, emits BEFORE only for loop index zero, invokes original once, emits AFTER only for positive pre-call pending interest, and copies whether reserve selected state distribution rather than recapitalization. |
| Country states `+0xe44/+0xe48/+0xe4c` | `verified-current` | Head/tail/count maintained by creation callers; node data `+0`, next `+8`, null termination. Seven-day probe matched all 597 reported/walked states. |
| State size and pool slots | `verified-static-callsites` constructor; `verified-runtime` pool/savings fields | Constructor **RVA `0x000cdc60`**, VA `0x004cdc60`; three callers allocate `0x290`, initialize `+0x258/+0x260` to zero. Earlier notes incorrectly treated the VA as an RVA. |
| State provinces `+0x48` | `verified-runtime` | Four-byte game-state province indices; 346–661 destination provinces per creditor sample all resolved. |
| Province POP vectors `+0x194/+0x198` | `verified-runtime` | 16-byte list records, exactly 13 per province in this fixture; list count/tail and linked POP walks agreed. This does not establish 13 as a universal limit. |
| POP payload/node/next/savings | `verified-static-callsites` extents; `verified-runtime` traversal | Payload/value stride `0x278`, node allocation `0x288`, next `+0x27c`, savings `+0x250`; 2,635–5,565 linked POPs per creditor sample, no duplicates. See extent proof below. |
| `CPop::GiveMoney` RVA `0x0055a5f0` | `verified-runtime` | Native body and reviewed direct callers establish EAX=POP, ESI=cash-flow type, stack 64-bit amount, `ret 8`; reversible run below verifies narrow effects. The complete direct-call scan has 15 callsites selecting indices 0–6, none 7; indirect/inlined consumers remain outside that scan. |
| Interest cash-flow index 7 | `verified-static-callsites`, reversible slot effect `verified-runtime` | Presentation RVA `0x00566440` reads `+0x210`, index 7 of `+0x1d8`, formats `POP_DAILY_INTEREST`. Fixture changed/restored slot 7; it did not exercise presentation or snapshot every other slot. |

**POP extent (`verified-static-callsites`).** Value-copy helper VA `0x004c0af0`
advances source/destination by `0x278` at VAs `0x004c0b38/0x004c0b45`; constructors
initialize/copy through dword `+0x274`. List creation allocates `0x288` at VAs
`0x004b8d8e/0x004be161` and passes the unchanged base to copy constructor VA
`0x00955450`. Previous/next/byte marker are outside the payload at
`+0x278/+0x27c/+0x280`. This is separately allocated linked storage, not a
contiguous `0x288` POP stride. Marker lifecycle and trailing padding are unclaimed.

The retired read-only probe checked readable spans before snapshots, capped
512 states and 1,024 province indices/state, 64 creditor destinations, 4,096
province resolutions, 128 lists/province, 100,000 POP reads/callback, and 512
country slots for global scans. It retained copied POD only until the matching
AFTER phase, atomically queued a fixed pair through 1,024 slots, and performed
CSV I/O on a worker. `flags=0` meant structural checks passed, not economic
proof; any nonzero flag invalidated semantic promotion. Contained subscriber
exceptions incremented a kernel counter and flagged subsequent rows.

Current collection remains bounded (512 states/country, 1,024 indices/state,
4,096 province references, 128 lists/province, 100,000 POPs; cleanup additionally
caps the complete campaign at 4,096 states); it rejects malformed
containers, count/tail disagreement, duplicate province/state/POP identities,
and incomplete traversal. It scans states first and walks POPs only in positive
pools. Cleanup preflights the complete bounded campaign state traversal and
writable pool spans before clearing; failed clear/readback after earlier clears
is reported as partial mutation and does not enable payouts. The C ABI exposes copied records and
opaque callback-scoped handles, never engine pointers. Exact tables/authority
contracts belong in [interest_pool_api.h](../../include/smedley/interest_pool_api.h),
[event_services_api.h](../../include/smedley/event_services_api.h), and the
[plugin guide](../../docs/plugins.md), not a duplicated API here.

### Allocation and write constraints

- Convert each complete positive native state pool to POP units at `1000:1`.
  Only positive stored savings participate. Floor proportional payouts, then
  award remaining single raw units to largest fractional remainders; ties use
  stable province/list/POP traversal order. No floating-point arithmetic or RNG.
- Prepare every payout for that state before its first native call, and require
  the exact sum to equal `state_pool_raw * 1000`. Reject no eligible savings,
  invalid/insufficient scratch, unrepresentable savings sums, scaled totals, or
  arithmetic results. Current integer multiply/divide handles an oversized
  intermediate product; the old blanket product-overflow rejection is obsolete.
- Validate executable/signatures, game session, owning thread, callback phase,
  country/state membership, unchanged pool, writable spans, and POP arithmetic
  before writes. Invoke native `GiveMoney` with index 7 in deterministic order;
  verify exact money `+0x180`, slot 7 `+0x210`, total `+0x218`, and unchanged
  savings `+0x250`. Clear the unchanged pool only after all postconditions pass.
- Multi-POP native calls are not atomic. Pre-mutation failure retains the pool
  for that pass; partial mutation/postcondition failure or unavailable mutation
  disables later payouts in the pass. Never retry the complete retained pool
  after partial payment: that could double-pay. Cleanup must succeed before
  re-enabling; completed writes are not rolled back.
- Current C-service callback has no date/tag/session marker. It discards pools
  at **every country-zero BEFORE**, not once per session. This removes serialized
  orphan interest and retained failures before the next native pass. Historical
  once-per-session optimization and its throughput are not current behavior.
  A callback exception disables registration; it is not next-day recovery.
- Duplicate identity scope is **one bank callback**, not one day. A later bank
  can create a new pool for the same state/depositor after an earlier payout.
  The initial day-scoped guard wrongly rejected 5,496 valid second-bank payouts
  in a century diagnostic run; `c357fd9` corrected it. Duplicates across states
  within a callback remain invalid.

The current plugin resolves only versioned event, interest-pool, and logging C
services and exports `SmedleyPluginGetApiV1`. Preallocated buffers and a bounded
result queue separate synchronous engine work from worker logging/CSV/telemetry;
authority handles are scrubbed before callback return. See
[current source](../../plugins/interest_bug_fix/src/interest_bug_fix.cpp) and
[kernel service](../../kernel/game_state/src/services/interest_pool.cpp).
CSV uses country index, unavailable date `-1`, metadata flags, state/payout and
allocation/mutation results, and cumulative dropped results. Telemetry is
attempted with `metadata-degraded` quality, which the current bundled sink
rejects as invalid. Attempts are not gated by the CSV debug flag, despite
manifest help describing diagnostics together. Old CSV headers,
per-day summaries, named tags/dates, and fine-grained status names are not current
contracts. The [guide](../../docs/interest-fix.md) explains configuration and diagnosis.

## Supporting runtime evidence

### Native banking observations

An isolated vanilla Prussia campaign on the cataloged executable, with no
Smedley or mods, supplies passive GDB before/after observations at VAs
`0x00508d3e/0x00508d43` and `0x00685f0f/0x00685f14`. Eight daily passes cover
2,168 interest pairs and 2,176 bank pairs across 272 native country slots, with
no capture errors or cap flags. These observations verify native boundaries,
not the current plugin binary or its performance.

| Observed contract | Result (`verified-runtime`) |
| --- | --- |
| Named-only interest | 13 calls: treasury debit = named bank credit, `134808` raw total. |
| Mixed named/ordinal-zero interest | 27 calls: `203931` raw treasury debit, `108529` named bank credit; the `95402` difference matches ordinal-zero base dues. Shortfall execution is not established. |
| Bank allocation | 64 positive calls clear `243337` pending raw units and add `25463` to state pools. All recorded state allocations match native arithmetic; the unallocated `217874` is discarded, not a unit-conversion discrepancy. No low-reserve or oversized-share case is exercised. |
| Pool persistence and POP interest | Pools rise `10768` → `36231` over eight days. Interest flow 7 stays zero across 16,269 within-call and 14,214 adjacent-day common positive-savings POP observations. Later/indirect/transient payout and other POPs are not excluded. |

Why state savings weights do not sum to the bank denominator remains unresolved.
Ordinal-zero origination observations are qualified in
[fiscal evidence](fiscal-credit-boundaries.md#origination).

### Common fixture and scope

The historical tables below use the supplied unmodified `benchmark.v2` unless
a row says otherwise: no selected mod recorded, exact targets, speed 5, and
source save unchanged. Seven-day intervals were raw date `59883384` → `59883552`.
“Unrecorded” means the revision/configuration was not retained; it does not mean
the run tested today's code. Short probe runs ended paused/responsive; later
bounded-exit rows explicitly tested native exit. Zero flags/drops/gaps apply
only to the reported collection and fixture, not arbitrary campaigns or mods.

### Read-only and reversible mapping runs

Revision is unrecorded for these runs. Configurations use `campaign_runner`
plus the named retired probe or telemetry, **not the production fix**.

| Configuration | Result |
| --- | --- |
| Initial structure, seven days, `interest_probe` | 1,897 samples = 271 slots × 7 dates; 597 states, 2,311 province candidates per pass; zero flags/count mismatch/drops. Creditors appeared day 2; state-interest candidate reached 8,568, same-country bank always zero. Collection 22,241 µs total, 11.72 µs mean, 224 µs max, 3,141–3,250 µs/day; queue publication excluded. |
| Exact boundary, seven days, paired `interest_probe`; separate empty-registry one-day run | 3,794 rows / 1,897 ordered pairs; zero flags/drops/unavailable dates; unchanged state/vector counts. 12 SWE/SAR creditor calls, treasury −87,242, own bank/state unchanged: **same-country correlation was the wrong target**. Collection 47,010 µs, mean 12.39, max 352, publication excluded. Hardened subscribed one-day repeat: 271 complete clean pairs. |
| Seven days, destination probe | All 1,897 pairs; 12 creditor calls resolved every entry, paid bytes one, zero flags/drops. Treasury −87,242 = banks +87,242; aggregate call transfers 4,947–10,365. Rate/debt and destination state fields unchanged in-call. Collection 55,704 µs, mean 14.68, max 619. |
| Seven days, destination POP probe | All 1,897 pairs clean; 12 creditor samples covered 346–661 provinces, 4,498–8,593 list records, 2,635–5,565 POPs/phase, exact attempt/success counts, no duplicates. Savings drift 19–108; savings unchanged in-call. Collection 345,637 µs, mean 91.10, max 13,821: too expensive for default telemetry. |
| Seven days, only runner + `pop_money_fixture` | At date `59883432`, SWE destination POP: +1000/−1000 exactly added/restored money 23,890,866, slot 7 zero, total zero; savings 27 unchanged. `sample_flags=0`, addition/restoration verified. ABI/field proof only, not allocation acceptance. |
| Seven days, daily-entry/boundary correlation | All 1,897 pairs/start snapshots clean; bank/state unchanged from daily entry to interest boundary, bank already zero at entry. |
| Seven days, global bracket probe | Seven final (`D50`, ordinal 271) / first (`REB`, ordinal 1) snapshots, 1,897 clean pairs. Five transitions clear banks 71,785, add states 8,555. |
| One day, capped global probe | 271 clean pairs; after 512-country cap/timing correction, global scans were included at 3,376/3,370 µs rather than omitted from callback cost. |
| Seven days, individual destination probe | 1,897 clean pairs, 12 calls, 40 transfers of 145–3,268 raw units; per-call individual sum = aggregate bank delta = −treasury delta. Total 87,301; destination ordinals 2/3/4/5 received 12/11/12/5 transfers. |
| Seven days, telemetry `world.economy.credit` + `pop.cashflow.aggregate`, no fix | 30,275 records, zero gaps/drops. State interest 0, 0, 1,268, 2,817, 4,560, 6,481, 8,586; no index-7 native cash flow despite 12,805 component records. |

The five global transitions preserve the nonconservation observation, not just
its total:

| Final date | Bank pending | State pool then → next entry | State increase |
| --- | ---: | --- | ---: |
| 59883432 | 11,353 | 0 → 1,268 | 1,268 |
| 59883456 | 15,108 | 1,268 → 2,817 | 1,549 |
| 59883480 | 15,108 | 2,817 → 4,561 | 1,744 |
| 59883504 | 15,108 | 4,561 → 6,479 | 1,918 |
| 59883528 | 15,108 | 6,479 → 8,555 | 2,076 |

### Superseded direct-creditor payout tests

These tests establish mapping/native-write provenance, **not state-pool
acceptance**. All variants below used direct destination-bank payouts, including
the early implementation through `34b750f`; they predate the state-pool design.
Historical `interest_fix.csv` is not today's `interest_bug_fix.csv`. All use the
common fixture; revision is unrecorded unless listed.

| Revision / configuration | Result and scope |
| --- | --- |
| Two days, diagnostic fix | SWE 4,947 → 4,947,000 across 261 POPs; SAR 6,406 → 6,406,000 across 492; all postconditions, zero flags/drops. A repeat reproduced those results with immediate per-POP checks and all-candidate duplicate rejection. |
| Seven days, fix + read-only probe | 12 paid rows, bank 87,242 → POP 87,242,000; 60,936 traversed records, 7,514 paid/verified POPs, no flags/drops. Independent 1,897 probe pairs reconciled treasury/banks. |
| Two-day observer smoke | SWE 346 provinces/2,635 POPs in 9,000 µs; SAR 615/5,077 in 17,624 µs. All 753 paid POPs verified; health/value accepted, zero flags/gaps/drops/writer failures. |
| 60-day ordinal-zero regression after diagnostic correction | 181 paid + 322 no-transfer health records, zero flags/gaps/drops/writer failures; each paid paired with value, two complete monthly snapshots. |
| `f057bf5`, 3,650 days, pre-batching observer | 235,918 trace records; CSV 136,852 rows. Paid 94,796; no-transfer 34,243; allocation rejected 2,921; invalid pairs 4,892 (`0x01000000`, topology/delta changed before mutation). Paid total 801,282,450 → 801,282,450,000; 33,246,154 verified writes. No other failure, gaps/drops/writer failures; all publications accepted, paid-only value records. Callback total 1,629,137,122 µs; paid median/max 13,817/67,653, allocation median 9,166, invalid 1,492, no-transfer 21. |
| Two-day opt-out | No `interest_fix` selection; existing fix CSV unchanged. This proves opt-out for that artifact, not current throughput. |
| `d8458bd`, 3,650 days, batched observer, speed 5 | 3,650 summaries, no partial days/rejected debtors/recipient failures; 132,351 recipient payouts, 8,635,406 verified POP writes. Named bank total 2,003,503,700 = domestic 343,963,354 + foreign 1,659,540,346; POP total 2,003,503,700,000. Every daily CSV total matched value telemetry, all publications accepted, zero flags/gaps/drops/writer failures. 11,571 trace records. No no-savings outcome here; a prior deterministic path did correctly reject POP-bearing countries with no positive savings. |
| Seven-day checked-boundary smoke, runner/fix/telemetry `GiveMoney` hook | Kernel-retained identity and `CurrentGameSession`; all three plugins loaded after in-process check; 3,684 clean ordered records, 24 paid recipients, 92,860 → 92,860,000, 4,911 verified POPs; exact pause, telemetry drain, native exit. |
| Seven-day observer, 58-signature catalog | Matched 58 then-active signatures (**not today's catalog count**); 45 clean records, seven health summaries, zero rejected debtors/flags/dropped results; final day 20,468 → 20,468,000, 1,034 verified POPs. |
| Seven-day integration, 64-signature catalog, telemetry/runner/fix | 64 then-active signatures; 1,970 clean records, no Victoria error-log entries; seven clean summaries, six nonzero days; final day 20,472 → 20,472,000, 1,115 verified POPs. |
| Checked batch, 3,650 days, runner/fix, CSV/telemetry off | Native bounded exit, 366.829 s / 9.95 days/s; repeats 9.93–10.09, not a guaranteed 10 floor. Previous same-fixture implementation 496.4 s / 7.35 days/s. Source save and pre-existing CSV unchanged. |

Matched no-fix baselines, exact older observer configurations, and economic
outcome comparisons are in [telemetry evidence](telemetry.md#historical-pre-batching-paired-ten-year-observer-benchmark)
and its [batched comparison](telemetry.md#final-batched-paired-ten-year-observer-benchmark).
Those results do not measure comprehensive money supply or establish bankruptcy
behavior.

### State-pool tests before the C-service port

| Revision / configuration | Result and scope |
| --- | --- |
| Revision unrecorded, seven-day diagnostic state-pool fix | Seven cleanup passes covered 597 states each; 48 complete pools, 244 verified writes, exact `pool * 1000`, no flags/drops/mutation failure/retained successful pool. |
| Baseline vs fix; revisions unrecorded, 3,650-day lifecycle-only pair | 369.927 s / 9.86682 days/s vs 561.080 s / 6.50531; throughput −34.1%, elapsed +51.7%. Exact targets, zero gaps/drops/writer failures. |
| Revision unrecorded, seven-day optimized diagnostic fix | One session cleanup instead of seven daily scans, 47 pools/180 verified writes, no failure/conservation error/drop. Positive-pool callback total 12,898 µs vs 29,829 before selective POP traversal on the same fixture shape. **Current port again cleans daily.** |
| Fix vs baseline; revisions unrecorded, 3,650 days, diagnostics/telemetry off | Full launch-to-exit 339.478 s / 10.7518 days/s vs 302.085 s / 12.0827; retained 89.0% throughput. Includes loading/entry/shutdown. Then-path used once-per-session/after-failure cleanup, ignored recapitalization, selective POP walks, once-per-callback membership and amortized signature/session checks. Not a benchmark of the current daily-cleanup port. |
| After `c357fd9` guard correction, 36,500 days, runner/fix/diagnostics | Exact target/native exit, 5,104.095 s / 7.1511 days/s **including CSV I/O**, not production throughput. CSV 938,029,562 bytes / 12,945,293 rows. Covered 36,499 completed bank dates: runner pauses before final target's bank pass. |

Century run outcomes after the duplicate-scope correction:

| Outcome | Count / raw total |
| --- | ---: |
| Successful state pools | 11,695,674 |
| Verified POP writes | 144,643,153 |
| Paid state-pool units | 122,721,581,891 |
| Exact POP payout units | 122,721,581,891,000 |
| No-positive-savings pools | 1,213,120 |
| Retained no-savings pool units | 38,472,078,485 |
| Later initialization discard units | 38,470,779,489 |
| Full cleanup passes | 31,749 |

Every paid row had matching paid/verified counts, exact `pool * 1000`, and
successful allocation. Zero quality flags, result drops, collection/duplicate/
overflow/unavailable/precondition/partial/postcondition/conservation failures
were reported. Only no-eligible-savings outcomes failed to pay; retained pools
were discarded before a later pass. The retained/discarded difference is pools
created near the final target without a later cleanup. The source save remained
unchanged. This demonstrates the historical implementation's checked payouts
and guards on one fixture, not arbitrary-mod correctness or current-port
century acceptance.

## Known limits

- Negative static evidence cannot exclude every aliased native consumer. Native
  telemetry corroborates only the observed fixture/interval.
- State allocation is not bank conservation. Preserve recapitalization, native
  residual loss, and the potential early-clamp behavior rather than claiming a
  fix to the complete lending system.
- Complete default accounting, bankruptcy effects, and missing bank-cash/world-
  money categories are not established here. Do not sum overlapping claims,
  deposits, bank accumulators, and liquid balances into a money-supply estimate.
- Current C-service port has degraded metadata and daily full cleanup. Historical
  state-pool speed and century results must not be advertised as current runtime
  acceptance. A new exact-binary run is needed for those claims.
- Host allocation/reader tests cover conservation, deterministic ties,
  nonpositive/no savings, bounds, and arithmetic rejection; they cannot prove
  engine lifecycle, UI presentation, native-call safety, save compatibility, or
  multiplayer behavior. Partial multi-POP writes have no rollback guarantee.
