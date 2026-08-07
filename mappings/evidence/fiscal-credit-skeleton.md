# Fiscal, banking, and credit mapping skeleton (issue #29)

Breadth-first skeleton. Each section names the best-known engine boundary for
one #29 requirement, its evidence level, and the concrete next step (so a later
session can extend one section without re-exploring the others). Addresses are
module-relative; the supported executable is HOD 3.04 (SHA-256
`62d48c20...`), preferred base `0x00400000`. Evidence levels follow `mappings/`
conventions.

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
- The `money` event effect shares treasury raw units (values to ~411G), so it is
  the right lever to force budget/bankruptcy experiments.

## POP taxes and tariffs

| Aspect | Value |
| --- | --- |
| Boundary | POP income tax and trade tariffs collect into treasury. Static budget-source `add` clusters below are where these land; the per-POP tax amount begins at POP income fields (POP `+0x250` savings is separate). |
| Evidence | `provisional` (candidates only; no runtime attribution yet). |
| Identified mechanism | Daily per-entity income loops add a 64-bit income field to treasury. The `0x0053e5xx-0x0053e8ff` loop reads the iterated entity's `+0xb8/+0xbc` and `add`/`adc` it to `CCountry+0xe78/+0xe7c`; the `0x00508xxx` loop reads `[ebx+0xa0]` `+4`/`+8` and divides by `2^15` (`shrd eax,esi,0xf; sar esi,0xf` at `0x00508e5c`) before adding. Both walk an entity list indexed via the global `[0x1258738]+0xd` (`+0xd` field scaled by `8`). |
| Open | Which entity the two loops iterate (POPs / goods / states) decides whether the credit is POP income-tax vs tariff/goods income. Next step: instrument the two treasury-add VAs (`0x0053e5xx`, `0x00508xxx`) with a read-only per-add logger, or resolve the entity array behind the global `[0x1258738]+0xd0`, then correlate with one live country's budget tax/tariff lines (a live SWE reading at 3.4 Feb 1836 confirmed the rates but not the per-loop split, because net treasury merges income, spending, loans, and gold). |

## Government budget sources and sinks

Treasury `add` (inflow/source), `verified-static-callsites`:

- Daily per-entity income loop `0x0053e567 0x0053e69f 0x0053e781 0x0053e7ff`
  (entities: `+0xb8/+0xbc` income).
- Daily per-entity income loop `0x00508c6d 0x00508ca4 0x00508cde 0x00508d1a`
  (entities: `[+0xa0]` `+4`/`+8`; `/2^15` scale), plus `0x00508e6b` and a
  `0x0050c262` site. `verified-static-callsites`.
- Pre-interest `0x005235a4 0x00523697 0x00523819` and `0x0048893c 0x00488add`.

Treasury `sub` (outflow/sink):

- Daily government expense function at `0x005238d0 .. 0x00523a7a`: `sub`/`sbb`
  a 64-bit expense at `0x005239c0` and `0x00523a35`, each clamping the running
  value at zero (`0x00523a27`). `verified-static-callsites`.
- Money-mutator sink `0x005257fa` with a zero clamp (`0x00525806..0x00525810`).
- Interest debit inside `PayDailyInterest` (`RVA 0x00123c30`): `verified-runtime`
  (reproduced, e.g. SWE `-32,814` raw/day).
- Bankruptcy refund `add` at `RVA 0x005246f7` (inside the `0x001241f0` handler).

The `/2^15` scale seen in the `0x00508xxx` source path and the `32768000`
(= `1000.0` @ 15 frac bits) write at `RVA 0x00505ec2` indicate treasury-adjacent
income is carried at a different fixed-point scale than the treasury field
itself; reconciling those scales is part of the conservation item.

Assignment sites include `RVA 0x00505ec2` which writes the raw constant
`32768000` (`1000.0` at 15 fractional bits) to treasury (initialization/reset).

| Next | Resolve the iterated-entity list identity for each source loop and name the income kind (tax/tariff/gold/interest); promote one source to `verified-runtime` with a visible budget-line correlation and record its fixed-point scale. |


## National bank (`CBank`, via `CCountry+0xe88`)

