# Engine scheduling research

## Scope and evidence

Fresh factual synthesis of eleven `Engine/` notes in
[maxioten/Victoria2-Reverse-Engineering at commit
`e299a0214d32274239402e8ee22039e1d02e101f`][upstream]. The pinned checkout has
no license file; no upstream prose, code, diagrams, or patch payloads are
reproduced here. This is a research reference, not a supported engine API or
permission to reuse upstream implementation.

**All imported names, layouts, semantics, measurements, and safety claims are
`historical-unverified`.** Upstream's own static/inferred/measured labels do
not map to Smedley's evidence levels. Independently inspected instructions are
identified below; they establish only the stated bytes, operands, and direct
edges. No research claim is promoted by this import. Existing local evidence
retains its own narrower status.

Addresses below are **preferred-base VAs**, unless labelled RVA or field offset.
The supported executable has base `0x00400000`; runtime address = loaded module
base + RVA. File offsets require PE section translation, not subtraction of the
image base. Executable identity and evidence rules remain in the
[mapping catalog](../README.md) and [identity record](../v2game-3.04.toml).

## A day is scheduled across frames, not one monolithic update

Sources: [daily tick][daily], [country update][country], [engine structure][structure].
The useful model separates frame servicing, queued time commands, daily work,
and AI jobs deferred to subsequent frames.

| Boundary / VA | Imported mechanism and inspection lead |
| --- | --- |
| `0x00654d80`, proposed `CInGameIdler_Update`; vtable entry `0x00e0a350` | Frame-level coordinator for time, commands, rendering, and AI queue work. |
| `0x00682bd0`, proposed `ProcessGameTimeAdvance` | Accumulates elapsed time at world `+0xb14`; threshold, pause/state, host, and pending-job conditions gate submission of a time command. |
| `0x009af9f0`, proposed `CommandQueue_Process` | Executes commands indirectly and services the session; submission and execution need not occur in the same frame. |
| `0x0072ecf0`, proposed `CHourlyTickCommand_Execute`; vtable `0x00e12414`, slot 6 | Calls `0x00682ec0` at `0x0072ecfa`. Despite the hourly name, the cited advance is one day. |
| `0x00682ec0`, proposed `Update_World_and_Advance_Time` | At `0x00682fa5`, adds `0x18` to world `+0xb0c`; calls daily body `0x006859c0` at `0x00683ab1`. |
| `0x006859c0`, proposed `DayTick` | Province/POP passes, random preparation, two distinct country passes, economy/statistics, deferred AI work, and event scanning. Not established as a monthly-only routine. |
| `0x004b9c60` / `0x004b1a10` | Province POP traversal / proposed province daily update; cited calls at `0x00685b09` / `0x00685b49`. |
| `0x00508590`, proposed `Country_PrePass` | First country pass, called at `0x00685c2c`. Contains fiscal/relation work; distinct from the later country routine. |
| `0x005091a0`, proposed `Country_DailyUpdate` | Later country pass, called at `0x00685ed1`, with country and random-array stack arguments. Source reports `ret 8`. |
| `0x00489990`, proposed `Economy_Update` | Call at `0x00685f7e`, **after** the later country pass. Upstream's displayed chain puts this above that pass, but local instructions show the opposite order. |
| `0x0068f4c0`, proposed `ProcessPopStatistics` | Call at `0x00685fd6`; source identifies a TBB parallel operation, not proof that the whole tick is parallel. |
| `0x008a7cd0`, proposed `EventManager_DailyScan` | Call at `0x006861e8`; source reports gating by world `+0xd11 == 0`. Meaning of the mode byte is not established here. |
| `0x00687e30` | Separate monthly path from `0x00683b36` when the month changes; source associates it with casus-belli checks. |

World pointer storage is reported at `0x012588e8`; country-database pointer
storage at `0x012587e4` has count/array fields `+0/+4`. These are search leads,
not lifetime or readable-span guarantees. World `+0xadc/+0xae0` is used for a
country pointer range; do not equate its slots with playable or living nations.

