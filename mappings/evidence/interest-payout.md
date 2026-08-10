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
restoring the omitted payment changes vanilla liquidity behavior and observed
POP balances. Comprehensive world-money supply remains unmapped, so the project
does not claim a measured total-money effect.

Loan creation passes the debtor's own tag and nonzero ordinal for domestic-bank
debt, another country's tag/ordinal for foreign debt, and literal `---` with
ordinal zero for the Shadowy Financiers fallback, displayed as "Private
Investors" in vanilla localization. The first two forms credit the identified
country bank and are paid to that country's savers. The ordinal-zero form has no
lender POPs or named destination-bank credit. The production fix leaves
ordinal-zero flows untouched and unallocated.

## Current static evidence

All addresses below are RVAs in the cataloged Victoria II 3.04 executable.

| Evidence | Status | Basis |
| --- | --- | --- |
| `CCountry::DailyUpdate` `0x00108590` | `verified-current` | Its only direct call to `PayDailyInterest` is at `0x00108d3e`, with the country pointer already on the stack. The checked kernel trampoline invoked the original call and emitted 1,897 ordered runtime boundary pairs. |
| `CCountry::PayDailyInterest` `0x00123c30` | `verified-runtime` | Its sole direct caller and `ret 4` cleanup are statically established. Bankruptcy construction can refund funds inside this call, so the debtor treasury before/after delta includes unrelated movement and is not a conservation check for named creditor transfers. |
| Creditor destination bank path | `verified-runtime` | `PayDailyInterest` reads the current country creditor vector at `+0xe8c`. A nonzero ordinal at creditor `+0x0c` resolves a destination country and credits bank `+0x20`; the branch at VA `0x00524022` skips that credit when the ordinal is zero. A later diagnostic captured 22 entries with the same no-country key `0x2d2d2d` (`---`), ordinal zero, and paid byte one. The production fix allocates only nonzero-ordinal named destination-bank deltas; ordinal-zero flows remain untouched and unallocated. |
| Domestic creditor creation | `verified-static-callsites` | `CCountry::TakeLoan` at RVA `0x00122910` passes the debtor's own tag/ordinal from `CCountry+0x1c/+0x20` through `CanTakeLoanFrom` and `TakeLoanFrom`. Domestic debt is therefore an explicit self-tagged creditor and follows the same verified nonzero-ordinal bank-credit path. |
| Shadowy Financiers creation | `verified-static-callsites` | The fallback path writes literal `---`, zeroes the ordinal at VA `0x00522c24`-`0x00522c27`, and calls `TakeLoanFrom` at `0x00522c6a`. Localisation maps `SHADOWY_INVESTOR` to “Private Investors,” and `SHADOWY_FINANCIERS_MAX_LOAN_AMOUNT` is 1500 in vanilla. |
| Creditor `+0x8/+0x10/+0x18/+0x20` | `verified-runtime` | Static code reads destination tag/ordinal at `+0x8/+0x0c`, multiplies debt `+0x18` by interest `+0x10`, and updates paid byte `+0x20`. SWE save/runtime correlation maps interest `0.01999` to raw `655` and debt `14969.11719` to raw `490508032`; run `3f63c64b` bracketed 12 exact requested/treasury/debt repayment reductions with retained identities. |
| Destination bank fields | `verified-runtime` | `+0x20` is the exact temporary interest destination (40 positive transfers across 12 interest calls). Save/runtime correlation maps `+0x10` to `money` and `+0x18` to `money_lent`; repayment reduces `money_lent` only for resolved nonzero-ordinal lenders and clamps it at zero. Ordinal-zero Shadowy Financiers have no destination-bank mutation. |
| `PayDailyInterest` boundary `0x00108d3e` | `verified-runtime` | The kernel replaces the sole direct call with a register/flags-preserving trampoline, emits `before`, invokes the original callee, emits `after`, and resumes at `0x00108d43`. Both subscribed and unsubscribed runtime fixtures reached exact targets. |
| Default construction path | `verified-static-callsites` | The insufficient-funds helper reaches `TakeLoan` at `0x001257a8` after preparing debtor, zero, and the requested 64-bit amount. The shortfall handler can conditionally refund canceled construction amounts; this is why net treasury delta is not an independent named-transfer conservation signal. |
| Country state list `+0xe44` | `verified-current` | Current country update code walks node data at `+0`, next at `+8`, and terminates at null. State-creation callers maintain head `+0xe44`, tail `+0xe48`, and count `+0xe4c`; the seven-day runtime probe walked every reported state without a mismatch. |
| State constructor `0x000cdc60` | `verified-static-callsites` | Three callers allocate `0x290` bytes. The constructor initializes 64-bit slots `+0x258` and `+0x260` to zero. |
| State province vector `+0x48` | `verified-runtime` | Current code and the destination POP run agree that its four-byte elements are game-state province indices. All 346-661 destination provinces per creditor-bearing sample resolved to readable province POP vectors with no quality flag. |
| State savings `+0x258` | `verified-runtime` | Current code converts POP savings through the global `1000.0` scale when updating this 64-bit state value. Across 12 live destination samples, summed `POP+0x250 / 1000` tracked it within 19-116 raw state units. |
| State interest pool `+0x260` | `verified-runtime` | Save serialization names the field `interest`. `CBank::DistributeInterest` at RVA `0x000f5bf0` adds savings-weighted state shares to it, and live daily snapshots observed the resulting accumulation. No state-to-POP consumer has been found. |
| Province POP-list vector `+0x194` | `verified-runtime` | Current code indexes 16-byte list elements from `+0x194/+0x198`. The destination run walked exactly 13 list records for each of 346-661 resolved provinces without a mismatch. |
| POP size and linked-list next | `verified-runtime` | Creation allocates `0x288` bytes, current list walks use next at `+0x27c`, and the destination run walked 2,635-5,565 entries per creditor-bearing sample with exact list count/tail agreement. |
| POP savings `+0x250` and scale | `verified-runtime` | Current code divides this signed 64-bit value by fixed-point `1000.0` at RVA `0x00b0b168` when updating state `+0x258`. The live aggregate correlation confirms the scale while exposing small accumulated bookkeeping/rounding differences. |
| `CPop::GiveMoney` `0x0055a5f0` | `verified-runtime` | Seven direct callers establish `EAX=CPop*`, `ESI=CashFlowType`, stack-passed 64-bit amount, and `ret 8`. A reversible fixture observed exact writes to money `+0x180`, indexed cash flow `+0x1d8`, and total `+0x218`, then exact restoration. |
| Interest cash-flow index `7` | `verified-static-callsites` | The presentation path at RVA `0x00566440` reads POP `+0x210`, index 7 of `+0x1d8`, and formats it through localisation key `POP_DAILY_INTEREST`. The reversible fixture independently confirmed that selected slot 7 changed by the injected amount and restored; it did not execute the presentation path or snapshot all other slots. |

