# Creditor POP interest investigation

Victoria II charges debtor countries interest and credits a country bank, but
historical Smedley commit `794c98e` claimed that the corresponding creditor POP
payments were omitted. That implementation is an investigation lead, not a
supported fix: it depended on a generated 469,301-line layout, mutated
provisional fields, and used floating-point proportional allocation without a
conservation test.

## Current static evidence

All addresses below are RVAs in the cataloged Victoria II 3.04 executable.

| Evidence | Status | Basis |
| --- | --- | --- |
| `CCountry::DailyUpdate` `0x00108590` | `verified-current` | Its only direct call to `PayDailyInterest` is at `0x00108d3e`, with the country pointer already on the stack. The checked kernel trampoline invoked the original call and emitted 1,897 ordered runtime boundary pairs. |
| `CCountry::PayDailyInterest` `0x00123c30` | `verified-runtime` | The paired boundary run observed current-country treasury reductions on all 12 calls with creditor entries. Its sole direct caller and `ret 4` cleanup are statically established. |
| Creditor destination bank path | `verified-runtime` | `PayDailyInterest` reads the current country creditor vector at `+0xe8c`, resolves a destination country from each creditor tag/ordinal at `+0x8`, and credits destination bank `+0x20`. All 12 creditor-bearing fixture calls resolved every entry and conserved the exact debtor treasury loss in the summed destination-bank gain. |
| Creditor `+0x8/+0x10/+0x18/+0x20` | `verified-runtime` | Static code reads the tag/ordinal at `+0x8`, multiplies the 64-bit `+0x18` value by the 64-bit `+0x10` value in its payment calculation, and updates the byte at `+0x20` on payment. The destination run validated every tag/ordinal and observed `+0x20 == 1` for every paid entry; the economic names on the two 64-bit fields remain candidates. |
| Destination bank `+0x20` | `verified-runtime` | The static add target and all 12 exact before/after pairs agree. Cumulative raw gain was `+87,242`, exactly negating debtor treasury delta `-87,242`. Other historical `CBank` fields remain unverified. |
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
- caps creditor destination traversal at 64 entries, validates each destination
  tag and ordinal against the bounded game-state country vector, and records
  candidate creditor values plus aggregate destination bank/state values;
- attempts at most 4,096 destination province resolutions, validates at most 128
  16-byte POP-list records per province, and attempts at most 100,000 POP reads across the
  complete callback while retaining raw and `/1000` savings totals;
- retains only the copied `before` POD until `after`, then atomically enqueues
  one fixed-size pair in a 1,024-slot single-producer queue; and
- reports dropped pairs and collection time while a worker performs all CSV I/O.

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

## Required evidence before mutation

1. Correlate the later destination-bank clearing path with state `+0x260`
   assignment before treating that field as distributable interest.
2. Define integer allocation, rounding, remainder ownership, conservation, and
   zero/negative-value behavior before implementing an independently selectable
   fix.
