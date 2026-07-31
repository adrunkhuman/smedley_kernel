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
| `CCountry::DailyUpdate` `0x00108590` | `verified-static-callsites` | Its only direct call to `PayDailyInterest` is at `0x00108d3e`, with the country pointer already on the stack. |
| `CCountry::PayDailyInterest` `0x00123c30` | `verified-static-callsites` | Reads the creditor vector at country `+0xe8c`; credits bank `+0x20` through the bank pointer at country `+0xe88`; returns with `ret 4`. |
| Country state list `+0xe44` | `verified-current` | Current country update code walks node data at `+0`, next at `+8`, and terminates at null. State-creation callers maintain head `+0xe44`, tail `+0xe48`, and count `+0xe4c`; the seven-day runtime probe walked every reported state without a mismatch. |
| State constructor `0x000cdc60` | `verified-static-callsites` | Three callers allocate `0x290` bytes. The constructor initializes 64-bit slots `+0x258` and `+0x260` to zero. |
| State `+0x48` vector candidate | `provisional` | Recovered from `794c98e`; the seven-day runtime probe accepted a stable 2,311 readable four-byte elements per daily world pass, but did not read or correlate them as province IDs. |
| State `+0x258/+0x260` economic labels | `provisional` | Their 64-bit shape is statically corroborated, but `savings_in_bank` and `interest_payments` semantics still require runtime correlation. |
| Province POP-list vector `+0x194` | `verified-static-callsites` | Current POP code indexes 16-byte list elements from the vector begin pointer at `+0x194`. |
| POP size and linked-list next | `verified-static-callsites` | Creation allocates `0x288` bytes and current list walks use next at `+0x27c`. |
| POP savings candidate `+0x250` | `verified-static-callsites` | Current POP economy code performs signed 64-bit transfers at `+0x250/+0x254`; the savings label remains provisional. |
| `CPop::GiveMoney` `0x0055a5f0` | `verified-static-callsites` | Fifteen direct callers agree on `EAX=CPop*`, `ESI=CashFlowType`, a stack-passed 64-bit amount, and callee cleanup with `ret 8`. |
| Interest cash-flow index `7` | `historical-unverified` | Recovered from `794c98e`; runtime correlation has not established the label. |

The state constructor is VA `0x004cdc60`, hence RVA `0x000cdc60` for the
preferred image base `0x00400000`. Earlier notes that treated `0x004cdc60` as
an RVA were incorrect.

## Read-only runtime probe

The opt-in `interest_probe` plugin writes `interest_probe.csv` in the game
directory. It does not patch the historical post-update site and never mutates
game state. Each pre-update country callback:

- validates readable pages before copying provisional structures;
- caps traversal at 512 states and 1,024 four-byte vector elements per state;
- records state-list count agreement, candidate state totals, creditor count,
  treasury, and bank interest;
- enqueues one fixed-size sample in a 1,024-slot single-producer queue; and
- reports dropped samples and callback time while a worker performs all CSV I/O.

`flags=0x0` means the structural checks passed. Any nonzero flag makes the row
unsuitable for semantic promotion. The two state columns deliberately include
`candidate` in their names because this probe does not establish economic
meaning by itself.

## Supplied-save runtime evidence

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

## Required evidence before mutation

1. Correlate the candidate state totals with country creditor and bank-interest
   movement at the correct pre/post-update boundary.
2. Add bounded POP-level sampling and prove that POP `+0x250` sums use the same
   scale as state `+0x258`.
3. Invoke `CPop::GiveMoney` only in a disposable controlled fixture and verify
   POP money plus cash-flow bucket `7` independently.
4. Define integer allocation, rounding, remainder ownership, conservation, and
   zero/negative-value behavior before implementing an independently selectable
   fix.
