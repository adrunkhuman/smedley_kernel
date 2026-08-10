# Fiscal, banking, and credit mapping skeleton (issue #29)

Breadth-first skeleton. Each section names the best-known engine boundary for
one #29 requirement, its evidence level, and the concrete next step (so a later
session can extend one section without re-exploring the others). Addresses
labeled RVA are module-relative; addresses labeled VA use the supported
executable's preferred base `0x00400000`. The supported executable is HOD 3.04
(SHA-256 `62d48c20...`). Evidence levels follow `mappings/` conventions.

Shared anchor facts:
- Treasury is a signed 64-bit pair at `CCountry+0xe78` (low) / `+0xe7c` (high),
  at a `1/32768` (£) fixed-point scale. Scale **verified-runtime**: live SWE
  on-screen treasury matched the probe raw, with `raw / 32768` equal to the
  displayed pounds on two consecutive days (Jul 13 `4441.5`~`4446.8`, Jul 14
  `4499.6`~`4501.6`), and the net daily change (`+58.1 £`) consistent with the
  budget's tax+tariff income minus spending.
  `CCountry+0xe88` is the bank pointer, `+0xe8c` the creditor vector.
- Static xref scan found 89 references to `+0xe78`; the treasury **add**
  (income/source) sites and **sub** (expense/sink) sites below are the fiscal
  boundary candidates. Not all are classified by economic kind yet.
- A `money = -1000000000` event forced negative treasury in the retained probe;
  `-99999` did not. This establishes effective probe magnitudes, not a general
  event-effect scale.

## POP taxes and tariffs

| Aspect | Value |
| --- | --- |
| Boundary | Poor, middle, and rich tax receipts enter treasury at VAs `0x00508ca4`, `0x00508cde`, and `0x00508d1a`. Tariffs enter at VA `0x00488add`. |
| Evidence | Tax receipts `verified-runtime`; tariff receipt `verified-static-callsites`. |
| Identified mechanism | `CCountry+0xa0` tax settings `[0..2]` feed treasury and daily accounting `+0x00/+0x08/+0x10`. Run `767d2c76` matched SWE raw receipts `778031/424367/62756` exactly to poor/middle/rich serialized `tax_income` sums after `*32768/1000`. Trade settlement multiplies its computed base by `CCountry+0x1440` (`tariffs`), adds the result to treasury, and records it at accounting `+0x18`. |
| Open | Bracket the tariff add for a runtime budget-line correlation. The separate `0x00508c6d` accounting `+0x28` receipt and `0x0053e5xx` accounting `+0x40` family remain economically unidentified; neither is needed to identify the class-tax callsites. |

## Government budget sources and sinks

Treasury `add` (inflow/source):

- Daily per-entity income-loop VAs `0x0053e567 0x0053e69f 0x0053e781 0x0053e7ff`
  (entities: `+0xb8/+0xbc` income).
- Class-tax VAs `0x00508ca4 0x00508cde 0x00508d1a`:
  `verified-runtime`; the sibling `0x00508c6d` receipt remains unidentified.
- Tariff VA `0x00488add`: `verified-static-callsites` through
  `CCountry+0x1440` and accounting `+0x18` data flow.
- Other unclassified VAs `0x00508e6b`, `0x0050c262`, `0x005235a4`,
  `0x00523697`, `0x00523819`, and `0x0048893c`.

Treasury `sub` (outflow/sink):

- Loan-principal repayment function at RVA `0x001238d0`: debits treasury at VAs
  `0x005239c0` / `0x00523a35` while reducing creditor and lender-bank principal;
  `verified-runtime`, not a generic government-expense boundary.
- Money-mutator sink at VA `0x005257fa` with a zero clamp (VAs
  `0x00525806..0x00525810`).
- Interest debit inside `PayDailyInterest` (`RVA 0x00123c30`): `verified-runtime`
  (reproduced, e.g. SWE `-32,814` raw/day).
- Conditional construction-cancellation refund at VA `0x005246f7` inside the
  shortfall handler; this is not the general default treasury reset.

The `0x00508xxx` source path divides its upstream value by `2^15` before adding
to treasury. The upstream representation remains unresolved; this does not by
itself prove a scale different from treasury's 48.15 fixed point.

Assignment sites include VA `0x00505ec2` which writes the raw constant
`32768000` (`1000.0` at 15 fractional bits) to treasury (initialization/reset).

| Next | Runtime-correlate the tariff add and identify the remaining accounting `+0x20/+0x28/+0x38/+0x40` source families, especially gold and transfers. |


## National bank (`CBank`, via `CCountry+0xe88`)