The state constructor is VA `0x004cdc60`, hence RVA `0x000cdc60` for the
preferred image base `0x00400000`. Earlier notes that treated `0x004cdc60` as
an RVA were incorrect.

## Historical read-only runtime probe

The retired `interest_probe` plugin wrote `interest_probe.csv` in the game
directory. It did not patch the historical post-update site or mutate game
state. Its bounded traversal and allocation logic now belong directly to the
production `interest_bug_fix` core and tests. The probe signature-checked the sole
`PayDailyInterest` call and emitted `before` and `after` observations around the
original callee. Each phase:

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

The final investigation build also snapshotted the same country at the existing
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

On July 31, 2026, Release artifacts were run from the unmodified supplied
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

## Retired historical GiveMoney probe

The retired `pop_money_fixture` probe produced the evidence below. Its source,
CMake target, and manifest are no longer available in this repository. The
probe waited for a structurally valid destination POP, snapshotted four 64-bit
fields, invoked `CPop::GiveMoney` with cash-flow index 7 and raw amount `+1000`,
snapshotted again, invoked `-1000`, and verified exact restoration.

Run `5ee018ab-f95f-4bc5-962e-d5cf5c968260` used only `campaign_runner` and this
probe from the unchanged supplied save. At raw date `59883432`, the selected
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
Subsequent static analysis identified the split: banks whose 64-bit `+0x10`
value is at most 33 add pending interest back to `+0x10`; banks above that
threshold allocate it across states. State `+0x260` therefore intentionally is
not an exact representation of all cleared creditor interest and must not be
used as the production payout total without reproducing that policy.

