# Fiscal and credit boundary mapping (issue #29)

Issue #29 requires reproducible fiscal and credit/loan boundaries. This note
records the static disassembly stage and the planned runtime reproduction. It is
an in-progress evidence record: every claim below is `verified-static-callsites`
until a bounded runtime probe correlates it.

## Treasury boundary

`CCountry` treasury is a signed 64-bit fixed-point pair at **+0x0xe78 (low)**
and **+0x0xe7c (high)**. `CCountry::DailyUpdate` and the interest path read the
low dword from `+0xe78` and the high dword from `+0xe7c` together. The existing
catalog's `treasury_shadow = 0xe80` is the dword immediately following the
64-bit value and is not yet established as a separate economic field; the
mapping note `treasury_shadow` should be treated as the high half's neighbour
until runtime evidence distinguishes them.

## Interest payment boundary (`CCountry::PayDailyInterest`, RVA `0x00123c30`)

The function subscribes around its sole direct call (`RVA 0x00108d3e`,
`DailyInterestEvent`). Static flow:

1. Walks the country creditor vector (`+0xe8c` head, `+0xe90` end).
2. For a payable creditor computes interest, then compares treasury against the
   due amount at VA `0x00523fcd`-`0x00523fef`:
   - sufficient: credits the resolved destination-country bank `+0xe88` + `0x20`
     and marks the creditor paid (`+0x20 = 1`) at VA `0x00524196`-`0x0052419c`;
   - insufficient: accumulates the shortfall into a local running total
     (VA `0x00523fff`-`0x00524003`), calls the money mutator `0x00525770` with
     the full treasury, and does **not** mark the creditor paid.
3. At function end if the accumulated shortfall is still positive and exceeds a
   threshold double at `0x00e45f08`, calls the shortfall handler at
   `RVA 0x001241f0` (VA `0x005241cd`).

This gives a concrete **fiscal sink** (treasury debited for interest) and the
**credit** boundary (the exact per-creditor bank `+0x20` delta), both of which
the earlier interest evidence already correlated against a supplied save.

## Shortfall / bankruptcy handler (`RVA 0x001241f0`)

Called only from the insufficient-funds tail of `PayDailyInterest`. Static flow:

- Returns emergency funds to treasury: `add [ebx+0xe78], ecx; adc [ebx+0xe7c],
  edi` at VA `0x005246f7`.
- Adds the same amount to a money-accounting field (`[eax+0x40]/+0x44`) at VA
  `0x00524715`.
- Refunds and removes creditor debt entries and applies the `bankruptcy`
  administrative-reform modifier (issue `bankruptcy` in `common/issues.txt`).
- Traverses states/factories to clean up state-owned money under the shortfall.

The bankruptcy refund-to-treasury is the main **fiscal source** of the
boundary set. The exact threshold, the money-accounting field at `+0x40`, and
the effective modifier application remain unverified runtime targets.

## Planned runtime reproduction

A disposable **Smedley Bankruptcy Probe** mod was added under
`game/mod/interestprobe/` (descriptor `game/mod/interestprobe.mod`). It fires an
early-1836 `country_event` (`money = -99999`) for the tags SWE, SAR, FRA, ENG,
PRU, RUS, AUS, and USA. Driving those treasuries deeply negative forces the
native insufficient-funds tail and, in turn, the shortfall handler, so a bounded
observer run can observe the fiscal source/sink under the `DailyInterestEvent`
boundary.

Pre-existing bankruptcy fixture saves also exist:
`benchmark_interestprobe_bankruptcy.v2` (SHA-256 `0CB1784E...`) and
`benchmark_interestprobe_clm_bankruptcy.v2` (SHA-256 `02FA2996...`).

Acceptance requires: a bounded run that reproduces at least one treasury
mutation and one credit/bank mutation with retained before/after evidence, then
a reclassification from `verified-static-callsites` to `verified-runtime`.

## Runtime evidence (30-day observer run, run `eb378356`)

A disposable read-only probe (`fiscal_credit_probe`) subscribed to the
`DailyInterestEvent` BEFORE/AFTER boundary and snapshot treasury (via
`ReadTelemetryCountry`, `CCountry+0xe78`) plus the resolved destination-bank
`+0x20` totals (via `ReadCountryCreditors` / `ReadCountryCreditorBalances`).
The source `benchmark.v2` SHA-256 remained
`F24F40665745B5FF01AC3ED84B138EFB54C634FB1C9A69EF3C06A75617295D3E`, `error.log`
stayed empty, and the campaign reached its exact 30-day target. All 8,130
boundary rows had `flags=0x0` and zero dropped results.

The `money` event effect is in the same raw units as `CCountry+0xe78`: raw
treasuries here span from single thousands up to `411,599,982,393`, so the
initial `money = -99999` probe value was far too small to force a negative
treasury (this is why the first attempt produced no bankruptcy). Setting
`money = -1000000000` drove all targeted countries deeply negative.

The probe measured two reproducible boundaries plus the bankruptcy transition:

