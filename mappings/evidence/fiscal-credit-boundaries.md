# Fiscal and credit boundary evidence (issue #29)

This is the canonical evidence record for accepted fiscal, banking, and credit
boundaries. Unknown paths remain unavailable rather than inferred.

## Target and conventions

| Property | Value |
| --- | --- |
| Executable | Victoria II: Heart of Darkness 3.04, English, x86 |
| SHA-256 | `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648` |
| Preferred base | `0x00400000` |
| Address notation | RVA is module-relative; VA uses the preferred base |
| Fiscal unit | Signed 48.15 fixed point; pounds = `raw / 32768` |

Evidence levels follow the repository mapping conventions. Runtime observations
identify the supported executable only; they do not establish compatibility
with other builds.

## Core layouts

| Object | Field | Offset | Evidence |
| --- | --- | --- | --- |
| `CCountry` | treasury | `+0xe78/+0xe7c` | `verified-runtime` |
| `CCountry` | bank pointer | `+0xe88` | `verified-runtime` |
| `CCountry` | creditor vector | `+0xe8c/+0xe90` | `verified-runtime` |
| `CBank` | owner country | `+0x08` | `verified-runtime`; 4,065/4,065 sampled rows matched |
| `CBank` | `money` | `+0x10` | `verified-runtime`; save/runtime correlation |
| `CBank` | `money_lent` | `+0x18` | `verified-runtime`; save/runtime and repayment correlation |
| `CBank` | `interest_payments` | `+0x20` | `verified-runtime`; exact interest deltas |
| `CCreditor` | destination tag/ordinal | `+0x08/+0x0c` | `verified-runtime` |
| `CCreditor` | interest | `+0x10` | `verified-runtime`; save/runtime correlation |
| `CCreditor` | debt | `+0x18` | `verified-runtime`; save/runtime and repayment correlation |
| `CCreditor` | `was_paid` | `+0x20` | `verified-runtime` |

`CCountry+0xe80` remains `field_0xe80_candidate`; no economic lifecycle is
assigned to it.

## Fiscal receipts and sinks

### Class taxes

| Class | Treasury VA | Daily accounting | SWE raw | Serialized `tax_income` sum |
| --- | --- | --- | ---: | ---: |
| Poor | `0x00508ca4` | `+0x00` | `778031` | `23743.63687` |
| Middle | `0x00508cde` | `+0x08` | `424367` | `12950.67673` |
| Rich | `0x00508d1a` | `+0x10` | `62756` | `1915.18658` |

All three callsites are `verified-runtime`. For each class in run
`767d2c76-e1f2-4ad2-8df7-a19de1829f9f`:

```text
treasury receipt raw = floor(sum(serialized tax_income) * 32768 / 1000)
```

The observed SWE receipts total `1265154` raw. This identifies the three
`CCountry+0xa0` tax settings without classifying unrelated accounting slots by
shape.

### Tariffs

The tariff receipt is `verified-static-callsites`. Trade settlement reads
`CCountry+0x1440`, applies it to the computed trade amount, credits treasury at
VA `0x00488add`, and records the amount at daily accounting `+0x18` via VA
`0x00488af7`. Runtime amount correlation remains open.

### Other fiscal paths

| Path | Status | Limit |
| --- | --- | --- |
| Source family at VAs `0x0053e567`, `0x0053e69f`, `0x0053e781`, `0x0053e7ff` | Located | Economic category unresolved |
| Accounting `+0x28` source at VA `0x00508c6d` | Located | Economic category unresolved |
| Other source VAs `0x00508e6b`, `0x0050c262`, `0x005235a4`, `0x00523697`, `0x00523819`, `0x0048893c` | Located | Economic categories unresolved |
| Money-mutator sink at VA `0x005257fa` | `verified-static-callsites` | Zero-clamped; budget category unresolved |
| Treasury write at VA `0x00505ec2` | Located | Writes raw `32768000`; lifecycle unresolved |

## Loan lifecycle

### Origination

`CCountry::TakeLoan` at RVA `0x00122910` is
`verified-static-callsites`. It attempts a domestic self-tagged creditor before
the ordinal-zero Shadowy Financiers fallback. Detailed origination evidence is
in `interest-payout.md`.

### Daily interest

`CCountry::PayDailyInterest` at RVA `0x00123c30` is `verified-runtime`. It walks
`CCountry+0xe8c`, computes the amount due, and branches on available treasury:

1. A sufficient payment debits debtor treasury, credits the resolved nonzero-
   ordinal creditor bank at `+0x20`, and sets `CCreditor.was_paid`.
2. An ordinal-zero creditor receives no destination-bank mutation.
3. An insufficient payment accumulates a shortfall and leaves `was_paid` clear.
4. A positive shortfall above the threshold calls RVA `0x001241f0`.

Retained paired observations matched destination-bank credits to debtor treasury
debits for all named-creditor calls (`87,242` raw total). Net treasury movement
is not a general interest-conservation signal because the shortfall path can
contain unrelated construction refunds.