### Existing `DailyUpdateEvent` observes the earlier pass

The current [hook declaration](../../kernel/include/smedley/events/dailyupdate.hpp)
uses **RVA `0x001085ae`**, preferred VA `0x005085ae`, inside the function beginning
at **VA `0x00508590`**. It is not in **VA `0x005091a0`**. The
[trampoline](../../kernel/src/events/dailyupdate.cpp) extracts `[ebp+8]` and replays
the displaced country access. Local disassembly shows the hook instructions
`53 8b 5d 08 8a 83 bc 15 00 00` after that function's prologue, and separate
calls from the daily body to the two entries at `0x00685c2c` and `0x00685ed1`.

Thus the existing event name must not be read as a post-daily-update boundary,
a hook on the later routine, or a once-per-world-day callback. Its current
catalog status covers the existing hook, not every behavior upstream assigns
to the containing prepass. Fiscal claims belong to
[interest evidence](../evidence/interest-payout.md) and
[fiscal boundaries](../evidence/fiscal-credit-boundaries.md).

### Calendar and later country work

Upstream assigns hours to world `+0xb0c`, with a 365-day year and month-length
table at `0x00f1027c`. Local [date evidence](../evidence/telemetry.md#dates-and-world-snapshots)
owns the runtime unit/anchor correlation: raw date is
`24 * (365 * (year + 5000) + zero_based_day_of_year)`.
Upstream's `0x029c55c0` equals `5000 * 365 * 24`, i.e. the year-zero offset,
not an instruction to subtract that value and then subtract another 5000 years.
The two upstream date descriptions are not mutually consistent.

The later country routine is reported to perform these operations:

| Work | Useful source lead / limit |
| --- | --- |
| Staggered POP updates | `0x00957ea0` for POPs selected by ID modulo current month length and day index. `0x004b9250` is interpreted as empty-POP merging; exact selection/index conventions need checking. |
| Player statistics and migration | Statistics candidates `0x00966760/0x00966880`; migration scoring, sort `0x0054b200`, then distribution `0x004ba430`. Statistical meanings and migration policy remain uncertain. |
| Leaders and military | Leader list `+0xbd4`, inferred age/death rolls; lists `+0x7b4`, `+0x149c`, `+0x148c` participate in incompletely identified military work. |
| Pairwise relation maintenance | Loop near `0x0050abf0`, call at `0x0050ac44` to `0x0092e290`, indexed through country `+0xbe8`. Source describes conditionally purging lists behind `+0x50/+0xa8/+0x2c/+0x30/+0x28/+0x20`; empty cases are cheap, not evidence the loop is dispensable. |
| Goods, rank, AI, and player alerts | `0x0052fcd0` called at `0x0050b6b9`; `0x0085ccb0` at `0x0050b0eb`; player decision-alert lead `0x0050b792`. Do not infer ordering from this summary alone. |
| Event maintenance | Pulse lead `0x0050b1f4`; country delayed-event vector `+0x68/+0x6c`, countdown `item+0x54` near `0x0050b5ce`. Province counterpart is `+0x384/+0x388`, near `0x004b1f13` in `0x004b1a10`. |

Event-resource loading `0x008a67b0` is not the daily scanner or event executor.
The country note distinguishes scanner `0x008a7cd0`, probability routine
`0x008ae380`, firing `0x008a8120`, and window construction `0x008a8f30`.
It describes a probability scaled per million and clamped to `1..1000000`;
its mode/MTTH units and all event layouts remain imported hypotheses here.
Full parser, trigger, option, and effect reconstruction is outside this file.

Skipping the later country pass is not a semantics-preserving optimization:
staggered POP cohorts can miss their monthly update, country countdowns advance
less often, and military, migration, pulses, and random consumption change.
Skipping only that pass does **not** by itself skip province countdowns in the
separate province routine. Earlier upstream experiments conflate these scopes.
No bypasses, event-scan cadence changes, code caves, or patch recipes are imported.

### AI work can delay the next day

Source: [daily tick][daily]. At tick end, eligible AI records from world `+0xa4`
are assigned to buckets at world `+0xb84` using country index modulo bucket
count. Reported records carry tag/index at `+0x30/+0x34`; buckets occupy 16
bytes with head, tail, count, and flag; nodes carry tag/index and links, with
next at `+0x0c`. Source reports one bucket in single player, not a universal
multiplayer ownership contract.

Frame call `0x006557c4` enters `0x006874c0`, which drains bucket work through
`0x0081e9e0` while countries still qualify (`country+0x208` AI pointer), stopping
when empty or after a **20 ms wall-time budget** (double at `0x00e456c8`).
The next time command waits for all buckets to empty as well as pacing/host
conditions. A budgeted queue is not a worker pool. Machine-dependent frame
boundaries do not imply that changing job order, interleaving, or RNG use is
multiplayer-safe. Upstream's frames-per-day observations are workload results,
not a fixed scheduler ratio.

## Speed selection and intra-day servicing are different controls

Source: [game speed][speed]; authoritative local evidence:
[speed control](../evidence/speed-control.md).
World `+0xb28` is index `0..4`, corresponding to displayed speeds `1..5`.
Native handlers `0x0072ee90/0x0072efe0` change it; the upstream console lead
`0x0064e608` is not a general supported speed-setting API.

| Index | Daily threshold and source VA | Intra-day service coefficient at `0x00f0956c + 4*index` |
| ---: | --- | ---: |
| 0 | `4.0 s`, `0x00f17b58` | `0.03` |
| 1 | `2.0 s`, `0x00f17b54` | `0.03` |
| 2 | `1.0 s`, `0x00f092fc` | `0.03` |
| 3 | `0.5 s`, `0x00f17898` | `0.04` |
| 4 | **`0.0001 s`**, `0x00e45bb8` | `0.06` |

Upstream repeatedly calls the last daily value `0.0004`; its listed bytes
`17 b7 d1 38` actually encode approximately `0.0001`. Local binary reads and
the consumer at `0x00682c59` agree with existing Smedley evidence.

The source describes `0x00682bd0` accumulating time at `+0xb14`, returning for
nonzero `+0xb20`, checking `+0xbb8` near `0x00682d05`, and adjusting globals
`0x012588e6/0x012588ec/0x012588f0`. Exact state meanings and host/client
invariants are not supplied by those offsets alone.

Inside long loops, `0x00685620` instead decides whether to service the
application pump `0x009df2b0`. Upstream describes its interval as the service
coefficient multiplied by float `0x013f2ae8` and `(float[0x012588f0] + 1)`;
reads begin around `0x0068568b`. This may re-enter frame/UI work during a tick.
It is not another simulation-day threshold or a proven whole-application FPS
cap. Upstream's hardware-independent frame-rate claim omits regular frames,
scale/state assumptions, and actual workload. Shared constants such as
`0x00f092fc` have non-speed consumers; changing them is not isolated tuning.

## Session polling can block even in single player

Source: [network layer][network]. Upstream identifies statically linked ENet
from error strings and socket imports, with this inspection chain:

| VA | Imported identification |
| --- | --- |
| `0x009afbb1` | Indirect session-service call within command processing, via object `+0x38`, virtual byte offset `+0x1c`. Intermediate dispatch frames were not all resolved upstream. |
| `0x00a61780`, vtable entry `0x00e42744` | Proposed session service. At `0x00a618cc`, passes host from `this+0x18`, event storage, and timeout from `this+0x20` to `0x00a89d00`. |
| `0x00a89d00` / `0x00a8b400` | Proposed `enet_host_service` / `enet_socket_wait`; wait call at `0x00a89e43`. |
| `0x00a8b477` | Indirect call through IAT VA `0x00c8a330`, WS2_32 ordinal 18 (`select`); nearby IAT `0x00c8a338` imports `WSARecvFrom`. |
| `0x00a5ff00` / `0x00a635c0`, `0x00a63830` | Base/client/server constructor leads; source reports timeout initialization to 15 ms / 35 ms. |

Upstream observed single-player loopback polling with the base timeout.
Polling can send, receive, dispatch, and then wait; a configured timeout is a
maximum wait, not mandatory elapsed time on every call. Upstream's blocking
samples do not establish Smedley latency, packet ownership, or a safe new timeout.

The misleading `FIX_SFX_MIXER_LAG` name in the pinned
[patch inventory][patches] refers to a **WS2_32 `select` IAT hook**, not an audio
mixer fix. Its reported caller RVA `0x0068b47d` is the return address after the
call at VA `0x00a8b477`, inside this network wait. The stated caller range
`0x00689c00..0x0068c000` is likewise module-relative. No timeout modification,
IAT hook, or patch-default recommendation is adopted here.

## Random streams and threading limit safe reordering

Sources: [RNG][rng], [threads][threads], [daily tick][daily].

| Lead | Imported claim / boundary |
| --- | --- |
| MT state VA `0x00f0ecf0`, index `0x00f0f6b0` | 624-word Mersenne-Twister-family state. The RNG note expresses these as RVAs `0x00b0ecf0/0x00b0f6b0`; they are not VAs. |
| `0x009b7610` / `0x009b7700` | Next-value / regeneration candidates. Their addresses do not establish a general C calling convention; no callable wrapper is supplied. |
| Daily preparation near `0x00685b6b` | Source reports 1000 draws per day for an array passed into country work. This is not the total number of daily RNG draws or a complete stream map. |
| CRT `rand`, `0x00ab047b` | Source assigns the AI daily decision roll to a separate CRT stream with per-thread state, not the global MT stream. Seed/reset/persistence and all consumers remain untraced here. |
| TBB initialization | Source reports `task_scheduler_init::initialize(-1)`: default scheduler concurrency, not a specified count of simulation workers. |
| `0x0068f4c0` | Reported `ProcessPopStatisticsFunctor` parallel range invokes `0x00520150` then accumulator `0x00969760`. |
| `0x0068f390` / `0x0099da20` | Reported spatial-registration and graphics parallel-work leads; not proof that every invocation or their callers run off the game thread. |
| Vtable `0x00e439f8`; `0x00a7aed0 → 0x00a7b0c0 → 0x00a7b090` | Generic CreateThread wrapper, entry, and dispatch through worker object `+0x18`, virtual slot 1. Actual expensive job ownership remains unknown. |

The source characterizes most daily work as serial; identified parallel sites
do not prove that all other engine work is single-threaded. Neither the MT
routine nor surrounding shared state is established as thread-safe. Consistent
MT consumption is necessary for deterministic behavior, but equal binaries or
equal draw counts alone do not prove multiplayer synchronization. Reassigning
CRT-random work to another thread can also change its stream.

Concrete upstream blockers to naive parallelization include:

| Work | Shared state / ordering hazard reported upstream |
| --- | --- |
| `0x00520150`, `0x00969760` | Shared 64-bit additions at object `+0x8d8+i*8`, `+0x900+i*8`, `+0x13e8/+0x13ec`. The existing TBB site's partitioning or synchronization is unresolved; these observations do not prove it races, nor that arbitrary callers may run concurrently. |
| `0x00484060`, `0x00485e40` | Per-state path `0x004a46e0 → 0x004a5170 → 0x009b7950 → 0x009b7610`; shared scratch `0x013f24f4..0x013f25a8`; application-pump re-entry; saturation around 99999 makes accumulation order significant. |
| `0x00515490`, proposed country recalculation | Object creation/registration via `0x005163c0` and `0x004a7bc0`; unlink/deletion through world `+0xa84` and `0x005f2ae0`; shared relation writes `+0x38/+0x71`. Exact recalculated quantities remain unresolved. |
| `0x00508590`, prepass | Writes relations on other countries (`+0x40/+0x44`) and other treasuries (`+0xe78`). Country partitioning alone does not isolate writes. |

The thread note's suggestion that triggers look read-only is not a verified
parallel-evaluation contract: it does not resolve helper side effects, lazy
initialization, object lifetime, or concurrent mutation. Its floating-point
warning is also a limit, not a ready fix: new threads cannot assume the game's
x87 control word/MXCSR, and Direct3D may affect precision. Reproducing both is
not sufficient to remove shared-state races or establish deterministic reduction.
Private accumulators and a merge, suggested in [conclusions][conclusions], still
require verified partitioning, overflow/clamp behavior, and merge order.

## Loading is a separate lifecycle boundary

Source: [load initialization][loading]. Proposed entry `0x00662010` performs
post-load setup and transition work; treating it as recurring economy work or
calling it to refresh state would be unjustified.

| Source observation | Useful lead / unresolved interpretation |
| --- | --- |
| Phase and display | Primary object's `+0x1e08` set to 3; values from another object's `+0x64/+0x68` feed `+0x1e10/+0x1e14/+0x1e18`. Load-phase, dimensions, and aspect-ratio names remain hypotheses. |
| Reset and repair | Traversed entity field `+0x0c` cleared; another field `+0x34` may be assigned when zero, with limit `0x186a0`. Exact identity/repair contract is unknown. |
| Subsystem calls | `0x00521560`, `0x0050e540`, `0x005068f0`, `0x00521fd0`, `0x0050ea30`, `0x00530e40`, `0x0052fcd0`, `0x0051c890`, `0x005143b0`, `0x00517af0`, `0x0051eb40`. Inclusion during loading does not make each callee initialization-only. |
| Configuration | Repeated binding/key resolution and caching at offsets including `+0x11c8/+0x1268/+0x12b8/+0x1308`; toggle candidate `+0x7e0`. No verified key or bit meanings. |
| Spatial setup | Mode condition at world `+0xd11`; entity `+0x1d0` assigned before `0x00407b10` and `0x0068f390`. “Headless” is an upstream interpretation, not a supported mode contract. |
| Transition | `0x006480a0` followed by `0x00648040` with state value `0x0b`; load-time logging, an approximately `0x3c0` allocation, and boolean candidate `+0x3b8` are nearby leads, not an identified minimap controller. |
| Callbacks | Source describes virtual byte offset `+0x24` through slots at primary object `+0xd80`. Its stated `0..0x20` byte range with stride 4 describes **eight** slots, not its claimed 32; actual loop bounds/ownership require independent reconstruction. |

Smedley's [campaign evidence](../evidence/campaign-automation.md) separately
identifies save-load completion and campaign entry. Nothing here authorizes
retaining pointers across either transition.

## File checksums, runtime comparisons, and menu transitions differ

Source: [checksum investigation][checksum]. Its “confirmed bug” and “safe patch”
phrasing is not adopted: the counter's semantic identity and checksum
contribution have not been independently established here.

| Path | Imported mechanism / limit |
| --- | --- |
| File/history input | `0x006377a0 → 0x009da4b0 → 0x009da400 → 0x009939a0`; final routine reads with PhysFS. Strings at `0x00e07d2c/0x00e07cfc` identify checksum logging. |
| File calculation | Reported byte-wise Fletcher-style running sums modulo `0xffff`, combined as `(sum1 << 16) | sum2` around `0x00993a5f/0x00993a64`. This is the source's description, not a verified complete file-selection/hash specification. |
| Runtime consistency | World-advance routine `0x00682ec0`, comparison/logging near `0x00683180`, branch `0x006831a0`, and vector comparator `0x0068f340`; log string `0x00e0aa1c`. Comparing stored values is not evidence that this path generates them. |
| Resign dispatch | `RESIGN` string `0x00e0a0ac`, routines `0x00676500/0x006766a0`. Dispatch uses the context pointer at handler `+8`, virtual byte offset `+0x114`. Handler vtable `0x00e0a7e4` and its `0x009c4300` entry do not identify that context's concrete target. |
| Screen-load counter | Constructor candidate associated with `interface/backend.gui`: read fourth stack argument, load its `+0x30`, increment EAX at `0x005f8268`, store back. Class names `CBackEndIdler/CReloadDispatcher` and “checksum context” remain historical labels. |

Upstream reports that altering file-checksum output or suppressing the mismatch
branch did not stop the menu-return checksum change. Those negative experiments
constrain the proposed explanation, but do not prove that the screen-load
counter is the sole cause, that it is erroneous, or that removing it is safe.
The observed Resign → results → menu sequence does not locate all mutations.
An `exit_button` string at `0x00e13c5c` is not a resolved native exit handler.

Local instructions confirm the increment sequence at `0x005f8262..0x005f826b`,
not its checksum meaning. The increment's **RVA is `0x001f8268`; file offset is
`0x001f7668`** in the supported PE. The source contains a correct conversion
note but then repeats the wrong file offset `0x001f8268` in tables and patch
instructions. Those recipes are excluded. No checksum bypass, menu-counter
change, or promise of checksum stability is imported.

## Performance leads are not Smedley benchmarks

Source: [profile][profile]. Upstream combines redirected-call timing with
roughly millisecond sampling that suspends the game thread and walks stacks.
It describes TGC, early/new and later saves, and maximum speed, yet also labels
one budget “vanilla.” The precise patch state, workload, timing inclusivity,
and representativeness are insufficiently established for local comparison.
**No upstream duration, percentage, speedup, or allocator result is adopted as
a Smedley benchmark.** A large inclusive time also need not be CPU work: session
polling can wait inside indirect calls missed by the call-timing hooks.

Useful locality/workload hypotheses, with no measured cost attached:

| Data path | Source inspection leads |
| --- | --- |
| Pairwise relations | Country `+0xbe8` points to per-other-country relation objects indexed by country `+0x20`. Prepass loop `0x00508da7`, sampled load vicinity `0x00508dee`, reads `+0x2c/+0x30`; even tags without provinces may have meaningful relations. |
| Other relation scans | Market-capacity lead `0x00522cf0`, load vicinity `0x00522e27`, reads `+0x34`; AI target lead `0x0084b0f0`, vicinity `0x0084b29d`, reads `+0x34/+0x3c`; recalculation vicinity `0x00515fcf` and purge `0x0092e290` touch additional lists/fields. Pairwise traversal suggests scaling costs, not permission to drop entries. |
| Country-pair values | `0x0052fcd0`, load vicinity `0x00530388`, reads `other+0x1580[self]`, described as 8-byte entries. |
| POP traversal | Province `+0x194` holds per-type 16-byte list records, with POP next candidate `+0x27c`; traversal lead `0x004b9c9b`. Other reported hot accesses: `0x0095eb63` to POP `+0x11c`, `0x0095a66d` to `+0x180..+0x21c`, `0x00969889` to `+0xa4`. Not a complete safe POP layout. |
| Country flags | Trie lead `0x009e6c10`, sampled vicinity `0x009e6c2f`; pointer chasing per character is an upstream locality hypothesis. |
| Allocation | Statically linked CRT candidates: malloc `0x00ab049c`, free `0x00aaeed7`, realloc `0x00ac4320`, calloc `0x00abf700`, msize `0x00ab71db`, new/delete `0x00aae9af/0x00aae91b`, heap initialization `0x00ababd8`. Heap policy and allocator cost are not independently verified. |

The upstream 683-tag example is mod-specific, not a supported array bound or
proof that every pair object always exists. Samples concentrated after loads
are suggestive of locality stalls, not direct cache-miss measurements.
Profiling must relocate addresses for ASLR, distinguish inclusive/exclusive
time, reject stale stack return addresses, validate call targets, and avoid
overflow in stack-range checks. Neither linked-list replacement nor heap
replacement follows safely from these observations.

## Broader engine notes and rejected conclusions

Sources: [engine structure][structure], [conclusions][conclusions]. Entry
`0x00ab0f91`, loop candidate `0x009df550`, and engine/WinMain candidate
`0x009796b0` are orientation leads, not a reviewed direct-call graph.
Their early economy label `0x0068bf00` must not be treated as an alias for
`0x00489990` without tracing the callers. Likewise, the older “monthly” label
for `0x006859c0` conflicts with the later daily-tick analysis.

The source describes layered market processing, not one replaceable economy
function: market `0x00484060`, supply lead `0x00482930`, price path near
`0x00482b00`, signed 64-bit arithmetic, and 8-byte stockpile entries associated
with `0x0047dca0`. Price bounds are reported at `0x00e45c30` and `0x00e45c28`
with factors `0.2` and `5.0`. The suggestion that modifying only bounds is
“safest” is not a validated mutation boundary; no changed factors or market
patches are adopted. Loan lead `0x00523400` does not supersede local fiscal
evidence.

Console candidate `0x00420eb0` reportedly dispatches tokenized commands;
`fow` toggles `0x013f080c` and mirrors into the object reached via `0x01258a74`
at `+0x6bc44`. Upstream reports broken UI/terrain from rendering branch edits
in `0x0099da20/0x006592f0`, with condition lead `0x006594b4`. A toggled flag
alone is not a permanent fog-of-war override. These and the reported crashes
after removing event calls argue against importing bypasses, not for a
supported patch or a causal theory of battle maintenance.

Two known layout misinterpretations are explicitly excluded:

| Incorrect interpretation | Local evidence that takes precedence |
| --- | --- |
| World `+0xb3c` as special modifiers | Local [telemetry evidence](../evidence/telemetry.md#country-and-province-candidates) identifies a wars-container candidate with narrow correlation; it does not establish a complete war layout. |
| Country `+0xe4c` as an AI score | Local [state traversal evidence](../evidence/interest-payout.md#verified-traversal-and-mutation-boundaries) identifies state-list head/tail/count at `+0xe44/+0xe48/+0xe4c`. A count used in an AI comparison is still a count, not a new score field. |

## Instruction-level evidence and limits

In the [supported executable](../v2game-3.04.toml), direct calls connect the daily
body to `0x00508590/0x005091a0`. Their operands establish the hook placement and
the later `0x00489990` call position described above. PE-translated reads match
both timing tables and the fastest constant. Instructions also show the
`+0x18` date increment, session call `0x00a618cc → 0x00a89d00`, IAT call at
`0x00a8b477`, and screen-counter increment. The cited IAT imports WS2_32 ordinal
18 (`select`).

These observations do not establish full execution order through every branch,
engine object identity/lifetime, native-call ABI, thread affinity, seed policy,
checksum purpose, multiplayer determinism, or any performance improvement.
Only existing local evidence supports current Smedley behavior; this file
supplies bounded leads for further investigation.

[upstream]: https://github.com/maxioten/Victoria2-Reverse-Engineering/tree/e299a0214d32274239402e8ee22039e1d02e101f
[daily]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/daily-tick.md
[country]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/Country_Daily_Update.md
[network]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/network.md
[speed]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/game-speed.md
[rng]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/rng.md
[threads]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/threads.md
[loading]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/loading-initialization.md
[checksum]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/checksum.md
[structure]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/engine-structure.md
[profile]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/performance-profile.md
[conclusions]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Engine/conclusions.md
[patches]: https://github.com/maxioten/Victoria2-Reverse-Engineering/blob/e299a0214d32274239402e8ee22039e1d02e101f/Patch/PATCHES.md