- **Fiscal sink**: the current country's own interest is debited from `+0xe78`
  inside `PayDailyInterest`. Reproducible example: after the bankruptcy reset,
  SWE draws `-32,814` raw/day. The same column shows a large net movement from
  the `-1e9` forced drop on the firing day.
- **Credit boundary**: the resolved destination-bank `+0x20` total is credited
  inside the same call. Reproducible example: SWE `+64` raw/day, growing to
  `+1,038` by day 30; larger countries show larger credits (for example an
  1884 fixture run recorded daily destination credits up to `+7,090,559`).
  These match the prior interest evidence and stay conservation-relevant to the
  same-call treasury interest debit only when no unrelated shortfall movement
  is bundled in.
- **Forced bankruptcy transition**: on `benchmark.v2` with the `interestprobe`
  mod, all eight targeted tags (SWE, SAR, FRA, ENG, PRU, RUS, AUS, USA) show
  treasury `-2.7e9 .. -3.3e9` raw and `creditor_count = 0` on the firing day,
  then SWE/SAR/etc. reset to a small positive treasury the following day (for
  example SWE `-3,268,401,947` to `+490,036`) and resume the interest economy.
  The reset moves `+0xe78` by billions, consistent with the static shortfall
  handler `RVA 0x001241f0` "return funds to treasury" branch, but the exact
  callsite that performs the write-off is not bracketed by `DailyInterestEvent`,
  so it remains `verified-static-callsites`.

## National-bank fields (run `a9842147`, 30-day `benchmark.v2`)

To move the `CBank` layout out of `historical-unverified`, the `game_state`
reader `ReadCountryCreditors`/`ReadCountryCreditorBalances` now also samples the
destination nation's bank `+0x10` and `+0x18` into
`destination_bank_money_raw` / `destination_bank_total_lent_raw` (both use the
existing `AddChecked` overflow handling, so an unreadable field is emitted
absent; the per-destination `+0x20` array is unchanged).

Across the 30-day run, for the destination bank reached from SWE's creditors:

- `+0x10` (`money`) grew monotonically `15,489,107 -> 30,384,692`.
- `+0x18` (`total_lent`) tracked it closely, then plateaued at `30,312,608`
  while `+0x10` kept growing (last few days: `+0x10` up, `+0x18` flat).
- `+0x20` (`interest_payments`) accumulated the per-call interest credits
  (`+4,947 ... +5,363`) as previously verified.

This upgrades `CBank +0x10` and `+0x18` from `historical-unverified` to
`verified-current` (reader bytes against the supported executable, distinct
64-bit fields, runtime-observed to move and to diverge / plateau relative to
each other). The economic names `money` and `total_lent` remain candidate
(`historical-unverified`) labels: the near-equality and SWE that lends out most
of its deposit base is consistent with a national bank, but ownership structure,
deposit provenance, and loan mechanics are not yet resolved by this slice.

## Status against issue #29

This is an in-progress evidence record, not a claim that #29 is complete.

Met (minimum acceptance): at least one fiscal boundary (treasury mutation at
`PayDailyInterest`) and one banking/credit boundary (destination-bank `+0x20`)
now have reproducible runtime evidence, with documented ownership, units, and
lifecycle for those two fields.

Still outstanding (#29 required work):

- POP taxes and tariffs callsites/fields (no shape-based classifier).
- Government budget sources and sinks beyond the interest debit; treasury
  mutation boundaries for receipts (tax/tariff income, gold conversion) as well
  as the interest sink recorded here.
- National-bank asset/liability/deposit/ownership semantics (only the bank
  interest-receipt field `CBank+0x20` is mapped; deposit and ownership structure
  are unresolved).
- Loan **repayment** lifecycle and the full default/bankruptcy transition with
  retained creditor/debtor identity. (Loan *origination* is already
  `verified-static-callsites` in `interest-payout.md`: `CCountry::TakeLoan` at
  `RVA 0x00122910` for domestic creditor creation, the Shadowy Financiers
  fallback, and the bankruptcy construction reach at `0x001257a8`.)
- Conservation equations, units, rounding bounds, unavailable terms, and an
  explicit residual for unmapped treasury paths.
- Bounded implementation slices for each retained-evidence boundary.


## Disposable probe

The `fiscal_credit_probe` plugin (source under
`plugins/fiscal_credit_probe/`, manifest `fiscal_credit_probe.toml`,
`-smedley-probe-debug=1`) is a temporary investigation artifact. It mutates
nothing. It is registered in `plugins/CMakeLists.txt` only for this investigation
and must be removed (source, CMake `add_subdirectory`, and installed
`game/plugins/fiscal_credit_probe.*`) before any merge to `master`.

## Note on why the earlier bankruptcy-mod attempt failed

A `country_event` with a `year`/tag trigger and `mean_time_to_happen` loads and
fires for AI, but the `money` amount must exceed the country's raw treasury.
`ai_will_do` on an event option is required for AI auto-selection; a
`political_decisions` entry is not auto-taken by AI (GFM gates such decisions
with `ai = no`).