### Principal repayment

`CCountry::RepayLoan` at RVA `0x001238d0` and its order caller at RVA
`0x0018beec` are `verified-runtime`. The function reduces creditor debt and
debtor treasury. For a nonzero lender ordinal it also reduces the resolved
lender's `CBank.money_lent`, clamped at zero; Shadowy Financiers skip the bank
mutation. It removes creditor entries that reach zero, but that branch is only
`verified-static-callsites`.

Run `3f63c64b-601f-4828-a85b-9ab234de7f4c` captured 12 calls with D01 -> ENG,
BRZ -> USA, and SWE -> SWE identities. Every call satisfied:

```text
requested raw = -delta debtor treasury = -delta creditor debt
```

One SWE call requested `98304` raw (`3.0` pounds) and reduced treasury, debt,
and the next sampled `bank.money_lent` by that amount.

### Interest shortfall and default

The handler at RVA `0x001241f0` is `verified-runtime` as an invocation boundary.

Static evidence (`verified-static-callsites`):

- The handler applies `bad_debtor` or `in_bankrupcy` modifiers, emits creditor
  notifications, and cleans construction/factory state.
- A conditional branch refunds a canceled construction at VA `0x005246f7` and
  daily accounting `+0x40`; this is not a general bankruptcy refund.
- The handler does not call `CCountry::RemoveDebts`.

Runtime evidence (`verified-runtime` invocation):

Three bracketed calls retained country and creditor identity. Each returned with
no synchronous treasury, creditor-debt, creditor-count, or own-bank principal
change.

The conditional `RemoveDebts(true)` callsite at RVA `0x00108ace` was observed
for ITA, ALD, LIB, and JAN, not forced-shortfall SWE. The function itself is at
RVA `0x00111340`; its other direct caller is `CCountry::Annex` at RVA
`0x00118ee6`. Current evidence therefore does not establish default principal
write-off. A separately observed next-day treasury recovery remains
unattributed.

## Bank and UI projection

SWE save/runtime correlation identifies:

- `CBank+0x10` as `money`: `75894.56842` -> raw `2486913218`.
- `CBank+0x18` as `money_lent`: `21725.41159` -> raw `711898287`.
- `CCreditor+0x10` as interest: `0.01999` -> raw `655`.
- `CCreditor+0x18` as debt: `14969.11719` -> raw `490508032`.

The POP deposit path co-updates `CCState+0x258` and owner-bank `+0x10` at VAs
`0x00486f85` and `0x00486fac`. The visible national-bank rows are not direct
presentations of `CBank.money` and `money_lent`. A two-state `0.03 + 0.02`
display matched the per-state savings split, but the aggregate and rounding rule
remain provisional.

## Conservation and residuals

| Quantity | Contract |
| --- | --- |
| Treasury, bank, and creditor values | Signed 48.15 fixed point (`raw / 32768`) |
| Serialized tax arrays | Scale `1000`; convert with one floor per class |
| Tax rounding | Less than one treasury-raw unit per class |
| Interest transfer | Exact for sufficient named-creditor calls |
| Principal repayment | Exact treasury/debt equality at the bracketed boundary |

Country treasury residual for an interval is:

```text
R_treasury = delta treasury
  - (tax_poor + tax_middle + tax_rich + tariff
     + construction_refunds + named_other_receipts
     - named_budget_sinks - interest_paid - principal_repaid + loan_proceeds)
```

`R_treasury` is unavailable as a transaction category. It includes unmapped
gold, transfers, stockpile/trade settlement, subsidies, and any unbracketed
source or sink. Do not present it as categorized money or a world-money total.

## Retained runtime evidence

| Run | Scope | Result |
| --- | --- | --- |
| `767d2c76-e1f2-4ad2-8df7-a19de1829f9f` | One-day class-tax bracket, 271 countries | Exact SWE class conversion; zero flags/drops; CSV SHA-256 `5ABA189D...` |
| `eb378356` | 30-day daily-interest bracket | 8,130 rows; zero flags/drops; source save SHA-256 `F24F40665745B5FF01AC3ED84B138EFB54C634FB1C9A69EF3C06A75617295D3E` |
| `5d5db709` | Bank-owner correlation | 4,065/4,065 owner matches |
| `3f63c64b-601f-4828-a85b-9ab234de7f4c` | Seven-day repayment bracket | 12/12 exact treasury/debt reductions; CSV SHA-256 `135047B2...` |
| Forced shortfall | VNZ, NZL, SWE handler calls | `money = -99999` did not force negative treasury; `-1000000000` did. These are test magnitudes, not a bankruptcy threshold. No synchronous accounting mutation; next-day SWE treasury recovery remains unattributed. |

## Remaining unknowns

- Runtime tariff amount correlation and gold/other treasury source identities.
- Displayed bank-balance aggregation and rounding.
- Runtime observation of complete repayment entry removal.
- Default principal treatment and modifier/prestige outcomes.
- Source of the forced negative-treasury recovery.
