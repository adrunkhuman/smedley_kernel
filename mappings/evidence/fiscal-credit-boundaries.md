# Fiscal and credit boundaries

Native fiscal, banking and credit contracts. Unknown paths remain unclassified;
see the [modder guide](../../docs/fiscal-credit.md) for available observations.

## Target and conventions

| Property | Value |
| --- | --- |
| Executable | Victoria II: Heart of Darkness 3.04, English, x86 |
| SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| Preferred base | `0x00400000` |
| Address notation | RVA is module-relative; VA uses the preferred base |
| Fiscal unit | Signed 48.15 fixed point; pounds = `raw / 32768` |

Evidence levels apply to individual fields and paths, not complete objects or
compatibility with other executables.

## Core layouts

These narrow fields are `verified-runtime`; the surrounding layouts are not.

| Object | Field | Offset |
| --- | --- | --- |
| `CCountry` | treasury | `+0xe78/+0xe7c` |
| `CCountry` | bank pointer | `+0xe88` |
| `CCountry` | creditor vector | `+0xe8c/+0xe90` |
| `CBank` | owner country | `+0x08` |
| `CBank` | `money` | `+0x10` |
| `CBank` | `money_lent` | `+0x18` |
| `CBank` | `interest_payments` | `+0x20` |
| `CCreditor` | destination tag/ordinal | `+0x08/+0x0c` |
| `CCreditor` | interest rate | `+0x10` |
| `CCreditor` | principal debt | `+0x18` |
| `CCreditor` | `was_paid` | `+0x20` |

`CCountry+0xe80` remains unidentified; no economic lifecycle is assigned to it.

## Fiscal receipts and sinks

### Class taxes

The three `CCountry+0xa0` tax settings correspond to poor, middle and rich tax.
Each class receipt credits treasury and the same amount to its daily ledger
entry (`verified-runtime`):

| Class | Treasury VA | Daily accounting |
| --- | --- | --- |
| Poor | `0x00508ca4` | `+0x00` |
| Middle | `0x00508cde` | `+0x08` |
| Rich | `0x00508d1a` | `+0x10` |

```text
treasury receipt raw = floor(sum(serialized tax_income) * 32768 / 1000)
```

Conversion occurs once per class, not once per POP. Unrelated accounting slots
cannot be classified by their shape alone.

### Tariffs

The positive tariff receipt is `verified-static-callsites`. Trade settlement
reads `CCountry+0x1440`, applies it to the computed trade amount, credits treasury
at VA `0x00488add`, and records the same amount at daily accounting `+0x18` via
VA `0x00488af7`. This is not the complete negative-rate/subsidy contract; runtime
amount correlation remains unresolved.

### Other fiscal paths

| Path | Status | Meaning or limit |
| --- | --- | --- |
| Source family at VAs `0x0053e567`, `0x0053e69f`, `0x0053e781`, `0x0053e7ff` | Located | Economic category unresolved. |
| Accounting `+0x28` source at VA `0x00508c6d` | Located | Economic category unresolved. |
| Loan-proceeds sources at VAs `0x005235a4`, `0x00523697`, `0x00523819` | `verified-static-callsites` | Origination treasury credits. |
| Other sources at VAs `0x00508e6b`, `0x0050c262`, `0x0048893c` | Located | Economic categories unresolved. |
| Money-mutator sink at VA `0x005257fa` | `verified-static-callsites` | Zero-clamped; budget category unresolved. |
| Treasury write at VA `0x00505ec2` | Located | Writes raw `32768000`; lifecycle unresolved. |

## Loan lifecycle

### Origination

`CCountry::TakeLoan` at RVA `0x00122910` is
`verified-static-callsites`. It attempts a domestic self-tagged creditor before
the ordinal-zero Shadowy Financiers fallback. Origination helper VA
`0x00523400` increases creditor principal and debtor treasury; it is not merely
an interest calculator. Named borrowing also increases lender-bank `money_lent`
(`+0x18`) without debiting bank `money` (`+0x10`).

Ordinal zero does not imply an absent bank mutation:

| Path | `verified-static-callsites` contract |
| --- | --- |
| Existing ordinal-zero creditor | Successful top-up at VAs `0x00523561`–`0x005235bb` increases principal/treasury without a bank-lent store. |
| New ordinal-zero creditor | VAs `0x0052348e`–`0x0052349a` resolve the ordinal's country/bank, including slot zero. New-creditor code at VAs `0x00523806`–`0x00523834` tests bank-pointer nullness, not ordinal, and increases a non-null bank's lent field. |

