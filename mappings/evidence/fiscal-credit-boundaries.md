# Fiscal and credit boundary mapping (issue #29)

Issue #29 requires fiscal and credit/loan boundaries supported by runtime
evidence. This note records retained static and runtime observations. Each claim states its current
evidence level and unknown paths remain explicit.

## Treasury boundary

`CCountry` treasury is a signed 64-bit fixed-point pair at **+0xe78 (low)**
and **+0xe7c (high)**. `CCountry::DailyUpdate` and the interest path read the
low dword from `+0xe78` and the high dword from `+0xe7c` together. The separate
64-bit field beginning at `+0xe80` remains the neutral
`field_0xe80_candidate`; this investigation did not establish its economic
lifecycle.

## Tax receipt boundaries (`CCountry::DailyUpdate`)

Three consecutive treasury additions are now `verified-runtime` as class tax
receipts:

| Tax class | Treasury VA | Daily accounting slot | SWE raw | Save `tax_income` sum |
| --- | --- | --- | ---: | ---: |
| poor | `0x00508ca4` | `+0x00` | `778031` | `23743.63687` |
| middle | `0x00508cde` | `+0x08` | `424367` | `12950.67673` |
| rich | `0x00508d1a` | `+0x10` | `62756` | `1915.18658` |

Run `767d2c76-e1f2-4ad2-8df7-a19de1829f9f` used unchanged
`benchmark.v2` (SHA-256 `F24F4066...`) for one day. Its 271-row CSV had
SHA-256 `5ABA189D...`, zero quality flags, and zero drops. For each SWE class:

```text
treasury receipt raw = floor(sum(serialized tax_income) * 32768 / 1000)
```

The three observed SWE tax receipts sum to
`778031 + 424367 + 62756 = 1265154` raw. This identifies the three
`CCountry+0xa0` tax-setting entries and their accounting slots without using
expense-index shape.

## Tariff receipt boundary

Tariffs are `verified-static-callsites`. The trade-settlement path reads the
serialized tariff setting at `CCountry+0x1440`, multiplies it by the computed
trade amount, adds the fixed-point result to treasury at VA `0x00488add`, and
adds the same result to daily accounting slot `+0x18` at VA `0x00488af7`.
The exact per-call tariff amount has not been bracketed at runtime, so the
callsite must not be promoted to `verified-runtime` yet.

## Interest payment boundary (`CCountry::PayDailyInterest`, RVA `0x00123c30`)

The function subscribes around its sole direct call (`RVA 0x00108d3e`,
`DailyInterestEvent`). Static flow:

1. Walks the country creditor vector (`+0xe8c` head, `+0xe90` end).
2. For a payable creditor computes interest, then compares treasury against the
   due amount at VA `0x00523fcd`-`0x00523fef`:
   - sufficient: credits the resolved destination-country bank `+0xe88` + `0x20`
     and marks the creditor paid (`+0x20 = 1`) at VA `0x00524196`-`0x0052419c`;
   - insufficient: accumulates the shortfall into a local running total
     (VA `0x00523fff`-`0x00524003`) and does **not** mark the creditor paid.
3. At function end if the accumulated shortfall is still positive and exceeds a
   threshold double at `0x00e45f08`, calls the shortfall handler at
   `RVA 0x001241f0` (VA `0x005241cd`).

This gives a concrete **fiscal sink** (treasury debited for interest) and the
**credit** boundary (the exact per-creditor bank `+0x20` delta), both of which
the earlier interest evidence already correlated against a supplied save.

## Shortfall / bankruptcy handler (`RVA 0x001241f0`)

Called only from the insufficient-funds tail of `PayDailyInterest`. Static flow:

- Applies `bad_debtor` / `in_bankrupcy` modifiers or emits
  `on_debtor_default_second` notifications with creditor identity, depending on
  its date/cooldown branch.
- Traverses construction/factory state for cleanup. A conditional
  construction-cancellation branch refunds that construction's stored amount
  to treasury at VA `0x005246f7` and accounting `+0x40` at VA `0x00524715`.
  This is not a general bankruptcy refund.
