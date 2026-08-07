# How the money actually moves (player-oriented guide)

A companion to `fiscal-credit-skeleton.md`, written for Victoria II players and
modders, not reverse engineers. It explains what each mapped engine boundary
means for how the game behaves. Technical offsets are in brackets for tracing;
readers who do not care about them can ignore them.

## The national bank is your citizens' savings, lent back to you

The national bank is not "your treasury." It is the savings your citizens have
tucked away. The engine tracks that money and, crucially, most of it is lent
back out (that is how the bank's "money" and "money lent" fields stay almost
equal — for example Sweden's bank, measured over a month, kept its cash and its
lent-out totals essentially equal, so it is lending almost everything it holds).

- When your POPs earn more than they spend, the surplus goes into the bank
  (`money`). [CBank `+0x10`]
- The bank lends most of that out (`total_lent`). [CBank `+0x18`]
- The bank belongs to the country [owner pointer `+0x08` was confirmed to always
  point back to the country whose bank it is], so it is genuinely "your
  citizens' bank," not a foreign institution.

Why players care: because the bank lends to you, your own citizens are your
cheapest lender. When you run a deficit you borrow from the national bank first,
and the interest you then pay comes back to your own people rather than a
foreign country or the Shadowy Financiers.

## Your treasury is the cash in your hand; the budget feeds and drains it daily

Your treasury is what the budget screen shows as cash on hand. The engine fills
and empties it in a daily loop:

- **Income in**: daily loops add your citizens' taxable income and your trade
  (tariff) income into the treasury, per source. [treasury `+0xe78`; the daily
  income loops are at `0x00508xxx` and `0x0053e5xx`]
- **Spending out**: when your government spends (military, admin, etc.) it is
  docked from the treasury daily, and for these routine expenses the account
  cannot go below zero — it is clamped. [expense function `0x005238d0`]
- So a balanced budget keeps treasury roughly flat; a deficit drains it toward
  the clamp, and surplus income builds it up.

## Interest is paid daily to whoever lent to you

Every day the engine pays interest to each of your creditors. If you have the
cash, it is paid and the creditor is marked settled. [PayDailyInterest
`0x00123c30`]

- If you have the money, the creditor's bank gets the interest credited. That is
  why, for example, Spain's bank interest row increased every day while its
  treasury fell by an amount matching what it owed in interest.
- Where the money ends up follows who lent it: your own national bank first
  (your citizens), then foreign countries' banks, then the Shadowy Financiers.

## Bankruptcy is what happens when you can no longer pay the interest

If your treasury cannot cover the daily interest, the shortfall is not just
ignored. The engine collects the unpaid amount, and when it crosses a threshold
it forces the books to balance the only way it can: it writes the debts off and
returns emergency money to the treasury. That is the "bankruptcy reset" —
the country scrapes its slate (creditors are told the loans are gone) and starts
again from a small positive cash position. [shortfall handler `0x001241f0`]

Why players care: that is why a country that is too deep in debt suddenly has a
tiny positive treasury again and a lost-prestige ding — the game did not forgive
the deficit, it forced a formal bankruptcy.

## A worked example: Sweden at 3.4 February 1836

From a live observer run of the benchmark save, Sweden's early budget read:
income tax on the poor ~41%, middle ~8%, rich ~3%, and tariffs ~50%. The
probe captured over roughly a month of game days:

- The national bank kept growing: `money` from `15.5M` to `26.4M` raw, and
  `total_lent` tracking then flattening near the same value — the bank simply
  lends out nearly everything it takes in as savings. That matches the
  player-facing rule that rich/middle strata fuel the bank and the government
  then borrows that same money back.
- The interest-window treasury delta was small and steady (a few to ~18k raw)
  — that is the daily interest charge alone. The big swings in the treasury day
  to day come from tax + tariff income, spending, gold conversion, and loans all
  landing together, which is why separating tax from tariff by net treasury
  alone does not work.

## What this does and does not yet explain

Mapped with confidence (see the skeleton): treasury + bank ownership + bank
money/lent, daily income sources, daily expense clamp, daily interest payment,
and the bankruptcy reset.

Still being mapped / needing live-game help:
- Which exact income source (income tax vs tariff vs gold) each daily loop
  carries — needs one look at a running country's budget numbers.
- Loan repayment is looking like a non-event in normal play: over a 15-day run
  of all 271 countries, not one country's creditor count ever dropped (they
  only grew). So government loans appear to persist and accrue interest until
  bankruptcy writes them off, rather than being paid back over time. A
  player-chosen pay-down is not ruled out, just never observed here.
