# GPU split wait and callback storage changes

By default, the front retains its existing spin windows: 16,384 iterations for drain/target waits and
4,096 for ring capacity. After that window it parks on a progress sequence instead of yielding
indefinitely. The back retains its existing 8,192-iteration pre-sleep spin. Thread priorities,
affinity, guest clocks, scheduler ownership and GPU completion policy are unchanged.

## Debug spin-budget measurement

The existing loopback debug server exposes `split_spin_budgets` for controlled same-scene
measurements. It is unavailable unless `EDEN_NX2_DEBUG_SERVER` starts that server. Empty params
read the policy. Optional `front`, `capacity` and `back` integer fields update it; `front` is
shared by drain and target waits. Each value must be between zero and its inherited default
(16,384 / 4,096 / 8,192). Unknown fields, fractional values, booleans, negative values and values
above those limits are rejected before any change. `{"reset":true}` alone restores defaults.

```json
{"method":"split_spin_budgets"}
{"method":"split_spin_budgets","params":{"front":1024,"capacity":512,"back":2048}}
{"method":"split_spin_budgets","params":{"reset":true}}
```

Responses report `previous`, `current` and `defaults`. One relaxed atomic snapshot supplies the
budget at each wait's entry; a change does not restart, notify or complete a wait already in
progress. Zero skips only the spin, using the same blocking path and completion predicate.
The three fields are published together, and debug-session end restores baseline defaults. The
API persists no settings. Normal defaults are identical to v07; the extra cost is one relaxed
policy load per relevant wait, not per spin iteration.

For a measured setting to survive normal Desktop launches, the existing executable-adjacent
`nx2_switches.txt` loader accepts these numeric tuning parameters:

```text
EDEN_NX2_SPIN_FRONT=16384
EDEN_NX2_SPIN_CAPACITY=4096
EDEN_NX2_SPIN_BACK=8192
```

Those examples are the unchanged baselines, not selected performance settings. `MaybeStart`
reads the parameters only when starting the split back thread. Each must be a complete decimal
integer from zero to its baseline maximum; an unset, malformed or out-of-range parameter uses
that field's baseline. No clamping or partial number parsing occurs. The existing loader lets
the user-config switches file override the executable's file, and the real process environment
overrides both. The debug API changes memory only: explicit reset means baseline, while a later
split restart can reread the configured startup values. A measured winner can be written to the
private switches file separately; no new binary is needed and the API never writes that file.

This tool does not choose a new default or claim a speedup. Compare repeated baseline/candidate/
baseline measurements in the same scene, recording policy, frame pacing and thread CPU evidence,
and restore defaults afterwards. Short-spin host research is not gameplay validation. The
production ring tests exercise baseline, zero and intermediate budgets, including a blocked idle
hook, target completion ahead of a blocked tail, ring-capacity wrap, restart, and repeated wakeup
from a zero-spin idle back thread. Request validation and coherent concurrent policy reads have
separate tests.

The sequence is only a wakeup hint. Completion still requires an acquire read of `consumed`
reaching the exact target; `Drain` additionally requires `BackQuiet`. The back notifies after
publishing consumed positions, after completing its idle hook and becoming quiet, and on exit.
The waiter snapshots the sequence before rechecking its predicate, so completion between that
check and parking changes the value that `atomic::wait` checks. An unrelated notification cannot
complete a wait. Stop remains a front-thread operation after production has stopped; exiting
the back does not manufacture consumption of cancelled entries.

Owned `std::function` values are constructed directly in the ring's byte storage. This removes
the unconditional `make_unique<std::function<...>>` wrapper allocation. Allocations internal to
`std::function`, such as large captures, remain. Records reserve enough padding for the standard
library's function alignment. Each callback is moved into local RAII ownership and its ring
object destroyed before invocation; shutdown destroys unconsumed callbacks without invoking
them. Recycled entries explicitly recreate their byte storage. The tradeoff is a larger ring
record than the previous two-word pointer entry; total ring storage remains 8 MiB.

## Source review

- [Lichii PR 161](https://github.com/zortos293/Lichii/pull/161): reviewed actual front wait/notify
  changes; its position-only `Drain` is not used.
- [Lichii PR 184](https://github.com/zortos293/Lichii/pull/184): reviewed actual backoff and
  notification changes; its position-only completion and unrelated bundle are not used.
- [Eglantine PR 593](https://github.com/Djihads80/Eglantine/pull/593): reviewed idle-hook
  synchronization. The local `BackQuiet` and exclusive scheduler ownership remain intact.
- Actual GPU split changes in PRs 166 and 179 were also reviewed. Their memory-write shortcuts,
  overlays, sink batching and coherence changes are not included here.

## Focused validation

The production split tests cover published/unpublished cancellation, exactly-once callbacks,
mixed entry traversal, ring wrap, repeated sessions, drain during a blocked idle hook, and
stop/restart during a callback. Added tests cover a parked target wait completing before a
later blocked callback, capacity waits through two ring wraps, and large over-aligned captures.
The wait helper tests cover exact spin bounds, completion in the final check/park window,
unrelated notifications, acquire visibility and 5,000 concurrent handoffs.

An intermediate version passed 436 assertions in 13 focused cases. That run predates the final
raw-byte/alignment adjustment and the decision to preserve the inherited spin counts. The final
production split and both test translation units compiled with the existing MSVC Release flags;
the final independent wait-helper executable passed 19 assertions in four cases. The final
integrated split test gate must be run after merging; the intermediate run is not its substitute.

## Controlled host probe (2026-11-05)

`tools/nx2/gpu_split_wait_probe.cpp` compares the former spin/yield loop with the production
bounded-spin/park helper. It alternates policy order over five trials, uses a single producer and
consumer, and changes no OS settings. Small delays use a producer-side steady-clock busy wait;
the delayed case requests a 1 ms sleep. Existing host load was not stopped or controlled.

| Producer gap | Handoffs per trial | Baseline median wall ms | Spin/park median wall ms |
| --- | ---: | ---: | ---: |
| Immediate | 20,000 | 6.273 | 5.539 |
| 2 microseconds | 5,000 | 12.324 | 12.830 |
| 5 microseconds | 5,000 | 27.585 | 28.232 |
| 10 microseconds | 5,000 | 53.417 | 53.995 |
| 20 microseconds | 5,000 | 106.452 | 107.531 |
| Requested 1 ms sleep | 64 | 1,004.934 | 1,002.781 |

The requested 1 ms sleep actually took about 15.6 ms per handoff on this host. In that case the
baseline waiter used 750–984.375 ms CPU per trial, while spin/park used 0–93.750 ms. The thread CPU
counter resolved increments of 15.625 ms; zero means below that reporting resolution. Short-gap
wall differences were roughly 1–4% except the immediate case, with visible trial variability.
These results establish reduced busy CPU for these long host waits, not gameplay FPS or latency.
They do not exercise the Vulkan scheduler or measure allocation throughput.

A rejected 64–256-spin adaptive prototype increased the 20-microsecond handoff median by about
24%. A later adaptive recovery prototype avoided that penalty in these narrow cases, but its
time thresholds were not adopted. Adaptive scheduling across workloads remains research.

## Sparse binding (F06)

`MultiRangeBufferCache::CreateSparse` still waits for its bind fence. A safe asynchronous
replacement must order the first consuming queue submission after the sparse bind and retain
its semaphore and bound-resource dependencies through completion, including retirement and
failure paths. The existing scheduler submission/retirement API does not establish that contract
merely by removing the CPU fence wait. No such change or gameplay speedup is claimed here.