- Reads creditor identity for notifications but does not write creditor debt,
  lender-bank principal, or the creditor vector.

The exact threshold and modifier duration remain unverified runtime targets.

A later temporary callsite bracket verified that the handler is invoked with
the affected country pointer. On the unmodified 1884 fixture it captured VNZ
and NZL; on a disposable copy with SWE treasury set to `-100000.0`, it captured
SWE with self-creditor identity (`SWE`, ordinal 16). All three calls returned
without an immediate treasury, creditor-principal, creditor-count, or own-bank
principal change. This is retained negative evidence: the previously observed
next-day treasury reset is not proven to occur synchronously at the handler's
outer call boundary.

A separate all-country bracket disproved `CCountry::RemoveDebts(true)` at RVA
`0x00108ace` as the deferred default boundary. Across three days it ran only for
ITA, ALD, LIB, and JAN under a preceding list-membership condition, never for
forced-shortfall SWE. Its only other direct caller is `CCountry::Annex` at RVA
`0x00118ee6`. `RemoveDebts` can destroy creditors and adjust lender banks, but
no call from the shortfall handler or a default-marked SWE path exists. No debt
write-off should be claimed from the current evidence; the forced-treasury
recovery and any formal principal treatment remain separate open questions.

## Runtime setup used

A disposable mod and read-only plugin were used for this investigation and
removed afterward. The mod fired an early-1836 country event for SWE, SAR, FRA,
ENG, PRU, RUS, AUS, and USA. `money = -99999` did not force bankruptcy;
`money = -1000000000` drove the targeted treasuries deeply negative and
reproduced the native reset transition. The mod, plugin, and local save fixtures
were excluded from the repository after the observations were retained.

## Runtime evidence (30-day observer run, run `eb378356`)

A disposable read-only probe (`fiscal_credit_probe`) subscribed to the
`DailyInterestEvent` BEFORE/AFTER boundary and snapshot treasury (via
`ReadTelemetryCountry`, `CCountry+0xe78`) plus the resolved destination-bank
`+0x20` totals (via `ReadCountryCreditors` / `ReadCountryCreditorBalances`).
The source `benchmark.v2` SHA-256 remained
`F24F40665745B5FF01AC3ED84B138EFB54C634FB1C9A69EF3C06A75617295D3E`, `error.log`
stayed empty, and the campaign reached its exact 30-day target. All 8,130
boundary rows had `flags=0x0` and zero dropped results.

The initial `money = -99999` event did not force a negative treasury. Setting
`money = -1000000000` drove all targeted countries deeply negative. This
establishes the effective probe magnitudes, not a general event-effect scale.

The probe measured two runtime boundaries plus a forced-treasury recovery:

- **Fiscal sink**: the current country's own interest is debited from `+0xe78`
  inside `PayDailyInterest`. Observed example: after the treasury recovery,
  SWE draws `-32,814` raw/day. The same column shows a large net movement from
  the `-1e9` forced drop on the firing day.
- **Credit boundary**: the resolved destination-bank `+0x20` total is credited
  inside the same call. Observed example: SWE `+64` raw/day, growing to
  `+1,038` by day 30; larger countries show larger credits (for example an
  1884 fixture run recorded daily destination credits up to `+7,090,559`).
  These match the prior interest evidence and stay conservation-relevant to the
  same-call treasury interest debit only when no unrelated shortfall movement
  is bundled in.
- **Forced negative-treasury/reset observation**: on `benchmark.v2` with the probe
  mod, all eight targeted tags (SWE, SAR, FRA, ENG, PRU, RUS, AUS, USA) show
  treasury `-2.7e9 .. -3.3e9` raw and `creditor_count = 0` on the firing day,
  then SWE/SAR/etc. reset to a small positive treasury the following day (for
  example SWE `-3,268,401,947` to `+490,036`) and resume the interest economy.
  A later handler bracket produced no synchronous treasury/debt mutation, and
  static re-analysis identified VA `0x005246f7` as a conditional construction
  refund rather than a general default refund. The recovery therefore remains
  unattributed and must not be presented as formal bankruptcy accounting.

