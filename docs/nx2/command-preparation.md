# Command preparation and upload bookkeeping

This change retains every guest draw, index conversion, query and barrier. It reduces CPU work
around those operations:

- Uniform reuse now belongs to the Vulkan runtime rather than a template-global singleton.
  Native root-table uploads use the same byte-exact, scheduler-tick-scoped reuse as guest-memory
  uniforms. Padding is compared and uploaded as zeros; size changes invalidate reuse. Guest
  uniform bytes are still read on every bind, because native writes can bypass dirty tracking.
- Final host index bindings compare buffer, byte offset, index type and recording epoch before
  recording a redundant bind. Conversion passes execute first. General scheduler invalidation
  and render-pass ends change the epoch; host-u8 and quad binding paths explicitly invalidate.
- The memory tracker counts words containing CPU or untracked bits. Only a fully clean existing
  4 MiB region can bypass upload scanning. Missing regions and cached CPU writes remain dirty.
  The count follows every CPU/untracked mutation and cached-write promotion.
- Tracker notification ranges coalesce in their existing vector, retaining sorting, exact
  adjacency and batch notification semantics without a second allocation and copy.

The upload summary and index-binding optimization are scoped ports of Eglantine PR562
(https://github.com/Djihads80/Eglantine/pull/562). Index invalidation additionally covers this
fork's BindHostUint8IndexBuffer path. In-place notification coalescing follows Lichii PR184
(https://github.com/zortos293/Lichii/pull/184). Neither PR is imported wholesale. The native
root-table reuse and runtime ownership changes extend this fork's existing uniform reuse.

Validation: focused Windows MSVC Release tests exercise production helpers and the real memory
tracker, including 12,000 randomized mutations, cache-owner isolation, root-table zero padding,
size/content/tick changes and exact index keys. New index tests require the normal test target
entry `video_core/index_binding_state.cpp`. Compile probes cover vk_buffer_cache.cpp,
vk_scheduler.cpp and vk_rasterizer.cpp. Runtime correctness and performance in a game require
the combined candidate build and a controlled gameplay trial; these tests are not FPS evidence.

## Query bookkeeping follow-up

`SamplesStreamer::ReserveHostQuery` now remembers the active query's tail bank. A live query
holds references to every bank in its chain, so a bank in that chain cannot be recycled while
the query is extended. Each reservation therefore remains in its tail or appends a new bank.
The cursor replaces the repeated linked-chain membership walk. Snapshots, bank references,
slot counts, query values and the existing >256-slot discard guard are unchanged.

`ApplyBanksWideOp` keeps its first bank's min/max slot range inline. Additional banks use the
same ordered or unordered map as before, constructed only if needed. Ordered operations still
visit banks by ID; unordered result downloads receive the same range for every bank. The
accumulator is local to each call, so sync and download threads share no new mutable scratch.

The upstream survey reviewed Lichii PR121 and Eglantine PR655/163 diffs. PR121's AMD protection
stays intact; PR655's query-visibility flush is already present in this base. These bookkeeping
changes are local and do not implement a count-preserving replacement for that guard.

A standalone MSVC Release harness ran the five query-bank cases (390,697 assertions), using
the production BankPool, tail cursor and range accumulator. It substituted abort assertions
for the Catch runner to avoid linking shared libraries during the combined build. The actual
Catch source and vk_query_cache.cpp also passed compile-only probes. The tests compare the
cursor with the old chain walk through 400 randomized windows and bank recycling, and both
range modes with the original map union over 1,500 batches, including empty and zero-length
ranges. The normal combined Catch run remains the integration gate.

An allocation-counting CPU benchmark used 20,000 batches of 64 range additions, median of seven
alternating baseline/candidate runs. Single-bank ordered batches went from 40,000 heap
allocations to zero (164.8 to 126.6 ns/batch); unordered batches went from 20,000 to zero
(231.2 to 126.3 ns/batch). Four/64-bank ratios were 0.938/0.971 ordered and 0.916/0.992 unordered.
The build ran concurrently, so timings are indicative helper measurements, not gameplay FPS.

## Numeric storage bindings and native bank uploads

Scoped ports from Lichii PR194, head `438c03ab74f9f73003f91ed6491d4e47c951dd03`
(https://github.com/zortos293/Lichii/pull/194), reduce three kinds of draw preparation work:

- Storage bindings memoize only device/GPU addresses and numeric size. The fixed four-way cache
  belongs to each BufferCache, never retains a BufferId or sparse-segment state, and consumes
  144 KiB with no per-lookup allocation. Every bind still reads the guest pointer and, when
  applicable, the complete following size word. Keys include the unique address-space identity,
  its whole-map serial, alignment, read/write policy, native/table-pointer policy, and this
  branch's separate DLSS `mapping_size` policy. The last dependency is absent from upstream's
  patch and must not be dropped. Mapping, sparse mapping and unmapping keep the existing global
  serial updates. Content upload, host buffer lookup and multi-range resolution still follow.
- The existing once-per-frame native 64 KiB bank refresh uploads its mapped prefix directly,
  rather than falsely marking it CPU-modified and then retracking it. The existing GPU-modified
  exclusion, page-hole stop, alignment-stream fallback and regular dirty synchronization stay
  in place. Legitimate tracked writes within the same frame still upload. Immediate-upload
  policies also set `any_buffer_uploaded`, retaining the barrier required by those backends.
  This does not introduce a new per-frame byte snapshot or change root-table byte reuse.
  The direct path also checks precise `gpu_modified_ranges`: a CPU/service invalidation can
  hide a page's coarse GPU bit while untouched bytes on that page remain GPU-owned. Such banks
  retain the original forced CPU mark over the mapped bank followed by normal synchronization,
  which excludes those precise spans from CPU uploads. This fallback also refreshes unnoticed
  CPU writes on other pages of the bank; simply skipping the forced refresh would leave them stale.
- Compute and DMA plain register stores no longer advance the split's last-writer position.
  Native compute uses the same plain-method mask as CallMethod's direct register-store return;
  legacy compute and DMA use their execution masks. Macro and unknown method IDs remain writers.
  CallMultiMethod repeats one method, so it cannot hide an execution register later in an
  incrementing batch. FIFO execution, writer waits, sync boundaries and BackQuiet are unchanged.

No command-page batching or fence visibility relaxation from PR194 is included. Issue191's
exposure regression is a reason to preserve completion-before-publication, not to amortize guest
visibility across fences. Texture/per-pixel dependencies, draws, query values and barriers are
not elided by this change.

Validation: the production numeric cache helper passed 3 Catch cases / 50,291 assertions with
fresh descriptor reads, every policy/alignment dependency, owner isolation, global serial
changes and 25,000 randomized requests compared with an uncached oracle. Real BufferCache and
MemoryManager fixtures cover remaps, sparse holes, discontinuous mappings, replacement address
spaces at the same address, native/table/DLSS sizes, exact upload boundaries, GPU-owned exclusion,
tracked same-frame updates and immediate-upload barrier bookkeeping. The engine classifier
fixture checks every method below 0xe00 in native/legacy modes and conservative out-of-range
handling. These integration fixtures have compile-only validation locally; the combined test
runner remains their execution gate. Production compile probes passed dma_pusher.cpp and the
Vulkan/OpenGL rasterizers. Add `video_core/storage_binding_cache.cpp`,
`video_core/draw_preparation.cpp` and `video_core/command_writer.cpp` to the normal tests target.

A CPU-only MSVC /O2 benchmark used the production cache against a modeled locked interval-map
lookup and two translations, one million calls, median of seven alternating trials. For 1/160/512
repeated bindings it measured 14.23/20.31/30.61 ns uncached versus 2.38/2.85/8.61 ns cached; the
resolver ran only 1/160/512 times. With 8,192 churning bindings every lookup missed and time rose
from 48.43 to 59.59 ns (+23%). This is a helper working-set tradeoff, not a measurement of the
real MemoryManager, total draw preparation, GPU time or gameplay FPS. Runtime image correctness
and performance still require the combined candidate's gameplay trial.