| Field | Offset | Evidence |
| --- | --- | --- |
| country (owner) | `+0x08` | verified-runtime: reader resolves `+0x08` and confirmed, across all 4,065 boundary rows of run `5d5db709`, that each destination bank owner equals the destination country (same tag+ordinal, zero mismatches) |
| money | `+0x10` | verified-runtime: SWE save `bank.money=75894.56842` maps to raw `2486913218`; the POP deposit path adds the same amount to state savings and owner-bank money |
| money_lent | `+0x18` | verified-runtime: SWE save `bank.money_lent=21725.41159` maps to raw `711898287`; repayment subtracts matching creditor principal from this lender-bank field |
| interest_payments | `+0x20` | verified-runtime (PayDailyInterest credits it; 12 exact pairs) |

**National-bank balance has a per-state presentation candidate.** Live
observation: once deposits began,
the on-screen bank balance was `0.05`, split `0.03 + 0.02` across the two states
that had generated savings. This is consistent with the bank's displayed balance
being derived from `CCState+0x258` savings (already `verified-static-callsites` in
`interest-payout.md` for the `CBank::DistributeInterest` weighting), not any
single `CBank` field. The earlier UI mismatch does not invalidate the serialized
`money`/`money_lent` identities; exact aggregate-to-UI correlation remains open.

Domain (player-facing) semantics, for framing: POP savings are represented by
the per-state `+0x258` pool used for bank-interest weighting. The displayed
balance's matching per-state split is a strong, incomplete correlation.
A borrower prefers its own bank first (domestic debt, already mapped via
`TakeLoan`). Interest accrues to a state pool, but the engine has **no
state-to-POP consumer** (see interest-payout evidence). `CGameState+0xb5c` is a
cash/loan-global lead read by `TakeLoan` for the NBD→interest-rate link
(`LOAN_BASE_INTEREST`).

| Next | Bracket the deposit co-update at VA `0x00486f85` and establish the displayed bank-balance aggregation/rounding rule. |

## Loan lifecycle (origination / interest / repayment / default)

- Origination: `CCountry::TakeLoan` `RVA 0x00122910` — **verified-static-callsites**
  (domestic self-tagged creditor via `TakeLoanFrom`; Shadowy Financiers fallback;
  bankruptcy construction reach at `RVA 0x001257a8`). Detailed in interest-payout.
- Interest: paid daily in `PayDailyInterest` (`RVA 0x00123c30`) — **verified-runtime**.
- Default/bankruptcy: shortfall handler `RVA 0x001241f0` is **verified-runtime**
  as an invocation boundary with country/creditor identity; three calls returned
  without immediate treasury, principal, count, or own-bank mutation. Static
  flow applies default modifiers/notifications and conditionally refunds
  canceled constructions. A separate all-country bracket disproved
  `RemoveDebts(true)` as a deferred default path: it never ran for
  forced-shortfall SWE and its other direct caller is annexation.
- Repayment: `CCountry::RepayLoan` RVA `0x001238d0` and order caller RVA
  `0x0018beec` are **verified-runtime**. Run `3f63c64b` bracketed 12 calls; every
  requested amount exactly equaled both debtor treasury debit and creditor
  debt reduction. D01 -> ENG, BRZ -> USA, and SWE -> SWE identities were
  retained. No entry reached zero, so removal is still static-only.

| Next | Determine whether default intentionally preserves principal; separately locate the forced-treasury recovery source and verify modifier/prestige effects. |

## Conservation, units, rounding, residual

- Units: treasury, creditor interest/debt, and bank money/money-lent/interest are
  signed 48.15 fixed point (`raw / 32768`). Serialized tax arrays use a `1000`
  scale before conversion to treasury raw. POP money/state savings use the
  separately verified `1000.0` scale link.
- Established limits: `CBank::DistributeInterest` has no exact conservation
  guarantee (fixed-point truncation residual; over-allocation when denominator
  small); the state interest pool `+0x260` has no state-to-POP consumer;
  bankruptcy makes net treasury deltas unsuitable as named-transfer conservation
  signals.
- Tax conversion rounds down once per class, so each class contributes less
  than one treasury-raw unit of conversion residual. Repayment and the
  sufficient-funds interest transfer conserve exactly in raw units.
- Country treasury residual for an interval is defined as:

  `R_treasury = Δtreasury - (tax_poor + tax_middle + tax_rich + tariff + construction_refunds + named_other_receipts - named_budget_sinks - interest_paid - principal_repaid + loan_proceeds)`

  `R_treasury` is explicitly unavailable as a transaction category. It contains
  unmapped gold, transfers, stockpile/trade settlement, subsidies, and any
  unbracketed source/sink, including the unresolved default reset; it must not
  be presented as categorized money.