The first production design therefore derived each payout from the exact
per-destination bank delta across `PayDailyInterest`. Later static and runtime
evidence below established that this bypasses native bank recapitalization and
the intended bank-to-state allocation stage. Both runs preserved the source-save
hash and ended paused and responsive.

After adding the 512-country cap and extending callback timing over the global
scans, one-day smoke run `f3416e70-4f81-46be-b389-6c3231ba60e9` produced all
271 ordered pairs with both global snapshots, no flags or drops, and the same
unchanged source-save hash. The first and final global scans now appeared in
`collection_us` at 3,376 and 3,370 microseconds respectively instead of being
excluded from the reported callback cost.

## Native state-interest pipeline

`CBank::DistributeInterest` begins at VA `0x004f5bf0` (RVA `0x000f5bf0`). Its
sole direct caller in the supported executable is the post-country loop at VA
`0x00685f0f`, which loads every `CCountry+0xe88` bank after the country daily
updates. The function reads the bank owner at `+0x08`, pending interest at
`+0x20/+0x24`, and a denominator or reserve at `+0x10/+0x14`.

For a positive pending amount, the native branches are:

1. When bank `+0x10/+0x14` is at most the integer conversion of `33.268`, add
   the pending amount back to that bank field.
2. Otherwise walk `CCountry+0xe44`, weight the original pending amount by each
   state's `+0x258/+0x25c` savings, clear the low 15 product bits before
   dividing by bank `+0x10/+0x14`, clamp against the remaining pending amount,
   and add the share to state `+0x260/+0x264`. The multiply and divide helpers
   operate on signed 64-bit values, and division truncates toward zero.
3. Clear bank `+0x20/+0x24` on every return path.

The state loop maintains a remaining amount initialized to the positive pending
amount. For each state it calculates a rounded share from the original pending
amount, not from the remaining amount. A share no larger than the remaining
amount is added to the state and subtracted from the local remainder. A larger
share is clamped to the complete remainder, but that branch does not zero the
local remainder before continuing the linked-list walk. Native safety therefore
depends on the bank denominator and state-savings weights preventing the clamp
before the final state.

The function does not transfer a positive final remainder anywhere before it
clears bank `+0x20/+0x24`. Consequently the distributable branch has no exact
conservation guarantee: fixed-point truncation can discard a residual, while a
denominator smaller than the traversed savings total can reach the clamp before
the final state and over-allocate. A replacement must consume the state-pool
increments actually produced by native code rather than reimplementing this
formula or assuming that their sum equals the bank's pending amount.

The benchmark save independently serializes the adjacent state fields as
`savings` and `interest`. A complete direct-displacement scan found state
interest references for construction, copying, save parsing, save formatting,
and this bank-to-state writer. It found no gameplay subtraction, clear, or POP
payout consumer. This negative static result does not exclude an aliased
indirect access, so it was checked against native POP cash-flow telemetry.

Read-only run `96be486d-b7b3-4943-b423-49929ff8ee77` advanced the unmodified
benchmark save for seven exact days with `world.economy.credit` and
`pop.cashflow.aggregate` capture, without the interest-fix plugin. Its 30,275
records had no sequence gap or drop. Aggregate state interest progressed
`0, 0, 1,268, 2,817, 4,560, 6,481, 8,586`. The native POP cash-flow hook emitted
12,805 component records, including 1,135 bank records, but zero records for
interest index 7. The trace SHA-256 was
`cc5de77502f92fa4c9faf7a863c12ca9cc469c687b8f3ed7580b1cf73d6b6b57`, and the
source save remained unchanged.

The supported executable therefore implements creditor-to-bank and
bank-to-state interest stages, but current static and runtime evidence finds no
state-to-POP stage. The replacement should preserve native bank
recapitalization, discard serialized state-interest balances when a campaign
session is first observed, and discard retained failed-payout balances before
the next native daily bank-distribution pass. Initialization must preflight the
complete bounded state traversal and enable payouts only after every pool has
been cleared successfully. Each subsequently observed
complete state pool should be distributed among that state's positive-savings
POPs. Exact largest-remainder allocation in POP money scale preserves each
native state total despite the independently observed difference between stored
state savings and recomputed POP savings.

