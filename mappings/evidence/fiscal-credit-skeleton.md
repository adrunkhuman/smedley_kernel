# Fiscal, banking, and credit mapping skeleton (issue #29)

Breadth-first skeleton. Each section names the best-known engine boundary for
one #29 requirement, its evidence level, and the concrete next step (so a later
session can extend one section without re-exploring the others). Addresses are
module-relative; the supported executable is HOD 3.04 (SHA-256
`62d48c20...`), preferred base `0x00400000`. Evidence levels follow `mappings/`
conventions.

Shared anchor facts:
- Treasury is a signed 64-bit pair at `CCountry+0xe78` (low) / `+0xe7c` (high).
  `CCountry+0xe88` is the bank pointer, `+0xe8c` the creditor vector.
- Static xref scan found 89 references to `+0xe78`; the treasury **add**
  (income/source) sites and **sub** (expense/sink) sites below are the fiscal
  boundary candidates. Not all are classified by economic kind yet.
- The `money` event effect shares treasury raw units (values to ~411G), so it is
  the right lever to force budget/bankruptcy experiments.

## POP taxes and tariffs

| Aspect | Value |
| --- | --- |
| Boundary | POP income tax and trade tariffs collect into treasury. Static budget-source `add` clusters below are where these land; the per-POP tax amount begins at POP income fields (POP `+0x250` savings is separate). |
| Evidence | `provisional` (candidates only; no runtime attribution). |
| Next | Disassemble the `add` clusters (esp. `RVA 0x0050xxxx`, `0x0053xxxx`) to identify the POP-income → tax → treasury chain and name a tax field without shape-based inference; correlate a live country's budget screen tax line with a treasury delta. |

## Government budget sources and sinks

Treasury `add` (inflow/source) clusters, `verified-static-callsites`:

- `RVA 0x00508c6d 0x00508ca4 0x00508cde 0x00508d1a 0x00508e6b 0x0050c262`
- `RVA 0x00538404 0x00538438 0x005388bc 0x005388eb 0x0053e567 0x0053e69f 0x0053e781 0x0053e7ff`
- `RVA 0x005235a4 0x00523697 0x00523819` (pre-interest daily income)
- `RVA 0x0048893c 0x00488add`

Treasury `sub` (outflow/sink) sites:

- `RVA 0x005239c0 0x00523a35` (daily expense deduction)
- `RVA 0x005257fa` (money mutator sink)
- Interest debit inside `PayDailyInterest` (`RVA 0x00123c30`): **verified-runtime**
  (reproduced, e.g. SWE `-32,814` raw/day).
- Bankruptcy refund `add` at `RVA 0x005246f7` (inside the `0x001241f0` handler).

Assignment sites include `RVA 0x00505ec2` which writes the raw constant
`32768000` (`1000.0` at 15 fractional bits) to treasury (initialization/reset).

| Next | For each `add` cluster, determine the source kind (POP income tax, tariff, gold conversion, interest) by reading the function that feeds the added value; then promote one to `verified-runtime` with a budget-line correlation. |

## National bank (`CBank`, via `CCountry+0xe88`)

| Field | Offset | Evidence |
| --- | --- | --- |
| country (owner) | `+0x08` | static (read by `CBank::DistributeInterest`), candidate |
| money | `+0x10` | verified-current; runtime-observed to accumulate (SWE bank `15.5M -> 30.4M` in 30 days) |
| total_lent | `+0x18` | verified-current; runtime-observed (tracks money then plateaus) |
| interest_payments | `+0x20` | verified-runtime (PayDailyInterest credits it; 12 exact pairs) |

Domain (player-facing) semantics, for framing: POPs with excess income deposit
savings into the bank (`+0x10` grows); a borrower prefers its own bank first
(domestic debt, already mapped via `TakeLoan`); repayment returns money to the
lender; interest accrues to depositors but the engine has **no state-to-POP
consumer** (see interest-payout evidence). `CGameState+0xb5c` is the cash/loan
global read by `TakeLoan` and is a lead for the NBD→interest-rate link
(`LOAN_BASE_INTEREST`).

| Next | Verify `+0x08` is the owning country (resolve its tag/ordinal and cross-check the visible bank screen); find the POP-savings → bank deposit field/callsite; map repayment as `total_lent` comes down. |

## Loan lifecycle (origination / interest / repayment / default)

- Origination: `CCountry::TakeLoan` `RVA 0x00122910` — **verified-static-callsites**
  (domestic self-tagged creditor via `TakeLoanFrom`; Shadowy Financiers fallback;
  bankruptcy construction reach at `RVA 0x001257a8`). Detailed in interest-payout.
- Interest: paid daily in `PayDailyInterest` (`RVA 0x00123c30`) — **verified-runtime**.
- Default/bankruptcy: shortfall handler `RVA 0x001241f0` (static; treasury reset
  observed at runtime but exact invocation unbracketed).
- Repayment: **OPEN** — the boundary where loan principal returns to the lender
  (`total_lent` down, creditor vector shrinks) is not yet located.

| Next | Hook the daily pass and watch `total_lent` / creditor vector for a repayment transition; identify the repayment callsite. |

## Conservation, units, rounding, residual

- Units: treasury, state `+0x258/+0x260`, bank `+0x10/+0x18/+0x20` are signed
  64-bit raw values; POP money/`state+0x258` use a `1000.0` scale link; treasury
  `+0x78` uses `32768000` = `1000.0` constant for the write at `RVA 0x00505ec2`.
- Established limits: `CBank::DistributeInterest` has no exact conservation
  guarantee (fixed-point truncation residual; over-allocation when denominator
  small); the state interest pool `+0x260` has no state-to-POP consumer;
  bankruptcy makes net treasury deltas unsuitable as named-transfer conservation
  signals.
- Residual: **OPEN** — an explicit expression for unmapped treasury paths
  (POPS wages, state/factory spending, subsidies) is not yet written.

## Status

- Acceptance minimum met (one fiscal + one credit boundary, runtime evidence).
- Breadth skeleton now covers every #29 requirement with a named boundary or an
  explicit open item and next step.
- Concrete verified deltas this session: treasury fiscal sink (SWE `-32,814`),
  destination-bank `+0x20` credit, forced-bankruptcy treasury reset, bank
  `+0x10/+0x18` accumulation.
- Still open: POP tax/tariff field attribution, budget-source kind per `add`
  cluster, bank ownership/deposit provenance, loan repayment, conservation
  residual.
