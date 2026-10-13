# POP interest fix

`interest_bug_fix` pays native state interest pools to POPs with positive stored
savings in that state. Victoria II already charges borrowers, credits named
creditor banks, and allocates some bank interest to states; the supported
executable has no observed state-to-POP payout stage. This plugin supplies that
last stage without replacing the loan formula or native bank recapitalization.

**This is an opt-in gameplay change**, not passive instrumentation. It changes
POP balances and vanilla liquidity behavior. It does not establish an effect on
bankruptcy or total world money supply. Historical performance and mapping
claims are in the [interest evidence](../mappings/evidence/interest-payout.md).

## Enable and configure

Use the installed plugin with the supported English Heart of Darkness 3.04
executable. In the launcher, select **State POP interest bug fix** and leave Safe
mode off. Selection enables payouts; there is no separate payout switch.

```powershell
smedley_cli --game-dir "C:\Games\Victoria 2" --plugin plugins\interest_bug_fix.toml --detach
```

Or add `"plugins/interest_bug_fix.toml"` to a profile's `plugins` array. Do not
select `v2up` alongside it: the manifests conflict. Deselect the plugin for a
vanilla run; deselection does not undo payments already saved in a campaign.
See [launcher profiles and options](launcher.md) for installation, mod selection,
and safe-mode behavior.

| Setting | Default | How to change it |
| --- | --- | --- |
| CSV diagnostics | Off | **Options… → interest fix → Show advanced settings → Enable interest-fix diagnostics**, CLI `--interest-fix-debug`, or profile `interest_fix_debug = true`. |
| Distribution | Fixed | Positive-savings POPs only; deterministic savings weighting and largest-remainder rounding. No user-configurable rate or recipient policy. |

## Limits and failure behavior

- Payouts consume the **native state pool**, not all interest charged to debtors.
  Native recapitalization and bank-to-state rounding remain unchanged. The
  ordinal-zero “Private Investors” fallback has no named bank or lender POPs and
  remains untouched.
- Old serialized pools are discarded, not paid retroactively. The current C ABI
  does not copy a campaign marker, so the plugin performs checked pool cleanup
  before **every daily bank pass**, including retained failed payouts.
- A complete state pool is converted at `1000:1` into POP-money units. Zero or
  negative savings get nothing. A pool with no positive-savings recipients is
  retained for the rest of the pass and discarded before the next pass.
- Invalid mappings, unreadable/malformed state, bounded traversal limits,
  arithmetic overflow, or changed preconditions reject the affected operation.
  Cleanup must finish successfully before payouts are enabled. Traversal is
  bounded at 512 states, 4,096 province references, and 100,000 POPs per country
  collection, with at most 4,096 states in campaign cleanup. Larger or malformed
  workloads are not partially accepted.
- Native payments across POPs are not atomic. A partial payment, postcondition
  failure, or unavailable mutation stops later payouts in that pass; the next
  successful cleanup can re-enable them. Already completed payments are not
  rolled back. A callback exception disables the registration rather than
  promising recovery on the next day.

## Diagnose a run

Diagnostics write `interest_bug_fix.csv` in the game directory, **truncating it
on each diagnostic launch**. The worker logs failures through Smedley's logging
service. Inspect `paid` versus `no_eligible_savings`, allocation/mutation results,
quality flags, and cumulative `dropped_results`; a drop means incomplete
reporting, not proof of a failed payment. `paid_pop_count` should equal
`verified_pop_count`, and successful `payout_raw` should equal
`state_pool_raw * 1000`.

**Interest-fix telemetry is currently rejected by the bundled sink.** The worker
attempts `interest.fix.health` and, for successful payments,
`interest.fix.value` independently of CSV debug mode. Its `metadata-degraded`
quality is not accepted by the sink, which returns `SMEDLEY_TELEMETRY_INVALID`
before publication. Capture configuration cannot enable these records; use CSV
and logs for diagnosis.

Dates and country tags are unavailable in this callback: records use
`country_index` and CSV `date_raw = -1`. Do not interpret the index as a country
tag or fabricate a date.

For developer contracts, use the [interest-pool C header](../include/smedley/interest_pool_api.h),
[event-services header](../include/smedley/event_services_api.h), and
[plugin-development guide](plugin-development.md). For verified fields,
counterexamples, historical test scope, and remaining uncertainty, use the
[claim-led mapping evidence](../mappings/evidence/interest-payout.md).