The state pool should be cleared only after a complete checked payout. A failure
before the first POP mutation retains the pool for the remainder of that pass.
Any partial mutation or postcondition failure latches payouts off through the
same pass: native `CPop::GiveMoney` calls across multiple POPs are not atomic, so
retrying the full retained pool could double-pay POPs already mutated. The next
daily pass discards any retained pool before re-enabling payouts.

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
contract defines the production allocator behavior:

1. The payout total is the complete native state interest pool multiplied by the
   verified `1000:1` state-to-POP money scale.
2. Only destination POPs with strictly positive stored savings participate.
   Zero or negative savings receive zero.
3. Each base payout is the integer floor of `total * POP savings / summed
   positive POP savings`.
4. Remaining single raw POP-money units go to the largest fractional remainders.
   Equal remainders use stable province/list/POP traversal order.
5. These conditions produce no mutation:
   - a nonpositive transfer;
   - no eligible savings;
   - insufficient bounded scratch space;
   - savings-sum overflow;
   - payout-scale overflow; or
   - multiplication overflow.

   The implementation deliberately rejects an extreme value instead of
   introducing floating-point or platform-dependent arithmetic.
6. Every payout is computed before the first `CPop::GiveMoney` call. A
   production plugin must verify that their exact sum equals `transfer * 1000`
   before applying any entry.

This largest-remainder policy is deterministic and exactly conservative in POP
money units relative to the native state pool. It does not require recomputed
POP savings to equal the drifting state `+0x258` aggregate. Win32 Release unit tests cover exact
conservation, remainder ordering and ties, nonpositive savings, an empty
eligible set, and conservative overflow rejection. No production mutation uses
the allocator unless the separate `interest_bug_fix` manifest is selected.

## Optional production fix

`interest_bug_fix` subscribes around the sole post-country
`CBank::DistributeInterest` call at RVA `0x00285f0f` and is disabled unless
explicitly selected. It conflicts with historical `v2up`.

### Production mutation contract

The callsite trampoline preserves registers and flags, dispatches a callback
before native distribution only at loop index zero, and invokes the original
function exactly once. It dispatches an after callback whenever the bank entered
the native call with positive pending interest and marks whether its pre-call
reserve selected the mapped state-distribution branch rather than bank
recapitalization. The fix ignores recapitalization callbacks before game-state
collection; other event subscribers retain the complete boundary.
On the first trusted callback for a newly observed campaign session, the fix
preflights every bounded state pool, discards serialized balances, and enables
payouts only after all clears succeed. A failed or incomplete payout schedules
the same cleanup before the next daily pass; a successful pass already leaves
every consumed pool zero. Native recapitalization and bank-to-state allocation
run unchanged.

After native distribution, the fix first scans copied state candidates without
walking POPs. Countries with no positive state pool return immediately. For a
country with interest it:

1. traverses at most 512 states and 4,096 province references, validates bounded
   province resolution and duplicate province identity for every state, and
   walks at most 100,000 POPs only in states with positive pools while rejecting
   malformed traversed containers and duplicate identities;
2. allocates each complete positive state pool among that state's positive-savings
   POPs and requires the exact payout sum to equal the pool multiplied by 1,000;
3. preflights every `CPop::GiveMoney` range and arithmetic result before the first
   native call;
4. invokes cash-flow index 7 in deterministic state/province/list/POP order and
   verifies each POP's exact money, slot 7, total-flow, and unchanged-savings
   postconditions; and
5. clears the unchanged state pool only after every POP postcondition succeeds.

A failure before the first POP mutation retains the pool for the rest of that
daily pass. Any partial mutation, native postcondition failure, or failed pool
clear disables later payouts in the same pass because retrying the complete pool
could double-pay a POP. The next first-country boundary performs a complete
checked discard before resetting that latch. Callbacks perform no file I/O; a
bounded worker writes `interest_bug_fix.csv` only in diagnostic mode.

The exact batched CSV header is:

```text
date_raw,country,state_id,status,flags,state_count,province_count,pop_count,paid_pop_count,verified_pop_count,state_pool_raw,payout_raw,discarded_raw,allocation_status,callback_us,health_telemetry_result,value_telemetry_result,dropped_results
```

