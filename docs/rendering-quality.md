<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Rendering quality work

This describes unreleased source changes. The published Preview 1 package is separate; its
validation records apply to that exact package. Source review and synthetic tests do not prove
correct GPU output, normal game speed or whole-game compatibility.

The development goal is consistent resource ownership, ordering and state throughout the
renderer. Reported glare, stretched triangles and black materials remain gameplay checks for
each candidate rather than assumed consequences of a passing test suite.

| Shared system | Development change | Practical boundary |
| --- | --- | --- |
| Resource reads | Reconcile GPU-produced texture handles, sampler/image descriptors and storage pointers before CPU decoding. Preserve newer CPU bytes and complete readback before binding descriptors. | Clean resources avoid submission/readback. This does not implement all shader-varying texture selection. |
| Feedback | Evaluate overlap using the current draw's sampled views, even when descriptor-table addresses are unchanged. | Existing depth-write/barrier throttling remains. General aliasing coverage is still incomplete. |
| Input assembly | Use the same effective topology and restart rules for pipeline creation and dynamic state; track stage changes even with unchanged guest topology. | Task/mesh slots do not imply tessellation. This does not prove that a topology warning caused a reported visual defect. |
| Storage pools | Extend read-only table bindings to mapped, device-supported reach, up to 1 GiB, reserving alignment and page padding. Query the actual OpenGL block limit. | Written pools retain their caps. Mapping continuity, physical prefix and final host range are checked. Larger pools can increase upload/residency costs. |
| Query reports | Keep fence-flagged reports alive until their real GPU fence completes; include payload-only, non-async release, semi-flush and direct-flush paths. | This repairs publication ordering, not lossless accumulation of indefinitely running occlusion counts. |
| Compiled pipelines | Retain warm graphics pipelines while driver budget permits and the cache has at most 16,384 entries; otherwise resume age trimming. | The threshold is an age-trim trigger, not a hard memory ceiling. First-use shaders can still compile during play. |
| Storage access alignment | Split 64/128-bit storage loads and stores into consecutive 32-bit accesses when vector alignment is not established. | Atomics retain their existing path. Compiler fixtures check emitted modules, not GPU data values. |
| Compute and host ordering | Publish compute writes before indirect/graphics consumers and GPU writes before mapped host readback. | Preserve exact GPU-owned spans and newer CPU bytes. Explicit barriers do not establish correctness for every aliasing pattern. |
| Pipeline workers | Give asynchronous native graphics emission owned inputs and completion state; prebuild bounded, detached cached environments. | First-use native draws still wait for their pipeline. Unseen shaders cannot be precompiled, and background work can compete with gameplay. |
| Worker completion | Synchronize cancelled-worker completion publication with waiting threads. | Portable fixtures cover the missed-wakeup window and admitted work; scheduler behavior still needs runtime observation. |

The storage-pool work adapts [Lichii PR 351 by jpkto](https://github.com/zortos293/Lichii/pull/351),
upstream commit `6aa59375402096a4ca4bd0a3ebfcac37f6d24877`. That report identifies lighting-probe
records beyond a 64 MiB binding in Ocarina of Time. Its reported result is a lead for validation
on other builds and devices, not transferred gameplay acceptance.

The query-report work adapts [Lichii PR 337](https://github.com/zortos293/Lichii/pull/337),
upstream commit `0f0720d07ddeeb52b0f59826b2e79b6b03302ab3`, with additional local publication
and waiting paths corrected during review. Original project copyright and license notices are
preserved.

The next development candidate selectively adapts [Lichii PR 295](https://github.com/zortos293/Lichii/pull/295)
(`18cfe8556341a0b969a7ae28b71fb541632a90ca`) for storage alignment and GPU ordering,
[PR 290](https://github.com/zortos293/Lichii/pull/290)
(`c9bdbaa38ee522e3124ae05eb56c74be15cf780a`) for cancelled-worker wakeups,
[PR 184](https://github.com/zortos293/Lichii/pull/184)
(`5f6066b26bb56fc0d5f2925464979bf6cdc56666`) for owned native pipeline emission, and
the mapped-host-read and bounded prebuild portions of [PR 194](https://github.com/zortos293/Lichii/pull/194)
(`b06d1818eeec9ab13f0df9822b58aef50a601483`). These are reviewed adaptations rather than
a merge of the upstream branch. Existing descriptor coherence, feedback, tessellation state,
query lifetime and warm-residency fixes remain in place. Vulkan/driver cache versions remain
57/52; preload policy, two-session history, two-minute save cadence, timing controls and
diagnostic defaults are unchanged. See [pipeline ownership and prebuild limits](nx2/async-native-pipelines.md).

## Remaining work

- The inherited reset after more than 256 occlusion slots discards accumulated samples. A
  bounded lossless checkpoint must preserve GPU prefix sums, CPU amendment, pending reports,
  reset epochs and fence lifetimes; simply increasing or removing the cap is insufficient.
- Invocation-dependent bindless handles through storage, loops and per-fragment control flow
  still need shader-time descriptor selection. A CPU per-draw fallback cannot represent these.
- Shader-environment indirect-pointer classification still contains a raw scalar read outside
  the new runtime resource-read path and needs a separate coherence/cache review.
- Water strobing, lighting/type mismatches, image aliases, timing coverage and large-ASTC mip
  residency retain the limits documented in [known issues](../KNOWN_ISSUES.md).

Warm-cache compatibility is limited to unchanged serialization, key and native translation
definitions under the normal device/header admission rules. It does not establish semantic
cache correctness, a cache hit rate, a graphics fix or a measured performance gain.
