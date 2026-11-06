# Native graphics pipeline workers

Adapted from Lichii commit `5f6066b26bb56fc0d5f2925464979bf6cdc56666`
([PR #184](https://github.com/zortos293/Lichii/pull/184)) and the bounded cached-pipeline
prebuild portion of `b06d1818eeec9ab13f0df9822b58aef50a601483`
([PR #194](https://github.com/zortos293/Lichii/pull/194)). Original upstream contributions
and attribution are retained through those commits; the latter records its Frizti/Lichii source.

GPUBack still reads the environments and translates graphics programs to IR. Native graphics
SPIR-V emission and shader-module creation can run on the existing pipeline worker pool, followed
by driver compilation. Each job owns its IR pools, programs, runtime information and binding
state. It does not read guest memory or live engine registers. At most the pipeline worker count
of emission jobs is retained; a full budget uses synchronous emission. Up to four released IR
pools are retained for reuse. Broken-parallel-compilation devices keep their existing one-worker
limit.

Graphics build callbacks retain shared completion state. Captures are released before completion
is published, all waiters are notified after success or exceptions, and destruction waits for or
claims unfinished work. An already completed pipeline can therefore be evicted without leaving
a raw pipeline pointer in its old queued callback. The cache stops background prebuilds, drains
its workers and serialization, and destroys graphics pipelines while emission dependencies are
still alive. This helper covers graphics pipelines; compute retains its existing build path.

Pending native draws are never discarded. VulkanWorker waits at the existing pipeline-build
command and cannot claim a deferred native emission job; later commands retain their ordering.
Workers can overlap useful producer work, but long builds still hold draws and can backpressure
GPUBack. Tessellation presence is retained from shader metadata before modules become available.

The background prebuild pool considers only detached shader environments already present in the
disk cache and skipped by the ordinary boot preload policy. It retains at most 64 entries and
16 MiB of estimated input storage, and uses one or two background workers after boot preload.
Broken-parallel-compilation devices admit no speculative builds. First use takes an already ready
result without waiting; if one is unavailable the normal runtime path builds the pipeline.
Cancellation joins active builds before releasing external resources. This does not compile
unseen shaders and may compete for CPU or driver resources while playing.

Existing full-key preload deduplication, malformed-cache guards, two-session history, two-minute
save cadence, cache version separation and memory-budget residency policies are preserved.
Preload-all continues to preload all accepted cached keys, leaving no skipped work to speculate.

`EDEN_NX2_PERF_VERIFY=1` also emits reference SPIR-V on GPUBack. Worker output is compared word for
word and the reference is selected on mismatch. This adds work and is not a performance mode.
Runtime timing separates GPUBack work, worker emission and draw holds; overlapping totals must
not be summed as elapsed time saved.

The production-helper tests cover delayed completion, multiple waiters, exceptions, capture
release, late callbacks after eviction, queued/running teardown, reservation saturation,
SPIR-V equivalence with merged vertex control flow, prebuild admission budgets, cancellation and
adoption races. These are synthetic CPU/compiler checks, not evidence of Vulkan-driver or
gameplay acceptance. Performance requires equivalent cold-cache sessions and measured frame
times; no FPS improvement is assumed from moving work to a worker.