## National-bank fields

A temporary probe sampled destination-bank `+0x10`, `+0x18`, and `+0x20`. The
`+0x10`/`+0x18` sampling and probe were removed after the investigation; only
the retained evidence below remains.

Across the 30-day run, for the destination bank reached from SWE's creditors:

- `+0x10` grew monotonically `15,489,107 -> 30,384,692`.
- `+0x18` tracked it closely, then plateaued at `30,312,608`
  while `+0x10` kept growing (last few days: `+0x10` up, `+0x18` flat).
- `+0x20` (`interest_payments`) accumulated the per-call interest credits
  (`+4,947 ... +5,363`) as previously verified.

The 1884 save/runtime run resolves the two formerly neutral fields:

- `CBank+0x10` is serialized `bank.money`: SWE save `75894.56842` maps to
  runtime raw `2486913218` (`round(value * 32768)`). Static VA `0x00486f85`
  adds the same deposit amount to `CCState+0x258`, then VA `0x00486fac` adds it
  to the owner bank `+0x10`, before the corresponding POP-savings update.
- `CBank+0x18` is serialized `bank.money_lent`: SWE save `21725.41159` maps to
  runtime raw `711898287`. `CCountry::RepayLoan` subtracts principal from this
  lender-bank field alongside the matching creditor debt.

The earlier UI comparison only disproved a direct raw-to-visible balance/loan
interpretation; it did not disprove the serialized field names. The catalog now
uses `money` and `money_lent`, while the displayed national-bank balance's
aggregate/rounding presentation remains a separate mapping question.

## Loan repayment boundary

`CCountry::RepayLoan` at RVA `0x001238d0` is `verified-runtime`. Static flow
walks the debtor's `CCountry+0xe8c` creditor vector, subtracts the requested
amount from `CCreditor+0x18`, debits debtor treasury, and removes entries that
reach zero. For a nonzero lender ordinal it also subtracts from resolved
`CBank+0x18` with a zero clamp; ordinal-zero Shadowy Financiers skip that bank
mutation.

Run `3f63c64b-601f-4828-a85b-9ab234de7f4c` bracketed the order caller at RVA
`0x0018beec` for seven days of unchanged `benchmark_interestprobe.v2`. The
12-row repayment CSV had SHA-256 `135047B2...`, zero flags, and zero drops. All
12 positive calls satisfied:

```text
requested raw = -debtor treasury delta = -creditor debt delta
```

Retained identities include D01 -> ENG, BRZ -> USA, and SWE -> SWE. SWE's save
debt `14969.11719` maps to raw `490508032`; one call requested `98304` raw
(`3.0` pounds) and reduced both treasury and creditor debt by exactly `98304`.
The next snapshot reduced SWE `bank.money_lent` by the same amount. No creditor
entry reached zero during the seven-day run, so removal remains statically
verified rather than runtime observed.

## Status against issue #29

The issue's evidence milestone is met with runtime tax, interest, repayment,
bank ownership, bank money/money-lent, and creditor interest/debt boundaries.

Explicit remaining unknowns:

- Tariff amount runtime correlation and gold/other treasury receipt identities.
- The displayed bank-balance aggregate and rounding rule.
- Runtime observation of complete repayment entry removal.
- Default principal treatment plus prestige/modifier outcomes; the handler
  invocation is verified, with no synchronous debt or treasury mutation.
- The source of the forced negative-treasury recovery.


## Disposable probe (removed)

The read-only fiscal/repayment/default and `RemoveDebts` probes, forcing mod,
disposable save, and their build registrations were removed after retaining
the evidence above. No investigation plugin ships in this branch.

## Note on why the earlier bankruptcy-mod attempt failed

A `country_event` with a `year`/tag trigger and `mean_time_to_happen` loads and
fires for AI, but the `money` amount must exceed the country's raw treasury.
`ai_chance` on an event option is required for AI auto-selection; a
`political_decisions` entry is not auto-taken by AI (GFM gates such decisions
with `ai = no`).