Possible statuses are `initialized`, `paid`, `collection_failed`,
`no_eligible_savings`, `allocation_overflow`, `allocation_invalid`,
`pop_balance_overflow`, `pop_not_writable`, `duplicate_pop`,
`mutation_unavailable`, `mutation_precondition_changed`, `partial_mutation`,
`postcondition_failed`, `conservation_failed`, and `campaign_disabled`.
`allocation_status` preserves the allocator's exact result. `dropped_results` is
the cumulative bounded result-queue drop count when the row is written.
Telemetry result codes follow the C ABI: 0 unavailable, 1 filtered, 2 accepted,
3 dropped, and 4 invalid.

### Historical direct creditor implementation

The runtime evidence below through commit `34b750f` describes the superseded
creditor-bank implementation. It remains provenance for mappings, payout ABI,
and exact mutation checks, not acceptance evidence for the state-pool design.

Sixty-day regression run `b88ce485-ed56-4634-9bb0-0927b0a83117` exercised the
ordinal-zero form after the diagnostic correction. It reached the exact target
with zero telemetry gaps, drops, or writer failure and produced 181 `paid` plus
322 `no_transfer` health records, all with zero flags. Every paid result had a
paired value record, both monthly economic snapshots were complete, and the
source-save hash remained unchanged.

Fix-enabled observer smoke run `5095e066-93e8-4faf-9060-dfa0199becd8`
completed two exact days with zero gaps, drops, or writer failures. Sweden (SWE)
traversed 346 provinces and 2,635 POPs in 9,000 microseconds; Sardinia-Piedmont
(SAR) traversed 615 provinces and 5,077 POPs in 17,624 microseconds. Both
health/value pairs were accepted, all flags were zero, all 753 paid POPs passed
immediate postconditions, and the source save remained unchanged.

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
in `telemetry.md`.

Opt-out run `1759f606-f8b6-4588-aad0-31025e74fd60` repeated two days without
selecting `interest_fix`; the prior fix CSV retained the same hash, timestamp,
and length, proving the plugin did not load. Every run preserved the source-save
SHA-256.

## Batched ten-year fix result

This run predates the production plugin and output rename from `interest_fix`
to `interest_bug_fix`; the old artifact name below identifies the measured file.

