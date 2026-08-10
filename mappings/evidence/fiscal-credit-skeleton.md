# Fiscal, banking, and credit mapping index (issue #29)

This index summarizes coverage and follow-up work. The canonical observations,
run IDs, formulas, and negative evidence are in `fiscal-credit-boundaries.md`.
Addresses labeled RVA are module-relative; VA addresses use preferred base
`0x00400000` for the supported HOD 3.04 executable.

## Coverage

| Area | Boundary | Evidence | Remaining limit |
| --- | --- | --- | --- |
| Treasury | `CCountry+0xe78/+0xe7c`, signed 48.15 | `verified-runtime` | `+0xe80` remains unidentified |
| Class taxes | VAs `0x00508ca4`, `0x00508cde`, `0x00508d1a` | `verified-runtime` | None for class attribution |
| Tariffs | VA `0x00488add`, accounting `+0x18` | `verified-static-callsites` | Runtime amount correlation |
| Bank ownership | `CBank+0x08` | `verified-runtime` | None for sampled ownership |
| Bank balances | `money +0x10`, `money_lent +0x18` | `verified-runtime` | UI projection unresolved |
| Bank interest | `interest_payments +0x20` | `verified-runtime` | Ordinal-zero flows have no resolved destination-country bank mutation |
| Creditor | interest `+0x10`, debt `+0x18`, paid `+0x20` | `verified-runtime` | None for field identity |
| Loan origination | `CCountry::TakeLoan`, RVA `0x00122910` | `verified-static-callsites` | Full runtime lifecycle fixture |
| Interest payment | `CCountry::PayDailyInterest`, RVA `0x00123c30` | `verified-runtime` | Shortfall threshold value |
| Principal repayment | `CCountry::RepayLoan`, RVA `0x001238d0` | `verified-runtime` | Runtime entry removal |
| Default invocation | Shortfall handler, RVA `0x001241f0` | `verified-runtime` | Principal and modifier outcomes |
| Debt cleanup | `CCountry::RemoveDebts`, RVA `0x00111340` | `verified-static-callsites` | Not established as default handling |

## Accounting contract

- Treasury, bank, and creditor amounts use signed 48.15 fixed point:
  `pounds = raw / 32768`.
- Serialized tax arrays use scale `1000`; each class receipt is
  `floor(sum(tax_income) * 32768 / 1000)`.
- Sufficient named-creditor interest and observed principal repayment conserve
  exactly at their bracketed boundaries.
- The treasury residual is unavailable as a category. It includes unmapped
  gold, transfers, stockpile/trade settlement, subsidies, and other sources or
  sinks.
- No default debt-write-off or treasury-reset term is established.

## Issue #29 implementation candidates

These are independent observational changes, not approved telemetry contracts.

| Slice | Boundary | Required validation |
| --- | --- | --- |
| Class-tax records | Three class receipt VAs | Supported executable, all signatures, valid country and accounting selector |
| Tariff record | VA `0x00488add` | Keep provisional until runtime budget-line correlation |
| Bank/credit snapshot | Verified `CBank` and `CCreditor` fields | Bounded vectors, readable spans, valid country ordinals, explicit unavailable values |
| Repayment event | Caller RVA `0x0018beec` | Exact requested/treasury/debt equality; reject partial capture |
| Default event | Handler RVA `0x001241f0` | Emit invocation only; do not claim debt write-off or treasury reset |

## Research queue

| Priority | Mapping | Current state | Completion evidence |
| --- | --- | --- | --- |
| P1 | Tariff runtime amount | Static callsite at VA `0x00488add` | Bracket and correlate with the budget tariff line |
| P1 | Gold/cash treasury source | Unlocated | Identify and bracket the treasury add |
| P1 | Default principal outcome | Handler invocation verified; no synchronous write-off | Determine whether principal intentionally persists |
| P1 | Bank-balance UI rule | Per-state savings correlation | Establish aggregation and rounding |
| P2 | Source family at VA `0x0053e5xx` | Located, unattributed | Identify source composition |
| P2 | Accounting `+0x28` source at VA `0x00508c6d` | Located, unattributed | Correlate nonzero samples |
| P2 | Repayment entry removal | Static branch only | Force a small debt to zero |
| P2 | Default modifier/prestige outcomes | Static flow only | Capture before/after state |
| P2 | Forced-treasury recovery | Observed next day, source unknown | Locate the responsible mutation |
| P2 | `CCountry+0x40/+0x44` | Unknown | Find consumers |
| P2 | `CGameState+0xb5c` loan-rate link | Static lead | Resolve against `LOAN_BASE_INTEREST` |
