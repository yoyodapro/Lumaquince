<!--
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Known issues

These notes apply to **Quality 03**, release `v0.1.0-preview.1-quality.3`, compiled source `bcbe1db80b66c33deb2fc9e96fffbd1a9229dd91`. The internal application version remains `0.1.0-preview.1`. Its build and isolated checks passed; **new GUI startup, gameplay, rendered-image correctness and FPS are unvalidated**. Earlier accepted gameplay belongs to an earlier executable. See [VALIDATION.md](VALIDATION.md).

The latest accepted Quality 02 predecessor received qualitative feedback that the sky beams were gone and materials looked normal. That observation belongs to the earlier executable built from source `a52eada6b0a7e02b162db5bd0467e8afd1e0e927` and does not qualify the new Quality 03 build. Other reported issues are unresolved or unverified on this candidate, not assertions that each symptom is still visible after those earlier improvements.

## Rendering

- Development gameplay has also reported excessive sky glare, glowing stretched triangles and
  black character/enemy materials. A short successful replay does not close these reports;
  verify affected scenes again for each candidate. The broader changes and open renderer work
  are described in [rendering quality](docs/rendering-quality.md).

- Water strobing was reported at Lake Hylia in earlier Ocarina of Time gameplay. Its cause and resolution remain unverified for Quality 03.
- An older predecessor's Vulkan validation log reported invalid texture bindings: a color-format/depth-comparison mismatch and a floating-point versus unsigned-integer image-type mismatch. Complete lighting and color correctness are not established.
- GPU-dependent texture selection through storage buffers, loops or per-fragment values is unfinished. A CPU per-draw fallback cannot represent every such selection.
- Point/line mesh output support does not complete vegetation culling compute or all task/mesh behavior.
- An earlier replay reported a three-dimensional image subresource warning concerning future slice semantics, separate from the two binding errors. Its status on Quality 03 has not been re-evaluated.
- General image aliasing, resource-producer coverage and every dynamic-stride transition are not exhaustively verified.

The predecessor's bounded post-fix replay did not reproduce six earlier Vulkan error classes covering read/write hazards, overlapping copy destinations, Win32 surface instance, dynamic stride and incompatible mutable views. That result covers one bounded workload on one test PC. Vulkan synchronization validation also has incomplete coverage of descriptor-buffer shader accesses.

Do not work around an image problem by deleting fog/shadow shaders, forcing arbitrary texture handles, disabling required depth comparisons or omitting draws and then describe the result as correct rendering.

## Speed and stutter

Frame rate varies with scene, cache state, hardware, driver and settings. A presented-frame counter does not prove unique rendered frames, display scanout rate or normal simulation speed.

Selected predecessor observations were around 45 FPS in a static view and 49–60 in changing camera views. Earlier observations around 44 FPS used different scene/time/cache conditions. These are context, not a matched benchmark, a percentage gain or a sustained 60 FPS result.

First encounters with new shaders or pipeline combinations can still compile during play. A warmed cache only knows previously encountered compatible records. No complete prebuilt shader pack is provided.

An earlier experiment using the same reviewed timing payload received strong positive feedback and movement closer to normal speed. It changes a guest timing step; it does not increase rendering throughput. The built-in Adaptive Timing (Experimental) Add-On is enabled by default only when the exact supported title, build and layout guards pass.

Quality 03's owned emission and bounded cached prebuild can overlap some preparation work, but first-use native draws still wait for the needed pipeline. Background work can compete with gameplay for CPU time and memory. The 64-entry/16 MiB captured-input limits are not limits on all compiler or driver allocations. The warm-cache threshold of 16,384 entries triggers age trimming; it is not a hard memory ceiling. No stutter or FPS improvement has been measured for this new build. The background prebuilder handles eligible cached entries skipped by foreground preload; with preload-all enabled, there may be no skipped work for it to prepare.

## Experimental timing limits

Adaptive Timing (Experimental) is enabled by default for the exact supported build. Other builds skip it. Matching enabled external executable patches take precedence over the built-in timing change. Do not assume a generic patch for a different update is compatible.

The reviewed timing algorithm has a 50 ms delta cap, approximately one-eighth smoothing and a 3% deadband. For measured intervals longer than 50 ms, the cap prevents full compensation; the hook's relationship to updates and frames is not proven. Transitions can take time to settle.

Shared state is unsynchronized. The hook's invocation schedule, concurrent/reentrant use and complete menu, cutscene, audio and physics coverage remain unproven. The context wrapper preserves the reviewed normal-return state; unusual floating-point traps, stack faults and asynchronous unwinding have not been qualified.

Use backed-up saves when testing. If timing or audio regresses, save and close normally, uncheck Adaptive Timing (Experimental) in game Properties → Add-Ons, and restart the game.

The shipped preload-all file override can increase startup compilation and memory use. Remove the EDEN_NX2_PRELOAD_ALL_PIPELINES line from windows/nx2_switches.txt in the application package (dist/nx2_switches.txt in source) to use the unchanged bounded used-list policy. This only changes preparation of encountered compatible records; new shaders can still compile during play.

## Memory, queries and lifecycle

- The inherited sparse-bind CPU fence remains. A proven asynchronous replacement is not implemented.
- Fence-flagged query reports now retain their real GPU completion lifetime, but the inherited reset beyond 256 occlusion slots still discards accumulated samples. Query publication and FIFO copy fixes do not provide lossless accumulation.
- Large-ASTC fine-mip trimming remains an inherited allocation safeguard; a quality-preserving residency replacement is unfinished.
- Capture-ring and shutdown fixes have bounded checks, not exhaustive long-session, repeated-resolution-change or device-loss stress.
- Guarded/trap-prefix shader-environment persistence remains excluded. Stage identity must not be guessed from a compute-only validation result.
- Shader-environment indirect-pointer classification still includes a raw scalar read outside the runtime resource-reconciliation path; it needs separate coherence/cache review.
- Remap notifications repair a source-level invalidation defect, but their contribution to any particular scene or FPS result is unknown.

## Validation coverage

The predecessor's native tests exercised host helpers and production fixtures. They did not execute the GUI/GPU path or establish full game speed.

The predecessor also received a bounded ordinary gameplay review and actual Vulkan core, synchronization, parameter, object-lifetime and thread-safety checks through normal shutdown. It was not fully Vulkan clean. Those results do not cover every game, GPU vendor, driver, configuration or later rebuild.

Quality 03 passed 900 full-suite cases, 206 required validation commands and validation of 178 actual emitted SPIR-V modules. These results do not demonstrate GPU execution or gameplay correctness. For exact Quality 03 results, consult the packaged [VALIDATION.md](VALIDATION.md) for exact Release/LTO build, native-test, GPU/gameplay, timing-default and package-review results. Skipped tests, a successful compile or historical predecessor checks must not be read as a current runtime pass. Whole-game, multi-device and exact wall-clock timing coverage remain incomplete.

See [release notes](RELEASE_NOTES.md) for historical results and [privacy guidance](PRIVACY.md) before sharing logs.
