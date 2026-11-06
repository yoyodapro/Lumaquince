<!--
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Differences from Lichii and Eglantine

Lumaquince is a focused development fork, not a separate emulator written from scratch. Its base includes substantial Lichii and Eglantine work, and earlier work from Eden, yuzu and Sudachi.

The descriptions below separate inherited facilities, changes retained from the fork's development predecessor and the later combined fixes. “Implemented” describes source behavior. It does not mean that a performance benefit or every affected game's correctness has been demonstrated.

The comparison is against the source snapshots used for this preview. Upstream development can move independently; this is not a claim that current upstream branches lack every listed change.

See the packaged [VALIDATION.md](VALIDATION.md) for exact build, check, dependency and package-review results for Lumaquince 0.1.0-preview.1.

## Inherited facilities

The existing GPU front/back split, macro execution/JIT, descriptor fast paths, texture conversion and large-ASTC trimming policies, software matrix lowering, debug tools and ordinary pipeline caches originate upstream. Lumaquince does not claim those as new inventions.

The split's accuracy and safe-DMA eligibility checks remain. Required sparse-binding waits and the existing bounded query-sample fallback remain. This fork does not force parallel GPU execution through modes whose ordering requirements have not been proven.

The built-in Max Resolution option is inherited and separate from the timing experiment. Resolution scaling, presentation pacing, rendered frame rate and guest simulation speed are distinct controls.

## Retained cache and command work

| Area | Retained change | Practical limit |
|---|---|---|
| Task/mesh persistence | Versioned, framed shader environments retain the state needed to serialize and reload supported task/mesh pipelines. Records have bounds, checksum and stage/state validation. | Device capability checks still apply. Guarded/trap-prefix persistence remains excluded; no compute-only record is reclassified by guessing a task/mesh stage. |
| Encountered pipeline preload | Compatible previously recorded pipeline keys can be queued at startup. Translation pools are recycled during preload. | The cache cannot predict or prebuild unseen shaders, future scenes or all possible pipeline combinations. A queued entry is not proof of successful compilation. |
| Descriptor storage | Storage grows according to the inclusive descriptor limit and retains its high-water capacity. Newly exposed entries are invalidated. | Smaller element counts are not a measured whole-process memory or FPS saving. |
| Gathered buffers | Source-owner tracking and completion-based retirement cover gathered fallback copies. In-flight resources stay retained until their completion contract permits release. | This is not a general asynchronous sparse-binding replacement or proof against every device-loss path. |
| Command preparation | Exact root-uniform and index-binding reuse, conservative clean-upload checks, numeric binding memoization and writer filtering reduce selected repeated host work. | Keys remain scoped to runtime/mapping state; conversions, unknown writers and required refreshes remain conservative. No broad dependency-driven scheduler is implemented. |
| Native uniform refresh | Eligible mapped banks can refresh directly, while precise GPU-owned spans stay protected and mixed CPU/GPU ownership takes the required synchronization path. | This does not replace the complete memory tracker or establish a game-specific hit rate. |
| Worker/callback lifetime | Owned callbacks and bounded spinning followed by parking preserve completion, cancellation and shutdown ownership. | Not every callable allocation is eliminated, and spin tuning is not an automatic performance policy. |
| Texture conversion | Selected large, supported odd-pitch layouts use bounded owned-snapshot workers, with a serial fallback and a join before upload. | Eligibility is narrow. Texture quality, sampling and inherited fine-mip trimming policy are unchanged. |
| Query bookkeeping | Single-bank fast paths and live-bank cursors avoid selected allocations and repeated walks. | The inherited sample-discard bound remains; lossless accumulation is unfinished. |
| Clears and uploads | Clear coverage, source ownership and deferred-clear ordering are preserved across copies and image writes. | General image aliasing and all producer/consumer combinations remain open work. |
| Software matrix path | A software K8 unroll is retained with the existing numerical contract. | No qualified hardware/cooperative-matrix replacement is shipped. |

## Shader correctness

The combined source includes the following targeted shader changes:

- Compare-and-swap retry loops use the word returned by the exchange when retrying. Associated packed half-float load typing and shared subword stores are corrected.
- High-register integer, shift, texture-gradient and atomic operand forms use aligned uniform mirrors where required, retaining aliasing, predication, channel masks and borrowed-register restoration.
- Depth-comparison descriptors retain floating-point image typing even if an invalid descriptor advertises an integer format.
- Vulkan mesh output supports point and line topologies as well as triangles, including index arity, mapping stride and fixed, default or authored point size.

