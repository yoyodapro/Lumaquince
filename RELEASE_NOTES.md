<!--
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Lumaquince Quality 03 — Renderer and pipeline update

The GitHub release is **`v0.1.0-preview.1-quality.3`**, titled **Lumaquince Quality 03 — Renderer and pipeline update**. The validated executables retain the internal version **`0.1.0-preview.1`**; they are not the original Preview 1 binaries. Identify this build by the Quality 03 release tag, compiled-source commit `bcbe1db80b66c33deb2fc9e96fffbd1a9229dd91` and executable checksums below. No executable was rebuilt just to change its displayed version.

The release archive is **`Lumaquince-quality-03-complete.zip`**, containing **`Lumaquince-quality-03/`**. Its `source/` directory preserves the exact compiled source. Updated documentation at the package root and in `windows/` is a release-record overlay; historical documentation inside `source/` remains part of the frozen compiler-source snapshot. A later public documentation commit does not change the binary identity.

This release combines all previously unpublished renderer work since the public Preview 1 main revision `7d6af2467e47fb9564d5fb5290b7faf94eea758a`, including the Quality 02 work and the subsequent selective upstream adaptations. It focuses on correct resource ownership, shader accesses, synchronization and bounded pipeline preparation. It does not claim that the reported glare, stretched triangles, black materials or water strobing are fixed in gameplay.

## Changes since the public Preview 1 release

### Resource ownership and renderer state

- Prepare GPU-owned uniform bytes before binding graphics and compute descriptors, including mixed direct/staged uniform banks. Read back only the GPU-owned spans, preserve newer CPU writes, and retain the clean-resource fast path.
- Reconcile GPU-produced texture handles, sampler/image descriptors and storage pointers before the CPU decodes draw or dispatch bindings. Feedback checks use each draw's current sampled views even when descriptor-table addresses are unchanged.
- Distinguish device-memory translation-cache owners across manager destruction and reconstruction, preventing a reused address from inheriting an earlier manager's block-read cache.
- Apply consistent topology and primitive-restart rules to pipeline creation and dynamic state. Stage changes, including mesh/ordinary transitions, update state even when guest topology is unchanged; task/mesh stages are not mistaken for tessellation.
- Extend read-only storage-table bindings beyond the previous 64 MiB cap within mapped and device-supported reach, up to 1 GiB. Preserve alignment/page-padding bounds, reject discontinuous backing prefixes, and use the actual OpenGL block-size limit. Writable-pool caps remain unchanged.
- Preserve fence-flagged query reports until the real GPU fence completes, including payload-only, non-async release, semi-flush and direct-flush paths. This fixes report lifetime and publication; the inherited bounded sample-discard policy is still present.

### Shader accesses and synchronization

- Cover constant and uniform LOP3 operand forms in the register-pressure/shadow handling, retaining aligned uniform reservation and existing SHF support.
- Lower 64-bit and 128-bit storage loads/stores to consecutive 32-bit operations when vector alignment cannot be established. The existing atomic path is retained.
- Publish compute writes before indirect-command and graphics consumers. Replace the affected wait-for-idle event ordering with an explicit memory dependency.
- Publish GPU writes to mapped host-readable buffers before completion waits and CPU access. Batched and mixed staged/mapped readbacks retain clipping, exact GPU-owned ranges and protection for newer CPU bytes; the mapped path does not allocate an extra readback staging buffer.

### Pipeline preparation and worker lifetime

- Retain warm graphics pipelines while driver memory permits and the cache contains at most 16,384 entries; resume conservative age trimming when either condition fails. The count is an age-trim trigger, not a hard memory ceiling.
- Give native asynchronous graphics emission owned inputs and shared completion state, with once-only work, exception handling, capture release and multiwaiter completion. Destruction waits before referenced state can disappear. Native draws still wait for a required pipeline.
- Prebuild eligible previously encountered cached environments with bounded ownership: at most 64 captured entries, a 16 MiB input budget and one or two background workers. Driver parallel-build workarounds remain active. Reload stops and joins the old generation before resetting reservations and admitting new work.
- Publish cancelled-worker completion under the same queue mutex used by waiting threads, closing a missed-wakeup window while preserving accepted-work accounting and cancellation ownership.
- Add focused production-path fixtures for ownership, mixed readback, cancellation, pipeline lifecycle, reload reuse and storage alignment. Correct test setup accounting and retain a successfully observed GPU split wait transition without changing production split scheduling.

The Vulkan shader-environment cache remains version 57, OpenGL 52 and the opaque Vulkan driver cache 52. Two-session history, the two-minute cache-save cadence, full-key admission/deduplication and existing warm-residency safeguards are retained. Compatible encountered records can be reused; a format number alone does not guarantee semantic correctness or a cache hit rate. Unseen shaders still compile when first encountered. The background prebuilder handles eligible cached entries skipped by foreground preload; with preload-all enabled, there may be no skipped work for it to prepare.

Adaptive Timing, its exact-build guards, the documented gameplay profile and diagnostic defaults are unchanged. This update does not omit native draws, disable required comparisons, remove fog/shadows, or replace the timing payload with an upstream timing stack.

