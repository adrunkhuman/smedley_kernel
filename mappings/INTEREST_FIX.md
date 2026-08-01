# Creditor POP interest investigation

Victoria II charges debtor countries interest and credits a country bank, but
historical Smedley commit `794c98e` claimed that the corresponding creditor POP
payments were omitted. That implementation is an investigation lead, not a
supported fix: it depended on a generated 469,301-line layout, mutated
provisional fields, and used floating-point proportional allocation without a
conservation test.

## Documented game mechanics

The Victoria 2 Wiki [Loans](https://vic2.paradoxwikis.com/Loans) page (permanent
revision
[`22984`](https://vic2.paradoxwikis.com/index.php?title=Loans&oldid=22984)) is a
community-maintained description of intended and observed gameplay, not proof
of executable internals. It reports that:

- POP surplus money is deposited in the POP's national bank and may be lent to
  sovereign countries;
- a borrower uses its own national bank before foreign banks and repays its own
  bank last;
- daily interest is based on domestic and foreign bank debt, effective interest
  modifiers, and division by 30; and
- charged interest is not paid to depositor POPs, producing the reported
  liquidity bug, although the page notes that this may be an intentional money
  sink rather than an accidental omission.

The supported executable independently corroborates the debt-times-rate daily
calculation and the immediate debtor-to-creditor-bank transfer described below.
The fix should therefore distribute the game's exact credited integer amount;
it must not reimplement the interest formula or merge domestic and foreign
modifier behavior. The wiki also supports savings-weighted POP distribution as
the gameplay model, while runtime evidence remains authoritative for storage,
scale, ordering, and safe mutation. The feature must remain opt-in because
restoring the payment changes the vanilla money supply.

Loan creation passes the debtor's own tag and nonzero ordinal for domestic-bank
debt, another country's tag/ordinal for foreign debt, and literal `---` with
ordinal zero for the Shadowy Financiers fallback. The first two forms credit the
identified country bank and are paid to that country's savers. The ordinal-zero
form has no lender POPs or destination-bank credit, so its exact treasury
residual remains a measured vanilla sink.

## Current static evidence

All addresses below are RVAs in the cataloged Victoria II 3.04 executable.

| Evidence | Status | Basis |
| --- | --- | --- |
| `CCountry::DailyUpdate` `0x00108590` | `verified-current` | Its only direct call to `PayDailyInterest` is at `0x00108d3e`, with the country pointer already on the stack. The checked kernel trampoline invoked the original call and emitted 1,897 ordered runtime boundary pairs. |
| `CCountry::PayDailyInterest` `0x00123c30` | `verified-runtime` | The paired boundary run observed current-country treasury reductions on all 12 calls with creditor entries. Its sole direct caller and `ret 4` cleanup are statically established. |
| Creditor destination bank path | `verified-runtime` | `PayDailyInterest` reads the current country creditor vector at `+0xe8c`. A nonzero ordinal at creditor `+0x0c` resolves a destination country and credits bank `+0x20`; the branch at VA `0x00524022` skips that credit when the ordinal is zero. All 12 creditor-bearing seven-day fixture calls contained only country destinations and conserved the exact debtor treasury loss in the summed destination-bank gain. A later diagnostic captured 22 rejected rows with the same no-country key `0x2d2d2d` (`---`), ordinal zero, and paid byte one. Ordinal-zero entries are retained in aggregate telemetry but excluded from destination conservation and POP payout. |
| Domestic creditor creation | `verified-static-callsites` | `CCountry::TakeLoan` at RVA `0x00122910` passes the debtor's own tag/ordinal from `CCountry+0x1c/+0x20` through `CanTakeLoanFrom` and `TakeLoanFrom`. Domestic debt is therefore an explicit self-tagged creditor and follows the same verified nonzero-ordinal bank-credit path. |
| Shadowy Financiers creation | `verified-static-callsites` | The fallback path writes literal `---`, zeroes the ordinal at VA `0x00522c24`-`0x00522c27`, and calls `TakeLoanFrom` at `0x00522c6a`. Localisation maps `SHADOWY_INVESTOR` to “Private Investors,” and `SHADOWY_FINANCIERS_MAX_LOAN_AMOUNT` is 1500 in vanilla. |
| Creditor `+0x8/+0x10/+0x18/+0x20` | `verified-runtime` | Static code reads the tag/ordinal at `+0x8`, multiplies the 64-bit `+0x18` value by the 64-bit `+0x10` value in its payment calculation, and updates the byte at `+0x20` on payment. The destination run validated every tag/ordinal and observed `+0x20 == 1` for every paid entry; the economic names on the two 64-bit fields remain candidates. |
| Destination bank `+0x20` | `verified-runtime` | The static add target and repeated exact before/after runs agree. The individual-destination run observed 40 positive transfers across 12 calls; every child sum exactly matched its aggregate bank delta and negated the corresponding debtor treasury loss. Other historical `CBank` fields remain unverified. |
| `PayDailyInterest` boundary `0x00108d3e` | `verified-runtime` | The kernel replaces the sole direct call with a register/flags-preserving trampoline, emits `before`, invokes the original callee, emits `after`, and resumes at `0x00108d43`. Both subscribed and unsubscribed runtime fixtures reached exact targets. |
| Country state list `+0xe44` | `verified-current` | Current country update code walks node data at `+0`, next at `+8`, and terminates at null. State-creation callers maintain head `+0xe44`, tail `+0xe48`, and count `+0xe4c`; the seven-day runtime probe walked every reported state without a mismatch. |
| State constructor `0x000cdc60` | `verified-static-callsites` | Three callers allocate `0x290` bytes. The constructor initializes 64-bit slots `+0x258` and `+0x260` to zero. |
| State province vector `+0x48` | `verified-runtime` | Current code and the destination POP run agree that its four-byte elements are game-state province indices. All 346-661 destination provinces per creditor-bearing sample resolved to readable province POP vectors with no quality flag. |
| State savings `+0x258` | `verified-runtime` | Current code converts POP savings through the global `1000.0` scale when updating this 64-bit state value. Across 12 live destination samples, summed `POP+0x250 / 1000` tracked it within 19-116 raw state units. |
| State interest candidate `+0x260` | `provisional` | Its 64-bit shape and changing live values are established, but the `interest_payments` economic label still lacks an independently observed state-to-POP transfer. |
| Province POP-list vector `+0x194` | `verified-runtime` | Current code indexes 16-byte list elements from `+0x194/+0x198`. The destination run walked exactly 13 list records for each of 346-661 resolved provinces without a mismatch. |
| POP size and linked-list next | `verified-runtime` | Creation allocates `0x288` bytes, current list walks use next at `+0x27c`, and the destination run walked 2,635-5,565 entries per creditor-bearing sample with exact list count/tail agreement. |
| POP savings `+0x250` and scale | `verified-runtime` | Current code divides this signed 64-bit value by fixed-point `1000.0` at RVA `0x00b0b168` when updating state `+0x258`. The live aggregate correlation confirms the scale while exposing small accumulated bookkeeping/rounding differences. |
| `CPop::GiveMoney` `0x0055a5f0` | `verified-runtime` | Seven direct callers establish `EAX=CPop*`, `ESI=CashFlowType`, stack-passed 64-bit amount, and `ret 8`. A reversible fixture observed exact writes to money `+0x180`, indexed cash flow `+0x1d8`, and total `+0x218`, then exact restoration. |
| Interest cash-flow index `7` | `verified-static-callsites` | The presentation path at RVA `0x00566440` reads POP `+0x210`, index 7 of `+0x1d8`, and formats it through localisation key `POP_DAILY_INTEREST`. The reversible fixture independently confirmed that selected slot 7 changed by the injected amount and restored; it did not execute the presentation path or snapshot all other slots. |

The state constructor is VA `0x004cdc60`, hence RVA `0x000cdc60` for the
preferred image base `0x00400000`. Earlier notes that treated `0x004cdc60` as
an RVA were incorrect.

## Read-only runtime probe

The opt-in `interest_probe` plugin writes `interest_probe.csv` in the game
directory. It does not patch the historical post-update site and never mutates
game state. The kernel signature-checks the sole `PayDailyInterest` call and
emits `before` and `after` observations around the original callee. Each phase:

- validates readable pages before copying provisional structures;
- caps traversal at 512 states and 1,024 four-byte vector elements per state;
- records state-list count agreement, candidate state totals, creditor count,
  treasury, and bank interest;
- caps creditor destination traversal at 64 entries, skips the executable's
  ordinal-zero no-destination form, validates each nonzero destination
  tag and ordinal against the bounded game-state country vector, and records
  candidate creditor values plus aggregate destination bank/state values;
- attempts at most 4,096 destination province resolutions, validates at most 128
  16-byte POP-list records per province, and attempts at most 100,000 POP reads across the
  complete callback while retaining raw and `/1000` savings totals;
- retains only the copied `before` POD until `after`, then atomically enqueues
  one fixed-size pair in a 1,024-slot single-producer queue; and
- reports dropped pairs and collection time while a worker performs all CSV I/O.

The current investigation build also snapshots the same country at the existing
`DailyUpdateEvent` entry and copies its bank `+0x20` and aggregate state `+0x260`
values into the later boundary pair when country and date match. This read-only
correlation tests whether the country update clears the bank accumulator into
the state candidate without introducing another hook. It additionally records
all-country bank/state totals, capped at 512 country slots, after the final
country's interest boundary and at the first country entry on each date,
bracketing the global post-country phase without retaining game pointers.
The worker also writes `interest_probe_transfers.csv`, one row per positive
individual destination-bank delta; game-thread callbacks perform no file I/O.

`flags=0x0` means the structural checks passed. Any nonzero flag makes the row
unsuitable for semantic promotion. The two state columns deliberately include
`candidate` in their names because this probe does not establish economic
meaning by itself. A contained boundary-subscriber exception increments a
kernel counter and sets a callback-failure flag on subsequent probe rows.

## Initial structure run

On 2026-07-31, Release artifacts were run from the unmodified supplied
`benchmark.v2` for seven game days at speed 5 with only `campaign_runner` and
`interest_probe`. The campaign advanced from raw date `59883384` to the exact
target `59883552`, then remained paused and responsive. The source-save SHA-256
remained
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

The resulting CSV had SHA-256
`a40ebce331f54bc3598f829b7112cca8e93efd05e5044923ba51e30ae19bf213`.
Across 1,897 samples, each of the seven dates contained all 271 country slots,
597 reported states exactly matched 597 walked states, and all daily passes
contained the same 2,311 candidate vector elements. No row had a structural flag, state-count
mismatch, or dropped sample.

Creditor entries appeared on the second sampled date and the aggregate state
interest candidate rose from zero to 8,568 by the seventh. The bank `+0x20`
candidate remained zero in every pre-update row, and countries with creditor
entries were not the countries with nonzero state-interest candidates. This
establishes stable live structure and changing candidate data, but not the
economic labels or the pre/post-interest correlation boundary.

The probe's collection phase consumed 22,241 microseconds across the seven
complete daily country passes: 11.72 microseconds per country sample on average
and 224 microseconds at the observed maximum. The per-day collection totals
ranged from 3,141 to 3,250 microseconds. These measurements exclude the final
fixed-size queue copy and atomic publication. This is bounded and suitable for
the opt-in investigation, not a budget for a default hot-path feature.

## Exact interest-boundary run

The final paired probe was run from the same unmodified save for seven days at
speed 5. A separate one-day run with no `interest_probe` subscriber first
reached its exact target, proving that the kernel trampoline retained the
original call when its event registry was empty. The subscribed run then
advanced from raw date `59883384` to `59883552`, remained paused and responsive,
and preserved the source-save hash above.

The paired CSV had SHA-256
`eb8f16afb8cd528b87f591bf562e2c03d9606f3c1750a3faadb911af0df064bb`.
Its 3,794 rows formed exactly 1,897 country/date pairs: one ordered `before` and
one `after` row for all 271 country slots on each date. No pair was malformed;
no row had an unavailable date, structural flag, state-count mismatch, or
dropped pair. State counts and candidate vector-element counts were unchanged
within every call.

Twelve calls changed current-country treasury, all for the only countries with
creditor entries in this fixture: Sweden and Sardinia-Piedmont. Their cumulative
raw fixed-point delta was `-87,242`. Current-country bank `+0x20`, state
`+0x258/+0x260`, and creditor-vector count never changed within the same calls.
Disassembly independently shows that the bank credit resolves the creditor tag
to another country's bank. The earlier same-country bank/state comparison was
therefore the wrong correlation target; destination-country values are the next
probe.

The paired collection phases consumed 47,010 microseconds total: 12.39
microseconds per phase on average and 352 microseconds at the observed maximum.
As above, this excludes final queue publication.

After hook-transaction and cross-DLL callback-containment hardening, the final
installed artifact repeated a one-day subscribed smoke with 271 complete pairs,
zero flags or dropped pairs, exact-target pause, a responsive process, and the
same unchanged source-save hash.

## Destination conservation run

The hardened probe then repeated the seven-day fixture with bounded creditor
decoding and destination-country resolution. Run
`fb9aff46-9194-4cb3-9e4f-c8ab8ffa0bec` advanced from raw date `59883384` to
the exact target `59883552`, remained responsive and paused, and preserved the
source-save hash. The closed CSV had SHA-256
`1686e0c5ee9a5dabc8fcd1737fcecc51b9da70cff9f18c21bdd36c9aa8b9b1a8`.

Its 3,794 rows again formed exactly 1,897 ordered pairs across seven dates and
271 country slots per date. All 12 creditor-bearing pairs resolved every entry:
creditor and destination counts agreed, every observed paid byte was one, and
there were no malformed pairs, quality flags, or dropped pairs.

Every one of those 12 calls satisfied exact raw fixed-point conservation:

```text
debtor treasury delta + summed destination bank delta = 0
```

The cumulative debtor delta was `-87,242` and cumulative destination-bank delta
was `+87,242`. Individual transfers ranged from `4,947` to `10,365` raw units.
Creditor `+0x10/+0x18`, destination state `+0x258/+0x260`, and creditor count
did not change within any call. This verifies the immediate country-to-bank
transfer; it does not establish the later state-to-POP distribution or the
economic names of the provisional state fields.

The expanded collection phases consumed 55,704 microseconds total: 14.68
microseconds per phase on average and 619 microseconds at the observed maximum.
The additional cost occurs only for the opt-in probe and remains bounded by the
creditor and state traversal limits.

## POP savings scale run

Run `3225cbfe-9174-4d15-972c-e109009e5d37` repeated the seven-day fixture with
destination-only POP traversal. It reached raw date `59883552`, remained paused
and responsive, and preserved the source-save hash. The closed CSV had SHA-256
`f3e076fb836de2cc2cba67e0a330987bbbe0aeb4e6d4f305c9fd4c0f381847bc`.

The 3,794 rows again formed 1,897 ordered pairs. Every creditor destination,
state province index, province POP vector, 16-byte list record, list count/tail,
and linked POP resolved without a quality flag. There were no malformed pairs
or drops. The 12 creditor-bearing pairs covered 346-661 destination provinces,
4,498-8,593 POP-list records, and 2,635-5,565 linked POPs per phase. Every
province had exactly 13 list records in this fixture. Attempt and successful
counts agreed for every province and POP, and no duplicate identity was found.

The executable converts each POP savings value to state scale by dividing by
`1000`. Runtime totals corroborated that scale but were deliberately not forced
to equality: summed per-POP truncated values exceeded aggregate state `+0x258`
by 19-108 raw state units. For example, the first Sweden sample observed raw POP
savings `1,909,522,541`, per-POP `/1000` sum `1,909,394`, and state savings
`1,909,375`. Neither POP nor state savings changed inside any observed
`PayDailyInterest` call.

The small difference is real evidence of accumulated transaction rounding or
bookkeeping behavior. A future fix must use the stored state denominator and
define remainder ownership; it must not assume that recomputing POP savings
produces exact state equality.

Collection consumed 345,637 microseconds over all phases: 91.10 microseconds on
average and 13,821 microseconds at the observed maximum. The peak occurred on
opt-in creditor destination POP walks. This remains bounded diagnostic work,
but it is too expensive for default telemetry.

## Reversible GiveMoney fixture

`pop_money_fixture` is a contributor-only CMake target and manifest; it is not
installed with the ordinary plugins. Selecting that manifest is explicit
authorization for one temporary mutation. The fixture waits for a structurally
valid destination POP, snapshots four 64-bit fields, invokes `CPop::GiveMoney`
with cash-flow index 7 and raw amount `+1000`, snapshots again, invokes `-1000`,
and verifies exact restoration. It performs no callback-thread I/O and reports
the fixed-size result from a worker thread.

Run `5ee018ab-f95f-4bc5-962e-d5cf5c968260` used only `campaign_runner` and this
fixture from the unchanged supplied save. At raw date `59883432`, the selected
Sweden creditor destination POP produced:

| Field | Before | After `+1000` | After `-1000` |
| --- | ---: | ---: | ---: |
| Money `+0x180` | 23,890,866 | 23,891,866 | 23,890,866 |
| Interest cash flow `+0x210` | 0 | 1,000 | 0 |
| Total cash flow `+0x218` | 0 | 1,000 | 0 |
| Savings `+0x250` | 27 | 27 | 27 |

The fixture row had `sample_flags=0`, `addition_verified=1`, and
`restoration_verified=1`. Its closed CSV SHA-256 was
`d473db637f2b3c4cacfe865b295fa650ca2bee97523851342a6d970497cadb29`.
The campaign still reached raw date `59883552`, remained responsive and paused,
and preserved the source-save hash. This verifies the narrow ABI and field
effects; it does not yet define a production allocation algorithm.

## Bank clearing correlation

Run `6e69ee23-75a0-4441-8cf4-7129e19a749b` paired each country at
`DailyUpdateEvent` entry with its later `PayDailyInterest` boundary. Its 3,794
rows formed all 1,897 expected pairs with every start snapshot available, zero
quality flags, and zero drops. Country bank `+0x20` and aggregate state `+0x260`
never changed between those boundaries; bank `+0x20` was already zero at every
country entry. The closed CSV SHA-256 was
`1a98cad7ee6b246c464ce5b50a69a0c6f6d56f40b6f692b2aef5ca29d9e9626b`.

Run `472db1b0-c70d-4979-91ad-aec05858d3e2` then bracketed the global
post-country phase. It again produced 3,794 rows, all 1,897 ordered pairs, seven
first-country snapshots, and seven final-country snapshots with no flags or
drops. The final country was ordinal 271 (`D50`); the first was ordinal 1
(`REB`). The closed CSV SHA-256 was
`aad060bd69d2c9ecc56afc4afd618fd621b40a819daa643d95a0b29b27726367`.

The five complete nonzero cross-date transitions were:

| Date at final boundary | Global bank `+0x20` | State `+0x260` then | State `+0x260` next entry | State delta |
| --- | ---: | ---: | ---: | ---: |
| `59883432` | 11,353 | 0 | 1,268 | 1,268 |
| `59883456` | 15,108 | 1,268 | 2,817 | 1,549 |
| `59883480` | 15,108 | 2,817 | 4,561 | 1,744 |
| `59883504` | 15,108 | 4,561 | 6,479 | 1,918 |
| `59883528` | 15,108 | 6,479 | 8,555 | 2,076 |

In every transition, global bank `+0x20` was zero at the next first-country
entry. The engine therefore clears the complete `71,785` raw bank accumulator
inside this bracket, while aggregate state `+0x260` rises by only `8,555`.
Those state changes may include other activity, so this result proves neither a
conversion ratio nor causation. It does prove that state `+0x260` is not an
exact conserved representation of the cleared creditor interest and must not be
used as the production payout total.

The production design should instead derive each payout from the exact
per-destination bank delta across `PayDailyInterest`, which is already the
verified debtor transfer amount. Depositor savings may determine shares, but
must not determine or recompute the total interest. Both runs preserved the
source-save hash and ended paused and responsive.

After adding the 512-country cap and extending callback timing over the global
scans, one-day smoke run `f3416e70-4f81-46be-b389-6c3231ba60e9` produced all
271 ordered pairs with both global snapshots, no flags or drops, and the same
unchanged source-save hash. The first and final global scans now appeared in
`collection_us` at 3,376 and 3,370 microseconds respectively instead of being
excluded from the reported callback cost.

## Individual destination run

Run `c8519eb7-8a9d-4826-ae40-9d11e5313232` recorded each destination bank
balance in the bounded before/after samples and emitted each positive delta from
the worker thread. The main CSV had SHA-256
`2fe7355f2f05b9a070dd1fe30c06a271b48b3e9018ab839a31fe9ae1247b152a`;
the child transfer CSV had SHA-256
`1cec436c646ffbc3fde5c2dff82c881f2a789a0385564afc176e01d3ebc24b1c`.

The run produced all 1,897 expected country/date pairs, no flags or drops, 12
creditor-bearing calls, and 40 positive individual destination transfers. Each
transfer was between 145 and 3,268 raw fixed-point units. Every call satisfied
both exact equalities:

```text
sum(individual destination bank deltas) = aggregate destination bank delta
debtor treasury delta + sum(individual destination bank deltas) = 0
```

The cumulative individual transfer was `+87,301`; cumulative debtor treasury
delta was `-87,301`. Destination ordinals 2, 3, 4, and 5 received 12, 11, 12,
and 5 transfers respectively. The campaign reached the exact target, remained
paused and responsive, and preserved the source-save hash. This verifies the
exact per-creditor amount that a fix may distribute without reimplementing the
loan formula or depending on state `+0x260`.

## Allocation contract

`AllocateInterest` is a deterministic preparation step that does not touch game
state. It writes payout/remainder fields and ordering scratch supplied by its
caller, clearing payout/remainder output before every valid-buffer result. Its
contract is:

1. The payout total is the verified individual destination-bank delta multiplied
   by the verified `1000:1` state-to-POP money scale.
2. Only destination POPs with strictly positive stored savings participate.
   Zero or negative savings receive zero.
3. Each base payout is the integer floor of `total * POP savings / summed
   positive POP savings`.
4. Remaining single raw POP-money units go to the largest fractional remainders.
   Equal remainders use stable province/list/POP traversal order.
5. A nonpositive transfer, no eligible savings, insufficient bounded scratch,
   savings-sum overflow, payout-scale overflow, or multiplication overflow
   produces no mutation. The implementation deliberately rejects an extreme
   value instead of introducing floating-point or platform-dependent arithmetic.
6. Every payout is computed before the first `CPop::GiveMoney` call. A
   production plugin must verify that their exact sum equals `transfer * 1000`
   before applying any entry.

This largest-remainder policy is deterministic, exactly conservative in POP
money units, and does not depend on the drifting state `+0x258` aggregate or the
nonconserved state `+0x260` candidate. Win32 Release unit tests cover exact
conservation, remainder ordering and ties, nonpositive savings, an empty
eligible set, and conservative overflow rejection. No production mutation uses
the allocator unless the separate `interest_fix` manifest is selected.

## Optional production fix

`interest_fix` subscribes to the exact `PayDailyInterest` boundary and is
disabled unless explicitly selected. It conflicts with historical `v2up`.
The hot boundary copies only country, creditor, treasury, and destination-bank
state. It accumulates exact positive destination deltas by recipient ordinal and
classifies self-tagged amounts as domestic and other named amounts as foreign.
After every expected country pair for the date has completed, it processes each
recipient once in ascending ordinal order:

1. requires each debtor's individual nonzero-ordinal bank deltas to sum exactly
   and not exceed its treasury loss, recording the remainder as the Private
   Investor sink;
2. sums transfers with checked arithmetic and retains no game pointers across
   callbacks;
3. traverses at most 4,096 recipient provinces and 100,000 POPs, rejecting
   duplicate POP identities across the completed day;
4. computes every payout in preallocated storage and requires their sum to equal
   the bank transfer multiplied by 1,000;
5. verifies all known `CPop::GiveMoney` write ranges are writable and all money
   and cash-flow additions are representable before the first call; and
6. invokes cash-flow index 7 in deterministic destination/province/list/POP
   order, then verifies each POP's exact money, slot 7, total-flow, and unchanged
   savings postconditions.

The post-call sample re-resolves the pre-call destination identities and reads
their current bank values directly. A creditor entry repaid and removed by the
original function therefore remains measurable without retaining its pointer or
requiring the mutable creditor vector to keep the same order.

Any structural, identity, budget, overflow, conservation, or writable-memory
failure skips that debtor pair or recipient before mutation. A postcondition
failure disables later payouts and is reported. Callbacks perform no file I/O;
a bounded worker writes `interest_fix.csv`.

The exact batched CSV header is:

```text
date_raw,country,status,flags,source_count,pop_count,paid_pop_count,province_count,verified_pop_count,transfer_raw,domestic_transfer_raw,foreign_transfer_raw,private_sink_raw,payout_raw,allocation_status,callback_us,rejected_debtors,health_telemetry_result,value_telemetry_result,dropped_results
```

Possible statuses are `paid`, `invalid_pair`, `batch_invalid`, `day_incomplete`,
`day_summary`, `recipient_identity_invalid`, `collection_failed`,
`no_eligible_savings`, `allocation_overflow`, `allocation_invalid`,
`pop_balance_overflow`, `pop_not_writable`, `duplicate_pop`, `pop_identity_limit`,
`postcondition_failed`, and `conservation_failed`. `allocation_status` preserves
the allocator's exact result. `dropped_results` is the cumulative bounded
result-queue drop count. Telemetry result codes
follow the C ABI: 0 unavailable, 1 filtered, 2 accepted, 3 dropped, and 4
invalid. `interest.fix.health` covers rejected debtor pairs, recipient outcomes,
and daily summaries. `interest.fix.value` is emitted only for `paid`, so a
failed or partial attempt cannot expose an intended payout as an observed result.

The runtime evidence below through commit `f057bf5` describes the earlier
per-debtor implementation. It remains provenance for mappings and exact
mutation checks, not acceptance evidence for the batched implementation.

Sixty-day regression run `b88ce485-ed56-4634-9bb0-0927b0a83117` exercised the
ordinal-zero form after the diagnostic correction. It reached the exact target
with zero telemetry gaps, drops, or writer failure and produced 181 `paid` plus
322 `no_transfer` health records, all with flags zero. Every paid result had a
paired value record, both monthly economic snapshots were complete, and the
source-save hash remained unchanged.

Fix-enabled observer smoke run `5095e066-93e8-4faf-9060-dfa0199becd8`
completed two exact days with zero gaps, drops, or writer failure. SWE traversed
346 provinces and 2,635 POPs in 9,000 microseconds; SAR traversed 615 provinces
and 5,077 POPs in 17,624 microseconds. Both health/value pairs were accepted,
all flags were zero, all 753 paid POPs passed immediate postconditions, and the
source save remained unchanged.

Two-day smoke run `fb8fb767-d7ab-4fea-be87-7598d6f9c880` exercised the first
two creditor-bearing calls. Sweden paid 4,947 bank units as 4,947,000 POP-money
units across 261 verified POPs; Sardinia-Piedmont paid 6,406 as 6,406,000 across
492 verified POPs. All guards and postconditions passed with no flags or drops.
The closed fix CSV SHA-256 was
`fef04c5aade3e158f2600a388a1cc1de5bc1c0b036822a9fbe8122d3b9cbc697`.
After final containment hardening, run
`61b3d205-1ff1-4df5-a036-5a1e21ec1606` repeated those two exact paid rows and
the same CSV hash with immediate per-POP postcondition checks and
all-candidate duplicate rejection enabled.

Full seven-day run `1b9b194b-06b2-4bff-9c36-145875a95a88` produced 12 `paid`
rows, no flags or drops, and these exact totals:

| Measure | Result |
| --- | ---: |
| Destination-bank transfer | 87,242 |
| POP-money payout | 87,242,000 |
| POP records traversed | 60,936 |
| POPs receiving nonzero payouts | 7,514 |
| POPs passing all postconditions | 7,514 |

The simultaneously loaded read-only probe independently produced all 1,897
boundary pairs with no flags or drops and measured destination-bank gain
`+87,242` against debtor treasury delta `-87,242`. The fix CSV SHA-256 was
`849735871fb3f59bdd2b24d86b754f4988c257c9e9b8f96a38f75408cbf6c4b7`;
the probe CSV SHA-256 was
`6cae03ed4636c80c26766e7c805d401f755c9a6ec25effed51e463aac8261cac`.
The process reached the exact target and remained paused and responsive.

## Pre-batching ten-year fix result

Observer run `c6193149-f6b2-4a88-9366-32484389d40a` used commit
`f057bf5` and completed 3,650 exact days from the unchanged supplied save. Its
235,918-record trace had zero gaps, telemetry drops, or writer failures. The
closed `interest_fix.csv` SHA-256 was
`74bbde807d8dc60763a446429d442497d9378e2ba37b636f72cbe3e6bc8aaf5e`;
its 136,852 rows had zero result-queue drops, and every health/value telemetry
publication returned `accepted`.

| Status | Count | Flags | Mutation contract |
| --- | ---: | --- | --- |
| `paid` | 94,796 | zero | Complete payout and all postconditions passed. |
| `no_transfer` | 34,243 | zero | Valid creditor-bearing boundary with no positive destination-bank delta. |
| `allocation_failed` | 2,921 | zero | No complete eligible allocation; rejected before mutation. |
| `invalid_pair` | 4,892 | `0x01000000` | Destination topology/delta pair changed; rejected before mutation. |

No collection, overflow, writable-memory, duplicate-POP, conservation, or
postcondition failure occurred. Every `paid` health record had exactly one value
record. Their aggregate destination-bank transfer was `801,282,450` raw units;
the POP payout was exactly `801,282,450,000` units. All 33,246,154 nonzero POP
payout instances passed the immediate money, interest-flow, total-flow, and
unchanged-savings postconditions. No failed/no-transfer record emitted a value
event.

Callback time totaled 1,629,137,122 microseconds. Paid callbacks had a 13,817 us
median and 67,653 us maximum; allocation failures had a 9,166 us median;
invalid pairs had a 1,492 us median; and no-transfer callbacks had a 21 us
median. The paired baseline and full economic outcome comparison are recorded
in `TELEMETRY.md`.

Opt-out run `1759f606-f8b6-4588-aad0-31025e74fd60` repeated two days without
selecting `interest_fix`; the prior fix CSV retained the same hash, timestamp,
and length, proving the plugin did not load. Every run preserved the source-save
SHA-256.

## Remaining validation

1. Map bankruptcy and the missing bank-cash/world-money categories before
   claiming effects on bankruptcy or total money supply.
2. Add genuine HFM and GFM save fixtures before claiming those mods as tested.
3. Validate multiplayer compatibility before enabling the fix in multiplayer.