- Credit principal conservation at a verified repayment boundary is:

  `requested = -Δdebtor_treasury = -Δcreditor.debt`

  Both equalities were bracketed on all 12 calls. For a resolved nonzero-ordinal
  lender, static flow also reduces `lender_bank.money_lent`, clamped at zero;
  ordinal-zero Shadowy Financiers skip that mutation. The SWE self-loan daily
  snapshot independently matched the lender-bank reduction. No default
  principal-write-off term is currently established.

## Bounded implementation slices

| Slice | Boundary and record | Validation rule |
| --- | --- | --- |
| Class-tax telemetry | Read poor/middle/rich daily accounting slots after VAs `0x00508ca4/0x00508cde/0x00508d1a`; emit three raw amounts plus country/date. | Supported executable; all three signatures; valid country identity and accounting selector; observational only. |
| Tariff telemetry | Hook VA `0x00488add`; emit the credited raw amount and country/date. | Keep provisional until the first visible budget-line correlation; reject signature mismatch; no inferred gold/other category. |
| Bank/credit snapshot | Expose `CBank.money`, `CBank.money_lent`, `CCreditor.interest`, and `CCreditor.debt` with owner/destination identity. | Bounded vectors, readable spans, verified country ordinals, explicit unavailable values; do not sum assets and liabilities as money supply. |
| Repayment event | Bracket caller RVA `0x0018beec`; emit requested amount and before/after debtor debt/treasury. | Exact `requested = -Δtreasury = -Δdebt`; reject malformed identity or partial capture. |
| Default event | Emit shortfall-handler invocation with debtor and creditor identities; modifier outcomes remain optional/unavailable. | Do not claim debt write-off or treasury reset; the runtime boundary produced no synchronous accounting mutation. |

These are separate changes. None requires mutation or expands the default
telemetry schema until its own contract and fixture are reviewed.

## Open mapping items (research queue)

Verified API-port candidates: treasury, class-tax receipts, bank ownership,
bank money/money-lent, creditor interest/debt, repayment events, and default
handler invocation. Per-state displayed bank balance and default outcomes remain
research candidates.

Remaining unverified/unknown mappings, each with a path to verification. Priority
P1 = high value and tractable; P2 = needs the blocked loop/entity work; P3 = may
resolve as a negative.

| # | Mapping | Location | Current state | How to close | Priority |
| --- | --- | --- | --- | --- | --- |
| 1 | Bank-balance UI scale | `Σ CCState+0x258` | part-resolved | nail the display/ratio vs state sum | P1 |
| 2 | Tariff runtime amount | VA `0x00488add` | static callsite | bracket and correlate against budget tariff line | P1 |
| 3 | Gold/cash conversion -> treasury | — | unlocated | find the gold-to-treasury add path | P1 |
| 4 | Budget-source family | VA `0x0053e5xx` (`+0xb8/+0xbc`) | located, un-attributed | identify source composition | P2 |
| 5 | Accounting `+0x28` source | VA `0x00508c6d` | located, un-attributed | correlate nonzero FRA samples | P2 |
| 6 | Iterated-entity identity | global VA `0x01258738`; offset `(value + 0xd) << 4` | accounting selector | reify selector/object layout | P2 |
| 7 | Default principal outcome | handler RVA `0x001241f0` | invocation verified; no synchronous write-off | determine whether principal intentionally persists | P1 |
| 8 | Repayment entry removal | RVA `0x001238d0` | reduction verified; removal static | force a small debt to zero | P2 |
| 9 | Money-accounting field | `CCountry+0x40/+0x44` | unknown | find pool consumers | P2 |
| 10 | NBD -> interest-rate global | `CGameState+0xb5c` | lead only | resolve and link to `LOAN_BASE_INTEREST` | P2 |
| 11 | Treasury init/reset write | VA `0x00505ec2` (writes `32768000`) | located | identify the reset function | P2 |

## Status

- Acceptance minimum met (one fiscal + one credit boundary, runtime evidence).
- Breadth skeleton now covers every #29 requirement with a named boundary or an
  explicit open item and next step.
- Concrete verified deltas now include class-tax receipts, interest transfer,
  bank money/money-lent save correlation, and exact principal repayment.
- Tariff and deposit callsites are static; default invocation is runtime
  verified with a negative synchronous-accounting result. Principal treatment,
  modifier effects, and the forced-treasury recovery source remain unresolved.
- Conservation limits and an explicit treasury residual are defined above;
  unknown terms remain unavailable rather than inferred.