Final run `c99a16f5-41e3-40ef-8f75-f6cc835f3aee` used commit `d8458bd`, the
unchanged supplied save, observer mode, speed 5, and 3,650 exact days. The
11,571-record trace had SHA-256
`0acbe58e187c5d4950ec03908294d8b653c678b6faf0e373762c6588f7e7b7fc` and zero
sequence gaps, telemetry drops, or writer failures. The source save retained
SHA-256 `f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

The closed `interest_fix.csv` had SHA-256
`a077b00228dbd26c15961b08a869e8a89ba87a60e62b13dffc920c5161027b0e` and these
complete results:

| Measure | Result |
| --- | ---: |
| Daily summaries | 3,650 `day_summary`; 0 `day_partial` |
| Successful recipient payouts | 132,351 |
| Rejected debtors / recipient failures | 0 / 0 |
| Named destination-bank transfer | 2,003,503,700 |
| Domestic / foreign transfer | 343,963,354 / 1,659,540,346 |
| Exact POP-money payout | 2,003,503,700,000 |
| POP payout instances passing postconditions | 8,635,406 |

Every recipient payout equaled its transfer multiplied by 1,000; every paid POP
passed money, interest-flow, total-flow, and unchanged-savings postconditions.
All 3,650 daily transfer totals in the CSV exactly matched their structured
`interest.fix.value` records. An earlier treasury-derived Private Investor
measurement is not retained: conditional construction-cancellation refunds can
occur inside the shortfall handler called by `PayDailyInterest`, so net treasury
movement is not a conservation check. All
daily summary flags were zero, and every health/value telemetry publication was
accepted.

The successful run had no `no_eligible_savings` result. A prior deterministic
simulation path did encounter this guard for countries with POPs but no positive
stored savings. The allocator correctly performs no mutation in that case:
there is no current depositor weighting with which to assign the bank gain, and
inventing a recipient would violate the documented allocation model.

The matched no-fix baseline and full performance/economic comparison are
recorded in `telemetry.md`.

## Checked boundary final smoke test

Run `c94800f7-74a8-4013-bfcc-c12e96776d52` used the final Release artifacts with
kernel-retained executable identity, `CurrentGameSession`, checked interest
mutation, campaign runner, and the telemetry `CPop::GiveMoney` hook. All three
plugins loaded after the in-process identity check. The benchmark advanced from
raw date `59883384` to `59883552`, paused with zero overshoot, drained telemetry,
and exited through the native bounded-run path.

The trace contained 3,684 ordered records with zero gaps, drops, or writer
failures. Its 24 paid recipient rows reconciled raw transfer `92,860` to exact
POP payout `92,860,000`; all 4,911 paid POP postconditions passed and every
allocation status and daily flag was successful. The source save retained
SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

## Historical renamed-plugin smoke test

This run predates removal of three unused hooks from the active mapping catalog.
Run `b4673a6f-11b5-4714-bf00-5fbcfc7e449b` exercised the installed
`interest_bug_fix` plugin with observer mode, speed 5, and a seven-day exact
benchmark. The supported executable matched all 58 then-active signatures; the
current catalog contains 55. The run
advanced from raw date `59883384` to `59883552`, paused with zero overshoot, and
produced a gap-free 45-record trace with zero drops or writer failures.

All seven `interest.fix.health` daily summaries had zero flags and rejected
debtors. The final day distributed raw transfer `20,468` as exact POP payout
`20,468,000` across 1,034 verified POPs, with no dropped fix results. The source
save retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

## Post-extraction smoke test

Run `e28f1cc5-e357-433e-a386-0a1d3d8f2ee7` exercised the installed telemetry,
campaign runner, and `interest_bug_fix` plugins after shared game-state readers
were extracted. The supported executable matched all 64 active signatures. The
seven-day benchmark reached raw date `59883552` exactly and produced 1,970
records with zero sequence gaps, drops, writer failures, or Victoria error-log
entries.

All seven `interest.fix.health` summaries reported `flags=0`. Six days performed
nonzero transfers; the final day reconciled raw transfer `20,472` to exact POP
payout `20,472,000` across 1,115 post-write-verified POPs. The source save
retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

## Checked batch performance

Run `c6e8e7f2-2637-42df-963e-18496bda36b0` exercised the production
`interest_bug_fix` and campaign runner for 3,650 exact days with diagnostics and
telemetry disabled. Callback-scoped batch mutation amortized session, signature,
and memory-region validation while retaining complete preflight and immediate
per-POP postconditions. The focused collector omitted economy aggregates not
consumed by payout policy, and zero-destination debtors completed before their
otherwise empty AFTER processing.

The process exited through the bounded native path after 366.829 seconds, or
9.95 game days per second. Repeated full runs of the optimized path ranged from
9.93 to 10.09 game days per second, so 10 is within run-to-run variation rather
than a guaranteed floor. The previous production implementation on the same
fixture took 496.4 seconds (7.35 game days per second). The source save and the
pre-existing diagnostics CSV remained byte-for-byte unchanged.

## State-pool production validation

Seven-day run `824f4a15-9f09-44a3-9c3f-908f5c8af6a4` exercised the production
state-pool implementation with diagnostics enabled. All seven initialization
passes covered 597 states. The run paid 48 complete state pools through 244
verified POP writes with no quality flag, drop, mutation failure, or retained
successful pool. Every paid row satisfied `payout_raw = state_pool_raw * 1000`.

Matched 3,650-day lifecycle-only runs measured the cost from the same unchanged
benchmark save:

| Configuration | Run | Game days/s | Elapsed |
| --- | --- | ---: | ---: |
| No interest fix | `8cba1f79-f685-4827-91e9-5c27ba4f29c0` | 9.86682 | 369.927 s |
| State-pool fix | `20053891-efd7-49e5-bc85-4c9544d46525` | 6.50531 | 561.080 s |

Both traces completed at the exact target with zero sequence gaps, drops, or
writer failures. Their SHA-256 values were respectively
`6f9b00de663e4c995c67c2337036a4d56f055d2160a5a57522a28dae7f5486cd` and
`e971f0bb23984dcaf41ff21e32213060a73c829d4d3449e2b7673d251350eec7`.
The fix reduced throughput by 34.1 percent, or increased benchmark elapsed time
by 51.7 percent. This is a material production cost, not telemetry loss: the
fix performs checked native POP writes that the unmodified game omits.

The source save retained SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`
through all runs.