| Field | Offset | Evidence |
| --- | --- | --- |
| country (owner) | `+0x08` | verified-runtime: reader resolves `+0x08` and confirmed, across all 4,065 boundary rows of run `5d5db709`, that each destination bank owner equals the destination country (same tag+ordinal, zero mismatches) |
| **(unknown pool A)** | `+0x10` | verified-current as a distinct 64-bit field that accumulates (SWE `15.5M -> 30.4M`), but live comparisons show it is **not** the national bank's displayed funds/loans (UI reads 0 while `+0x10` reads tens of millions) — label withdrawn |
| **(unknown pool B)** | `+0x18` | verified-current as a distinct 64-bit field that tracks `+0x10` then plateaus; **not** the displayed loans in live comparisons — label withdrawn |
| interest_payments | `+0x20` | verified-runtime (PayDailyInterest credits it; 12 exact pairs) |

**National-bank balance is per-state.** Live observation: once deposits began,
the on-screen bank balance was `0.05`, split `0.03 + 0.02` across the two states
that had generated savings. This is consistent with the bank's displayed balance
being the sum of `CCState+0x258` savings (already `verified-static-callsites` in
`interest-payout.md` for the `CBank::DistributeInterest` weighting), not any
single `CBank` field. The two states map to the `+0x258/+0x25c` savings that
accrue as POPs deposit, which then drives the bank's interest payout weighting.

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
- Repayment: runtime run `5d5db709` (15 days, all 271 countries) recorded
  **zero creditor-count decreases** (e.g. SWE `0,2,3,3,...4`; SAR
  `0,3,3,5,5,...9`; debt-free majors flat at `0`). This is a negative
  `verified-runtime` result consistent with **no recurring principal
  repayment** in normal play: government loans are taken, accrue interest, and
  are removed only at bankruptcy. A repayment path under conditions not
  exercised here (a country choosing to pay down debt) is not excluded.

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

## Open mapping items (research queue)

Verified API-port candidates (see the reader/telemetry sketch): treasury read,
bank ownership, per-state bank balance.

Remaining unverified/unknown mappings, each with a path to verification. Priority
P1 = high value and tractable; P2 = needs the blocked loop/entity work; P3 = may
resolve as a negative.

| # | Mapping | Location | Current state | How to close | Priority |
| --- | --- | --- | --- | --- | --- |
| 1 | Total-debt field | UI `2456.5 £` (SWE) vs creditor rows | unlocated | correlate live UI debt against creditor quantities | P1 |
| 2 | Pool X | `CBank+0x10` | unidentified 64-bit pool | correlate against debt / cash-flow | P1 |
| 3 | Pool Y | `CBank+0x18` | unidentified (tracks +0x10 then plateaus) | same | P1 |
| 4 | Bank-balance UI scale | `Σ CCState+0x258` | part-resolved | nail the `0.05` display/ratio vs state sum | P1 |
| 5 | Gold/cash conversion → treasury | — | unlocated | find the gold→treasury add path | P1 |
| 6 | Budget-source loop kind A | `0x0053e5xx` (`+0xb8/+0xbc`) | located, un-attributed | identify source composition | P2 |
| 7 | Budget-source loop kind B | `0x00508xxx` (`[+0xa0]`, `/2^15`) | located, un-attributed | same | P2 |
| 8 | Iterated-entity identity | global `[0x1258738]+0xd0` | unknown | reify the array (POPs/goods/states?) | P2 |
| 9 | Expense line → field | `0x005238d0` | static-as-expense | name which spending line each `sub` maps to | P2 |
| 10 | Bankruptcy write-off callsite | inside `0x001241f0` | observed, not bracketed | bracket the exact refund instruction | P2 |
| 11 | Money-accounting field | `CCountry+0x40/+0x44` | unknown | find pool consumers | P2 |
| 12 | NBD → interest-rate global | `CGameState+0xb5c` | lead only | resolve and link to `LOAN_BASE_INTEREST` | P2 |
| 13 | Treasury init/reset write | `0x00505ec2` (writes `32768000`) | located | identify the reset function | P2 |
| 14 | Loan repayment | — | negative result | confirm none vs rare path | P3 |

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
