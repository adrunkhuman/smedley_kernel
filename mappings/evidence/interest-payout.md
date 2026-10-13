# Native interest pipeline and POP payout evidence

The supported executable charges borrowers, credits named creditor banks, and
allocates bank interest to state pools. **No state-to-POP consumer was identified
in the reviewed static paths or observed native cash-flow phases.** Later,
indirect, or transient payouts are not excluded. The opt-in `interest_bug_fix`
consumes native state pools rather than bypassing bank policy; its current
operation belongs in the [user guide](../../docs/interest-fix.md).

Evidence levels below apply to individual claims, not whole layouts or current
plugin acceptance. Historical performance describes implementations before the
C-service port (`2f38c6b`), not the current binary.

## Target and provenance

All addresses are **RVAs**, unless explicitly labelled VA, for the supplied
English Victoria II: Heart of Darkness 3.04 x86 executable: SHA-256
`62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648`, file size
12,294,656 bytes, preferred base `0x00400000`, image size `0x01092000`.
Resolve against the loaded module; a signature match alone does not prove
semantics, ownership, calling convention, or safe mutation.

Expected bytes and field provenance live in the
[catalog](../v2game-3.04.toml); evidence levels are defined in
[the mapping guide](../README.md).

The community [Loans wiki, permanent revision 22984](https://vic2.paradoxwikis.com/index.php?title=Loans&oldid=22984)
describes daily debt-times-effective-rate interest divided by 30 and omitted
POP interest, possibly an intentional money sink. This is not proof of engine
internals or a measured world-money effect. The native integer calculation and
distinct domestic/foreign modifiers remain authoritative.

## Established pipeline and units

### Borrower → named bank

`CCountry::PayDailyInterest` (RVA `0x00123c30`, `verified-runtime`) reads
creditor entries through country `+0xe8c`, multiplies debt `+0x18` by interest
`+0x10`, and updates paid byte `+0x20`. Destination tag/ordinal are creditor
`+0x08/+0x0c`. A nonzero ordinal resolves a country and credits its bank pending
interest `+0x20`; VA `0x00524022` skips this credit for ordinal zero.

| Claim | Evidence level and meaning |
| --- | --- |
| Domestic debt is a named self-creditor | `verified-static-callsites`: `TakeLoan` RVA `0x00122910` passes the debtor's own tag/ordinal (`+0x1c/+0x20`) through `CanTakeLoanFrom` and `TakeLoanFrom`, using the same nonzero-ordinal credit path as foreign debt. |
| Ordinal-zero interest has no named bank destination | `verified-runtime` path, with static creation at VA `0x00522c24`–`0x00522c27`: literal `---`, ordinal zero, then `TakeLoanFrom` at VA `0x00522c6a`. The raw key is `0x2d2d2d`; localisation `SHADOWY_INVESTOR` displays “Private Investors”. Vanilla `SHADOWY_FINANCIERS_MAX_LOAN_AMOUNT` is 1500. |
| Creditor rate/principal use /32768 | `verified-runtime`: serialized interest `0.01999` corresponds to raw `655`; debt `14969.11719` to raw `490508032`. Observed treasury and principal reductions equal the requested repayment amount with unchanged identities; this is not a claim about every repayment branch. |
| Bank money, lent money and pending interest are distinct | `verified-runtime`: bank `+0x10` is serialized `money`, `+0x18` is `money_lent`, `+0x20` is temporary interest, not bank cash. Repayment reduces lender `money_lent` only for resolved nonzero ordinals and clamps at zero. New and existing ordinal-zero origination differ; see [fiscal evidence](fiscal-credit-boundaries.md#origination). |
| Named-bank credit occurs inside `PayDailyInterest` | `verified-runtime`: destination pending interest increases during the call. The destination may be another country; the debtor's own bank is not a general transfer signal. |

**Treasury delta is not a universal conservation signal.** The insufficient-funds
helper prepares debtor, zero, and the requested 64-bit amount before `TakeLoan`
at VA `0x005257a8`. The shortfall/default path can refund canceled construction
inside `PayDailyInterest`. Net treasury movement therefore includes unrelated
changes and cannot identify Private Investor interest or establish named-transfer
conservation.

### Named bank → state pool

`CBank::DistributeInterest` (RVA `0x000f5bf0`, `verified-runtime`) has one direct
caller, the post-country bank loop at RVA `0x00285f0f` (VA `0x00685f0f`). The loop
loads each country's `+0xe88` bank after country daily updates. Owner is bank
`+0x08`; pending interest is signed 64-bit `+0x20/+0x24`, and
reserve/denominator is `+0x10/+0x14`.

The following arithmetic is `verified-static-callsites`; ordinary state
allocation and pending-interest clearing are also `verified-runtime`:

1. Positive pending interest is added to bank `+0x10` if that field is at most
   the integer conversion of `33.268` (33).
2. Otherwise each state share weights the **original** pending amount by state
   savings `+0x258`, clears the low 15 product bits, divides by the bank
   denominator, and adds to state interest `+0x260`. Signed 64-bit division
   truncates toward zero.
3. Bank pending interest is cleared on every return path.

The local remainder starts as the pending amount. An ordinary share is added to
the state and subtracted from the remainder. An oversized share is clamped to the
complete remainder, but that branch **does not zero the remainder before
continuing**. A positive final remainder is discarded. Truncation can therefore
lose interest; a denominator smaller than traversed savings can cause an early
clamp and potential over-allocation. Runtime over-allocation and the low-reserve
branch are not established by the observations summarized here.

State allocation is not bank-to-state conservation: the state savings weights
need not sum to the bank denominator, and pending interest is cleared even when
part remains unallocated. Why those weights differ remains unresolved. A
1000:1 state-to-POP conversion does not repair native bank-allocation loss.

A direct-creditor payout bypasses recapitalization and residual loss; its exact
bank-to-POP conservation does not establish state-pool conservation.

### State pool → POP: missing native consumer and verified scale

Save parsing/formatting names adjacent state fields `savings` (`+0x258`) and
`interest` (`+0x260`). Reviewed direct-displacement references cover construction,
copying, parsing, formatting and the bank writer, but no gameplay subtraction,
clear or POP payout consumer. Aliased indirect access remains possible. Native
state-interest accumulation without observed index-7 cash flow supports this
narrow absence claim, not proof that the engine never pays interest.

POP savings `+0x250` are signed 64-bit values at **1,000 times state money
scale** (`verified-runtime`). Conversion uses global RVA `0x00b0b168`, raw
`32768000` (`1000.0` with 15 fractional bits). Summed per-POP truncated `/1000`
savings differ from stored state savings by 19–116 state raw units. For example,
raw POP savings `1,909,522,541` yield a per-POP truncated sum of `1,909,394`,
whereas stored state savings are `1,909,375`.

Recomputed POP savings and stored state savings are therefore not
interchangeable. Paying an already-created state pool at `1000:1` is a separate
operation from reproducing native bank allocation.

## Verified traversal and mutation boundaries

These are narrow fields and call boundaries, not a general engine object model.
The broader state layout remains provisional.

| Boundary | Level | Native contract / limit |
| --- | --- | --- |
| `DailyUpdate` RVA `0x00108590`; interest call `0x00108d3e` | `verified-current` function; `verified-runtime` boundary | Sole direct `PayDailyInterest` call, country pointer already on stack, `ret 4`; continuation `0x00108d43` (VAs `0x00508d3e/0x00508d43`). |
| Post-country bank call `0x00285f0f` | `verified-static-callsites` | Complete five-byte `e8 dc fc e6 ff`; bank in EDI; continuation `0x00285f14` (VA `0x00685f14`). |
| Country states `+0xe44/+0xe48/+0xe4c` | `verified-current` | Head/tail/count maintained by creation callers; node data `+0`, next `+8`, null termination. |
| State extent and pool slots | `verified-static-callsites` extent; `verified-runtime` savings/pool fields | Constructor RVA `0x000cdc60` (VA `0x004cdc60`); callers allocate `0x290` and constructor initializes `+0x258/+0x260` to zero. |
| State provinces `+0x48` | `verified-runtime` | Four-byte game-state province indices, not province pointers. |
| Province POP vectors `+0x194/+0x198` | `verified-runtime` | 16-byte list records with count/tail and linked POP storage. Observed list count 13 is not a universal limit. Native traversal includes RVA `0x000a9362`. |
| POP payload/node/next/savings | `verified-static-callsites` extents; `verified-runtime` traversal | Payload/value stride `0x278`, node allocation `0x288`, next `+0x27c`, savings `+0x250`; separately allocated nodes, not a contiguous node array. |
| `CPop::GiveMoney` RVA `0x0055a5f0` | `verified-runtime` | EAX=POP, ESI=cash-flow index, stack 64-bit amount, `ret 8`; narrow money/indexed-flow/total effects below. |
| Interest cash-flow index 7 | `verified-static-callsites` presentation; `verified-runtime` slot effect | Presentation RVA `0x00566440` reads `+0x210`, index 7 of `+0x1d8`, and formats `POP_DAILY_INTEREST`. Presentation behavior itself is not runtime established here. |

**POP extent (`verified-static-callsites`).** Value-copy helper VA `0x004c0af0`
advances source/destination by `0x278` at VAs `0x004c0b38/0x004c0b45`;
constructors initialize/copy through dword `+0x274`. List creation allocates
`0x288` at VAs `0x004b8d8e/0x004be161` and passes the unchanged base to copy
constructor VA `0x00955450`. Previous/next/byte marker are outside the payload
at `+0x278/+0x27c/+0x280`. Marker lifecycle and trailing padding are unclaimed.

**Native money effects (`verified-runtime`).** At the observed unclamped values,
index-7 amounts `+1000/-1000` change POP money `+0x180`, interest flow `+0x210`
and total flow `+0x218` by the same signed amount. Savings `+0x250` remain
unchanged. This establishes narrow field effects, not behavior at every
clamp/overflow boundary or the contents of every other flow slot.

The complete direct-call scan of `GiveMoney` has 15 callsites selecting indices
0–6, none 7. Indirect/inlined consumers remain outside this negative evidence.

A state can receive another pool from a later bank in the same daily pass.
Recipient uniqueness is therefore a **single-bank-callback** invariant, not a
day-wide invariant. Native payments across multiple POPs are not atomic;
completed calls have no rollback guarantee, and replaying a whole pool after
partial payment could double-pay. These constraints do not establish current
plugin recovery, save compatibility or multiplayer behavior.

## Historical performance constraints

The following state-pool comparisons use the same vanilla save at speed 5 over
3,650 days. Source revisions are unavailable. They precede the C-service port
and describe different instrumentation and cleanup costs:

| Measurement scope | Baseline elapsed s / days per s | State-pool fix elapsed s / days per s | Interpretation |
| --- | ---: | ---: | --- |
| Lifecycle-only telemetry | 369.927 / 9.86682 | 561.080 / 6.50531 | Elapsed +51.7%; throughput −34.1%. |
| Full launch-to-exit, diagnostics/telemetry off | 302.085 / 12.0827 | 339.478 / 10.7518 | Includes loading/entry/shutdown; 89.0% baseline throughput retained. |

The latter implementation used once-per-session/after-failure cleanup, selective
positive-pool POP traversal, once-per-callback membership checks and amortized
signature/session checks; recapitalization did not trigger payout work. The
current daily-cleanup port has a different cost profile. Neither comparison is
a current or cross-save/mod/machine performance claim.

Historical **direct-creditor**, rather than state-pool, performance and callback
costs are in [telemetry evidence](telemetry.md#historical-performance-constraints).
Those comparisons cannot measure the cost of preserving native bank policy.

## Known limits

- Negative static evidence cannot exclude every aliased native consumer; native
  observations cover only the stated fields and phases.
- State allocation is not bank conservation. Native recapitalization, residual
  loss and potential early-clamp behavior are separate from state-to-POP payout.
- Complete default accounting, bankruptcy effects and comprehensive world-money
  categories remain unresolved. Deposits, claims, accumulators and liquid
  balances must not be summed into a money-supply identity.
- Historical mapping and performance evidence does not establish current-port
  runtime acceptance, engine lifecycle safety, UI presentation, save compatibility
  or multiplayer behavior.