## State-pool performance optimization

Seven-day diagnostics run `ecbd9f74-c371-4995-b14c-3b7d0ad7ecf0` validated the
optimized path. It performed one full session cleanup rather than seven daily
campaign scans, paid 47 state pools through 180 verified POP writes, and had no
failure, conservation error, or dropped result. Positive-pool country callback
time totaled 12,898 microseconds, down from 29,829 microseconds before selective
POP traversal on the same fixture shape.

Final production run `1c0e4bc4-227e-41d6-861b-e308077c34ae` completed 3,650 exact
days with diagnostics and telemetry disabled. Complete launcher-to-process-exit
wall time was 339.478 seconds, a conservative 10.7518 game days per second that
also includes launch, save loading, campaign entry, and shutdown. The matching
no-fix Smedley configuration, run `1584444b-7c3f-4b3f-ad3e-70db4bf4d893`, took
302.085 seconds or 12.0827 game days per second. The optimized fix therefore
retained 89.0 percent of paired baseline throughput.

The final path performs full campaign cleanup only for a new session or after a
failed payout, ignores native recapitalization callbacks before collection,
walks POP lists only for positive state pools, validates country membership once
per callback, uses generation-tagged per-bank identity storage, and amortizes
signature and session checks across each prepared country. Checked writable
state spans, native `CPop::GiveMoney`, immediate POP postconditions, exact
state-pool conservation, and clear-after-success ordering remain active.

## Century acceptance run

The first 36,500-day diagnostic run exposed 5,496 valid second-bank payouts that
the initial day-scoped POP identity guard rejected. The same state had received
and cleared one pool, then native distribution from a later bank created a new
pool for it during the same daily bank loop. Commit `c357fd9` narrowed the guard
to one bank callback. Duplicate POPs across states in that callback remain
rejected by both collection and mutation preflight, while a later independently
created pool can pay the same depositor again.

Run `73859a8f-c876-45ea-9622-4b9a5f61e319` repeated the complete 36,500-day
fixture after that correction with campaign runner, `interest_bug_fix`, and
diagnostics enabled. It reached the exact target and exited through the bounded
native path in 5,104.095 seconds. The 7.1511 game days per second includes
diagnostic formatting and writing a 938,029,562-byte CSV, so it is not a
production throughput benchmark.

The closed CSV had SHA-256
`e1f26d99fabc3e5413f8a9ac84e7a496cb2fcd3c186d183698731b9ab8db322d`.
Its 12,945,293 data rows covered 36,499 completed native bank-distribution dates;
the campaign runner pauses at the final target before that date's bank pass.

| Outcome | Count or raw total |
| --- | ---: |
| Successful state pools | 11,695,674 |
| Verified POP writes | 144,643,153 |
| Paid state-pool raw total | 122,721,581,891 |
| Exact POP payout raw total | 122,721,581,891,000 |
| `no_eligible_savings` pools | 1,213,120 |
| Retained no-savings pool raw total | 38,472,078,485 |
| Subsequent initialization discard raw total | 38,470,779,489 |
| Full cleanup passes | 31,749 |

Every paid row satisfied `paid_pop_count = verified_pop_count` and
`payout_raw = state_pool_raw * 1000`, with successful allocation status. There
were zero quality flags, result drops, collection failures, duplicate-POP
failures, overflow failures, unavailable mutations, changed preconditions,
partial mutations, postcondition failures, or conservation failures. The only
non-paid outcomes were the documented `no_eligible_savings` guard; those pools
were retained and discarded before a later pass. The small difference between
retained and discarded totals consists of pools created near the final target
without a subsequent pass.

The source save remained unchanged at SHA-256
`f24f40665745b5ff01ac3ed84b138efb54c634fb1c9a69ef3c06a75617295d3e`.

## Remaining validation

1. Map the complete default accounting and the missing
    bank-cash/world-money categories before claiming effects on bankruptcy or
    total money supply.
