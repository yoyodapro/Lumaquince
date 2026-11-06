# Changelog

This record distinguishes the public release label from the executable's internal version. Earlier Eglantine release history is retained below; current Lumaquince release notes describe the selected adaptations and local changes.

## [Unreleased]

No additional changes recorded.

## [v0.1.0-preview.1-quality.3] — Quality 03

Compiled source: `bcbe1db80b66c33deb2fc9e96fffbd1a9229dd91`. Internal application version remains `0.1.0-preview.1`. Includes all unpublished Quality 02 and Quality 03 changes since public main `7d6af2467e47fb9564d5fb5290b7faf94eea758a`.

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

Validation: 900 full-suite cases (892 main + 8 updater), 206 required commands and 178 actual emitted SPIR-V modules passed. Fourteen focused groups account for 60 repeated case executions, not additional unique tests. New gameplay, image correctness and FPS remain unvalidated. Timing controls, diagnostic defaults and Vulkan/driver cache versions 57/52 are retained. See [release notes](RELEASE_NOTES.md) for attribution and [validation](VALIDATION.md) for scope.

## [v0.1.0-preview.1] — Original public preview

The original public preview combined retained cache/command work, shader CAS and operand fixes, point/line mesh output, bounded readbacks, query/copy ordering, memory ownership, Vulkan resource and presentation fixes, and guarded Adaptive Timing. Its own package validation remains historical; it is separate from the Quality 03 binaries. See [DIFFERENCES.md](DIFFERENCES.md) for the retained implementation and limits.

## [0.1.0] — Inherited Eglantine history

- Initial release candidate.