These are not a complete fix for GPU-selected texture handles. In particular, a per-fragment or loop-dependent handle read from GPU storage cannot generally be resolved once per draw on the CPU. Invalid depth-comparison and numeric image-type bindings remain in the tested predecessor. Correcting CAS or a packed half-float load must not be described as fixing those separate binding errors.

Mesh topology support does not implement vegetation culling compute, GPU-varying texture selection or permission to omit unsupported draws.

## Readback, queries and memory

| Change | What it does | What it does not establish |
|---|---|---|
| Empty range-set search | Returns safely when there is no searchable range. | A general memory-management redesign. |
| Readback bounds | Clips synchronous downloads to their owning buffer and batches selected ranges. Exact asynchronous queued-byte selection is also retained. | Removal of required fences, drains or CPU publication, or completion of a fully asynchronous download architecture. |
| Query publication | Publishes pending GPU query reports before macro DMA reads by preserving synchronization, signaling, deferred submission and waiting. | A speedup from omitting necessary waits. |
| FIFO query copies | Preserves each 4/8-byte write and its source bytes; overlapping destination batches receive transfer ordering. Repeated upload copies also retain transfer-write dependencies. | Dropping writes or changing FIFO semantics. Overlap lookup is bounded with ordered neighbor searches rather than a quadratic scan. |
| Physical remaps | Notifies GPU address caches when a mapped page changes physical backing even if its entry type remains mapped. Identical maps avoid extra notifications. | Proof that a particular visible defect or slowdown was caused by a remap in gameplay. |
| Windows allocation cleanup | Clears ownership after a failed arena allocation and releases fallback backing with the required release form. | A measured FPS improvement. |
| Host-memory moves | Transfers owned members, empties the moved-from object, cleans an assignment target and handles self-assignment. | A claim that moves caused a prior gameplay crash; no production move caller was established in the reviewed snapshot. |
| Cheat-file scanning | Parses each matching Build-ID file once on Windows rather than twice through case variants. | Bundled cheats, inventory edits or activated game modifications. |
| Texture logging | Compares effective capped/overridden thresholds before logging a change. | A changed VRAM cap, garbage-collection or residency policy. |

## Vulkan resources and synchronization

The following fixes are deliberately narrow:

- Descriptor-buffer mode avoids creating a texel buffer view that it does not use. The conventional view path, arguments and invalid-format guard remain.
- Applet capture rotates three resource slots and tracks final use before reuse. Noncoherent upload writes wait for all relevant allocation slots; stride changes drain before resource recreation.
- Capture and presentation teardown retain renderer finish/presentation waits. The split back owner flushes deferred fence submissions before relinquishing ownership.
- Image-copy, reinterpretation and MSAA source-layout restoration publish to image users with compatible access and stage scopes.
- Image uploads include the early depth/stencil stage required by their existing depth/stencil access masks.
- Win32 surface creation uses the actual window instance, with a controlled failure when it is unavailable.
- Pipelines declare dynamic binding stride when the corresponding extended dynamic state feature is enabled, including alongside dynamic vertex input.
- ASTC 8x6 linear/sRGB compatibility allows the existing conversion and format-list paths to create the necessary host views while retaining guest color-space selection.

A bounded predecessor replay no longer reproduced six earlier synchronization, copy, surface, stride and mutable-view error classes. Other invalid bindings remained. This is evidence about that workload and executable, not a clean Vulkan certificate, full synchronization parity with upstream, or validation of a newly rebuilt Lumaquince binary.

## Cache behavior

The reviewed combined source uses separate namespaces for translated Vulkan shader environments, OpenGL environments and the opaque Vulkan driver cache. Translation changes invalidate incompatible environments without rewriting old caches to impersonate a newer format.

The reviewed namespace values are Vulkan 57, OpenGL 52 and driver cache 52. These are internal cache versions, not application release numbers. A later source change may require a new namespace.

Only previously encountered compatible pipelines can be warmed. New areas, effects and combinations can still compile during play and cause stutter. No game-derived cache is supplied with the public release.

Task/mesh persistence is retained. Persistence for guarded/trap-prefix environments remains excluded/open; the implementation must retain reliable stage metadata rather than infer a stage from a compute-only validation result.

## Experimental timing option