## Validation

| Check | Result and scope |
|---|---|
| Windows x86-64 build | PASS — Release with link-time optimization; production NONE mode, no PGO performance claim |
| Unfiltered native suites | PASS — **900 cases: 892 main + 8 updater** |
| Required validation commands | PASS — **206**; this includes the suites, focused runs and shader validators, not 206 additional test suites |
| Focused groups | PASS — **14 groups / 60 case executions**, repeated coverage reported separately from the 900 unique full-suite cases |
| Actual emitted SPIR-V | PASS — **178 modules: 150 alignment + 6 matrix + 17 CAS + 5 mesh** |
| New executable GUI startup and gameplay | **NOT RUN** |
| New executable real GPU output, FPS and scene correctness | **NOT VALIDATED** |
| Sustained 60 FPS, exact full-game simulation speed, multi-device compatibility | **NOT ESTABLISHED** |

The exact test binaries ran in an isolated Windows Sandbox. Fixtures used no game, keys, firmware, saves or personal profiles. Offline shader validation checks module structure and validity; it does not execute shaders on a real GPU or prove correct rendered images.

The latest accepted Quality 02 predecessor received qualitative feedback that the sky beams were gone and materials looked normal. That observation belongs to the earlier executable built from source `a52eada6b0a7e02b162db5bd0467e8afd1e0e927` and does not qualify the new Quality 03 build. Other reported issues are unresolved or unverified on this candidate, not assertions that each symptom is still visible after those earlier improvements.

Earlier development gameplay, including positive movement feedback with the same reviewed timing payload, belongs to an earlier executable. It is not acceptance of this new build. Historical FPS observations were not matched benchmarks and provide no measured speedup for Quality 03. See [VALIDATION.md](VALIDATION.md) for the current result and [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for the unresolved work.

## Download and identity

Download [Lumaquince-quality-03-complete.zip](https://github.com/yoyodapro/Lumaquince/releases/download/v0.1.0-preview.1-quality.3/Lumaquince-quality-03-complete.zip). Extract it to a new folder and retain the previous application and separate save/settings backups. The GUI is `windows/lumaquince.exe`; the command-line frontend is `windows/lumaquince-cli.exe`.

| Artifact | SHA-256 |
|---|---|
| `windows/lumaquince.exe` | `d5bee379450344b61a951cef04e7748411a4cc6a81c1835034623a79e10b9811` |
| `windows/lumaquince-cli.exe` | `8187e62196837551ff8edac47d4a232b829c6dfd1e6b3f8bf0b0c09de0a435e6` |

Use the release's `SHA256SUMS.txt` and package-review records for the final complete archive identity. The package contains matching modified source, dependency-source materials, recipes and applicable notices. Games, keys, firmware, personal profiles, saves and game-derived shader caches are excluded. Automatic update checking, web service and Discord presence remain disabled in the release configuration.

Independent bit-for-bit rebuilds of every prebuilt dependency have not been performed. The FFmpeg/dav1d reconstruction recipe remains untested, and byte-identical timezone conversion is not established. See [BUILD-RECIPE.md](BUILD-RECIPE.md), [VALIDATION.md](VALIDATION.md) and [UPSTREAM.md](UPSTREAM.md).

## Upstream credit

| Adaptation | Upstream reference and reviewed commit |
|---|---|
| LOP3 operand pressure entries | [Lichii PR 241](https://github.com/zortos293/Lichii/pull/241), `d25d06b588fee6fed11a16617090d707987c4756` |
| Read-only storage-pool reach | [Lichii PR 351 by jpkto](https://github.com/zortos293/Lichii/pull/351), `6aa59375402096a4ca4bd0a3ebfcac37f6d24877` |
| Query-report lifetime | [Lichii PR 337](https://github.com/zortos293/Lichii/pull/337), `0f0720d07ddeeb52b0f59826b2e79b6b03302ab3` |
| Storage alignment and GPU ordering | [Lichii PR 295](https://github.com/zortos293/Lichii/pull/295), `18cfe8556341a0b969a7ae28b71fb541632a90ca` |
| Cancelled-worker wakeups | [Lichii PR 290](https://github.com/zortos293/Lichii/pull/290), `c9bdbaa38ee522e3124ae05eb56c74be15cf780a` |
| Owned native graphics emission | [Lichii PR 184](https://github.com/zortos293/Lichii/pull/184), `5f6066b26bb56fc0d5f2925464979bf6cdc56666` |
| Mapped-host reads and bounded cached prebuild | Selected portions of [Lichii PR 194](https://github.com/zortos293/Lichii/pull/194), `b06d1818eeec9ab13f0df9822b58aef50a601483` |

These are reviewed adaptations with local ownership, coherency and lifetime changes, not a wholesale merge of an upstream branch. Original source copyright and license notices remain. Lumaquince builds on Lichii, Eglantine, Eden, yuzu and Sudachi; see [UPSTREAM.md](UPSTREAM.md) for the broader attribution.