Slot zero can contain a non-null bank whose lent field changes. That field is
not a mirror of all outstanding ordinal-zero principal. Interest and repayment
have separate ordinal checks; origination behavior must not be inferred from
their bank-credit skips.

### Daily interest

`CCountry::PayDailyInterest` at RVA `0x00123c30` is `verified-runtime`. It walks
`CCountry+0xe8c`, computes the amount due, and branches on available treasury:

1. A sufficient payment debits debtor treasury, credits the resolved nonzero-
   ordinal creditor bank at `+0x20`, and sets `CCreditor.was_paid`.
2. An ordinal-zero creditor receives no bank pending-interest credit.
3. An insufficient payment accumulates a shortfall and leaves `was_paid` clear.
4. A positive shortfall above the threshold calls RVA `0x001241f0`.

Exact treasury-debit/bank-credit equality applies to sufficient named payments,
not all treasury movement. Shortfall handling can include unrelated construction
refunds. The [interest pipeline](interest-payout.md#established-pipeline-and-units)
distinguishes bank pending interest, state pools and POP accounting.

### Principal repayment

`CCountry::RepayLoan` at RVA `0x001238d0` and its order caller at RVA
`0x0018beec` are `verified-runtime`. Repayment reduces creditor principal and
debtor treasury. For a nonzero lender ordinal it also reduces the resolved
lender's `CBank.money_lent`, clamped at zero. Ordinal-zero repayment skips that
bank reduction. Fully repaid creditor entries are removed. Bank reserves are
not credited.

For sufficient-treasury named repayment, requested principal equals both the
treasury debit and principal reduction. This is principal consistency, not a
reserve-to-treasury transfer or a contract for shortfall branches.

### Interest shortfall and default

The handler at RVA `0x001241f0` has a `verified-runtime` invocation boundary.
Its reviewed static body (`verified-static-callsites`):

- Applies `bad_debtor` or `in_bankrupcy` modifiers, emits creditor notifications,
  and cleans construction/factory state.
- Conditionally refunds canceled construction at VA `0x005246f7` and daily
  accounting `+0x40`; this is not a general bankruptcy refund.
- Does not call `CCountry::RemoveDebts` or directly establish principal write-off.

A handler invocation need not synchronously change treasury, creditor principal,
creditor count or own-bank lent principal. Delayed treasury recovery remains
unattributed.

`CCountry::RemoveDebts` is at RVA `0x00111340`. Its direct callers are the
conditional `DailyUpdate` callsite at RVA `0x00108ace` and `CCountry::Annex` at
RVA `0x00118ee6`. The daily callsite is not an established automatic-default
write-off boundary.

## Bank and UI projection

Bank reserves, lent principal, creditor rates and principal use /32768 scale.
The POP deposit path co-updates `CState+0x258` and owner-bank `+0x10` at VAs
`0x00486f85` and `0x00486fac`. POP savings use a separate 1000:1 scale described
in [interest evidence](interest-payout.md#state-pool--pop-missing-native-consumer-and-verified-scale).

The displayed national-bank rows are not direct presentations of `CBank.money`
and `money_lent`. Per-state savings participate in presentation, but aggregate
and rounding rules remain provisional.

## Conservation and residuals

| Quantity | Contract |
| --- | --- |
| Treasury, bank and creditor values | Signed 48.15 fixed point (`raw / 32768`). |
| Serialized tax arrays | Scale `1000`; convert with one floor per class. |
| Tax rounding | Less than one treasury-raw unit per class. |
| Interest transfer | Exact for sufficient named-creditor payments. |
| Principal repayment | Exact treasury/debt equality for the qualified path above. |

Country treasury residual for an interval is:

```text
R_treasury = delta treasury
  - (tax_poor + tax_middle + tax_rich + tariff
     + construction_refunds + named_other_receipts
     - named_budget_sinks - interest_paid - principal_repaid + loan_proceeds)
```

The residual includes unmapped gold, transfers, stockpile/trade settlement,
subsidies and unbracketed sources or sinks. It is not a transaction category or
world-money total.

## Remaining unknowns

- Runtime tariff amount correlation and gold/other treasury source identities.
- Displayed bank-balance aggregation and rounding.
- Default principal treatment and modifier/prestige outcomes.
- Source of delayed negative-treasury recovery.
- Semantics of `CCountry+0x40/+0x44`.
- `CGameState+0xb5c` is established as a viewing tag, not a loan-rate input;
  no relation to `LOAN_BASE_INTEREST` is established.
