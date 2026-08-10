# Fiscal and credit behavior for modders

This reference states the fiscal and credit behavior established for the
supported Victoria II 3.04 executable. It assumes familiarity with the game's
budget and national-bank systems. See `fiscal-credit-boundaries.md` for the
runtime evidence, addresses, and conservation limits.

## Behavior contract

| Boundary | Established behavior | Limit |
| --- | --- | --- |
| Class taxes | Poor-, middle-, and rich-tax receipts have separate treasury and daily-accounting callsites. Runtime amounts match the corresponding serialized `tax_income` arrays. | Gold, transfers, and several other receipt families remain unattributed. |
| Tariffs | Trade settlement applies `CCountry.tariffs`, credits treasury, and records daily accounting `+0x18`. | The callsite is statically verified; its per-call amount has not been correlated at runtime. |
| National bank | `CBank.money`, `money_lent`, owner, and interest accumulator fields are identified. POP deposits co-update state savings and bank money. | The two visible bank rows are not direct presentations of the bank fields. Their exact aggregation and rounding remain unresolved. |
| Daily interest | A sufficient payment debits debtor treasury, credits the resolved country bank, and marks the creditor paid. | Ordinal-zero Shadowy Financiers have no destination-bank mutation. The native engine has no state-to-POP consumer for the resulting interest pool; `interest_bug_fix` supplies that missing consumer. |
| Principal repayment | Repayment reduces debtor treasury and creditor debt by the requested amount. For a named lender it also reduces `money_lent`, clamped at zero. | Entry removal is statically mapped but was not reached in the retained runtime run. |
| Interest shortfall | The shortfall handler applies default notifications/modifiers and performs construction or factory cleanup. | Three observed calls made no synchronous treasury, debt, creditor-count, or lender-principal change. Principal write-off and treasury recovery are not established. |

## Modding implications

- Treat class-tax receipts as independently attributable. Do not classify the
  remaining treasury residual as tariffs, gold, spending, or any other category.
- Treat bank fields, per-state savings, and UI bank values as distinct views
  until the display aggregation is mapped.
- Keep interest and principal repayment as separate lifecycle events.
- Do not model default as a verified debt write-off. The mapped handler does not
  call `RemoveDebts`, and the observed next-day treasury recovery has no assigned
  source.
- Preserve creditor identity. Domestic and foreign named creditors resolve to a
  country bank; Shadowy Financiers follow the ordinal-zero path.

## Address index

| Boundary | Address | Evidence |
| --- | --- | --- |
| Poor/middle/rich tax receipts | VAs `0x00508ca4`, `0x00508cde`, `0x00508d1a` | `verified-runtime` |
| Tariff receipt | VA `0x00488add` | `verified-static-callsites` |
| `CCountry::PayDailyInterest` | RVA `0x00123c30` | `verified-runtime` |
| `CCountry::RepayLoan` | RVA `0x001238d0` | `verified-runtime` |
| Interest-shortfall handler | RVA `0x001241f0` | `verified-runtime` invocation |

## Unresolved behavior

- Runtime tariff amount correlation and gold/other treasury sources.
- National-bank UI aggregation and rounding.
- Runtime observation of complete repayment entry removal.
- Default principal treatment, modifier/prestige outcomes, and the source of
  forced-treasury recovery.
