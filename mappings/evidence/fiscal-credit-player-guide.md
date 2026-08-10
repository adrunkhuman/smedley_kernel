# How the money actually moves (player-oriented guide)

A companion to `fiscal-credit-skeleton.md`, written for Victoria II players and
modders, not reverse engineers. It explains what each mapped engine boundary
means for how the game behaves. Technical offsets are in brackets for tracing;
readers who do not care about them can ignore them.

## The national bank tracks cash, loans, and citizens' savings

The national bank is not "your treasury." Its object stores serialized `money`
and `money_lent` fields (`+0x10`, `+0x18`). An 1884 save/runtime comparison
matched both exactly, and native repayment reduces `money_lent` with the
borrower's matching debt. These raw fields still do not map directly to the two
visible bank rows under all UI states. The displayed balance appears **per
state**: when deposits began,
the on-screen balance was `0.05`, built from two states (`0.03 + 0.02`). This is
consistent with your states' `CCState+0x258` savings being pooled for display,
but the exact raw aggregate-to-UI correlation remains incomplete. It is not the
bank object's fields alone.
The owner pointer (`+0x08`) matched the bank's country in all 4,065 sampled rows
from the retained ownership run.

Why players care: when you run a deficit the game attempts domestic borrowing
first, using your own country as the creditor identity, before falling back to
other creditors or the Shadowy Financiers. The mapped native interest path does
not complete the final state-to-POP payout by itself; that missing consumer is
the bug addressed by `interest_bug_fix`.

## Your treasury is the cash in your hand; the budget feeds and drains it daily

Your treasury is what the budget screen shows as cash on hand. The engine fills
and empties it in a daily loop:

- **Income in**: poor-, middle-, and rich-tax receipts are now individually
  mapped and match the save's tax-income arrays. Tariffs have a separate static
  treasury callsite; gold and several other receipt families remain unnamed.
  [tax VAs `0x00508ca4`, `0x00508cde`, `0x00508d1a`; tariff VA `0x00488add`]
- **Spending out**: government expenses debit treasury, but the individual
  military, administration, education, and subsidy callsites are not yet
  attributed. The previously suspected function at `0x005238d0` is actually
  principal repayment, not a generic budget-expense function.
- So a balanced budget keeps treasury roughly flat; a deficit drains it toward
  the clamp, and surplus income builds it up.

## Interest is paid daily to whoever lent to you

Every day the engine pays interest to each of your creditors. If you have the
cash, it is paid and the creditor is marked settled. [PayDailyInterest
`0x00123c30`]

- If you have the money and the creditor resolves to a country, that country's
  bank gets the interest credited. Across the retained named-creditor calls,
  destination-bank increases exactly matched debtor treasury decreases
  (`87,242` raw total).
- Named credit can resolve to your own national bank or a foreign country's
  bank. Ordinal-zero Shadowy Financiers have no destination-bank mutation; the
  mapped path leaves that flow unallocated.

## Principal repayment reduces cash and the lender's outstanding loans

The repayment path is separate from daily interest. Each native repayment
reduces the borrower's treasury and creditor debt by exactly the requested
amount. For a named lender it also reduces lender-bank `money_lent`, clamped at
zero; Shadowy Financiers have no country bank to update. A seven-day 1884 run
captured 12 exact repayments, including domestic SWE -> SWE and foreign
D01 -> ENG / BRZ -> USA loans. [repayment RVA `0x001238d0`]

## The mapped shortfall path

If your treasury cannot cover daily interest, the engine accumulates a
shortfall. Static analysis links that condition to the handler at RVA
`0x001241f0`. That handler applies default modifiers/notifications and cleans up
construction and factory state; three runtime-bracketed calls did not erase
creditor debt or change treasury. A separate test disproved `RemoveDebts(true)`
as a deferred default path: it ran for unrelated country-cleanup cases and
annexation, not for the forced-shortfall country. A forced negative-treasury run
also observed treasury recover to a small positive position, but the source of
that separate recovery remains unlocated.

Why players care: default handling spans more than one engine boundary. The
notification/modifier and construction cleanup are mapped, but current evidence
does not show principal write-off. The observed treasury recovery is not yet
attributed to the handler.

## A worked example: Sweden in early 1836

From a live observer run of the benchmark save, Sweden's early budget read:
income tax on the poor ~41%, middle ~8%, rich ~3%, and tariffs ~50%. The
probe captured over roughly a month of game days:

- Bank `money` grew steadily while `money_lent` later plateaued; the on-screen
  national-bank rows did not present those raw fields directly. When the
  balance later reached `0.05`, the UI split it as
  `0.03 + 0.02` across two states, consistent with the sum of per-state
  `CCState+0x258` savings.
- The interest-window treasury delta was small and steady (a few to ~18k raw)
  — that is the daily interest charge alone. The big swings in the treasury day
  to day come from tax + tariff income, spending, gold conversion, and loans all
  landing together, which is why separating tax from tariff by net treasury
  alone does not work.

## What this does and does not yet explain

Mapped with confidence (see the skeleton): treasury and bank ownership,
class-tax receipts, bank money/money-lent, creditor interest/debt, daily
interest payment, and principal repayment. Tariffs are statically mapped. The
per-state displayed bank-balance model remains a strong but incomplete
correlation.

Still being mapped / needing live-game help:
- Runtime correlation for tariff amounts, plus gold and other treasury sources.
- The exact UI aggregation rule for state savings and bank fields.
- Complete repayment entry removal, default modifier outcomes, and the
  forced-treasury recovery source; ordinary principal reduction is verified.