The timing work derives from the optional Dynamic FPS approach in the [sharkiix-v2 release](https://github.com/Shakoquo/Lichii-sharkiix-version/releases/tag/sharkiix-v2), with additional context-preservation and input-validation work in this fork's experiment.

It replaces a guest timing calculation with an elapsed-counter-based step when that step exceeds the original by approximately 3%. The reviewed experiment retains the original operation, preserves scratch registers and status on normal return, and uses two persistent state words. Its elapsed delta is capped at 50 ms and smoothed by approximately one eighth per invocation.

The practical aim is movement closer to wall-clock speed when throughput falls below the game's target. It does not create more rendered frames, unlock complete emulator throughput, fix lighting or change inventory.

Important limits:

- For measured intervals longer than 50 ms, the cap prevents full compensation. The hook's relationship to updates and frames is not proven.
- Smoothing delays adjustment after frame-time changes; the 3% deadband creates a threshold.
- It is not proven that the hook runs once per frame or cannot be called concurrently or reentrantly. Its shared state is unsynchronized.
- Added memory and floating-point operations may behave differently under faults or unusual floating-point trap settings. Normal-return preservation does not establish asynchronous exception/unwind equivalence.
- Menus, pauses, transitions, physics, audio and cutscenes do not have comprehensive timing coverage.

An earlier experiment with the same reviewed payload received strong positive qualitative gameplay feedback and movement closer to normal speed. There is no controlled, complete-game speed measurement, and the Lake Hylia water strobing remains.

The built-in **Adaptive Timing (Experimental)** Add-On is enabled by default. It is restricted to title ID 0400486024DEC000 and the Main module Build-ID E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348. It also checks the expected module layout, original hook operation, zero-filled code-cave region and metadata-backed padding before applying the timing change. A different title, build or layout skips it.

Matching enabled external executable patches take precedence; the built-in timing change yields to them. The inherited Max Resolution Add-On remains separate and is preserved.

To disable timing, close the game normally, open its Properties → Add-Ons page, uncheck Adaptive Timing (Experimental), and restart the game. Recheck the checkbox and restart to enable it again on the supported build. An unchecked timing option retains the original timing calculation. Do not install an external patch intended for a different update.

## Defaults and excluded proposals

The editable shipped dist/nx2_switches.txt profile selects split mode 2, fence batching enabled, a 4096 draw-flush budget, encountered-pipeline preload enabled and frame tracing disabled. This reproduces the tested development profile through file overrides; it does not change the underlying C++ defaults.

| File setting | Shipped value |
|---|---|
| EDEN_NX2_SPLIT | 2 |
| EDEN_NX2_FENCE_BATCH | 1 |
| EDEN_NX2_FLUSH_DRAWS | 4096 |
| EDEN_NX2_PRELOAD_ALL_PIPELINES | 1 |
| EDEN_DKB_FRAME_TRACE | 0 |

In particular, the C++ preload-all default remains false and the native preload budget remains 128. The shipped EDEN_NX2_PRELOAD_ALL_PIPELINES setting can increase startup compilation and memory use by considering all encountered compatible pipeline records. Remove that line to restore the bounded used-list policy. Neither policy discovers unseen shaders or guarantees a stutter-free game. Spin budgets remain at their inherited defaults.

Lumaquince uses its own application-data directory. Update checking, web service and Discord presence are disabled in the release configuration. See [VALIDATION.md](VALIDATION.md) for the actual packaged profile and build-option checks.

Debug/TAS capture profiles, global clock changes, shader rejection, fog/shadow removal, forced texture handles, disabled comparison sampling and deletion of required waits are not credited as fixes in this preview. Diagnostics inherited from upstream remain opt-in facilities, not an automatically enabled gameplay configuration.

An attempted profile-guided optimization path did not produce a qualified optimized release. The Windows preview target is ordinary Release with link-time optimization; it must not be advertised as a measured PGO improvement.

Dependency-stamped command packets, a complete accuracy-preserving parallel scheduler, predictive preparation of unseen shaders, quality-preserving virtual residency, full GPU-indexed texture selection and a comprehensive multi-device correctness laboratory remain unfinished.


The release source omits an unused upstream opaque Clang PGO profile. This Windows build uses plain MSVC LTO. An inherited ignore rule is anchored to the root output directory so the audio source directory is included in clean checkouts. Inherited CI workflows are retained as inactive references under `.github/upstream-reference`; they do not publish or manage issues for this fork. Upstream source-image filename metadata was removed without changing image pixels or copyright notices.
