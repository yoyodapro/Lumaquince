<!--
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Known issues

These notes describe the reviewed source and its development predecessor. See the packaged [VALIDATION.md](VALIDATION.md) for exact Lumaquince build and check status; earlier results do not transfer automatically.

## Rendering

- Development gameplay has also reported excessive sky glare, glowing stretched triangles and
  black character/enemy materials. A short successful replay does not close these reports;
  verify affected scenes again for each candidate. The broader changes and open renderer work
  are described in [rendering quality](docs/rendering-quality.md).

- Water strobing remains visible at Lake Hylia in Ocarina of Time. The cause has not been conclusively isolated.
- Invalid texture bindings remain in the predecessor's Vulkan validation log: a color-format/depth-comparison mismatch and a floating-point versus unsigned-integer image-type mismatch. Complete lighting and color correctness are not established.
- GPU-dependent texture selection through storage buffers, loops or per-fragment values is unfinished. A CPU per-draw fallback cannot represent every such selection.
- Point/line mesh output support does not complete vegetation culling compute or all task/mesh behavior.
- A three-dimensional image subresource warning remains concerning future slice semantics. Its presence is separate from the two binding errors.
- General image aliasing, resource-producer coverage and every dynamic-stride transition are not exhaustively verified.

The predecessor's bounded post-fix replay did not reproduce six earlier Vulkan error classes covering read/write hazards, overlapping copy destinations, Win32 surface instance, dynamic stride and incompatible mutable views. That result covers one bounded workload on one test PC. Vulkan synchronization validation also has incomplete coverage of descriptor-buffer shader accesses.

Do not work around an image problem by deleting fog/shadow shaders, forcing arbitrary texture handles, disabling required depth comparisons or omitting draws and then describe the result as correct rendering.

## Speed and stutter

Frame rate varies with scene, cache state, hardware, driver and settings. A presented-frame counter does not prove unique rendered frames, display scanout rate or normal simulation speed.

Selected predecessor observations were around 45 FPS in a static view and 49–60 in changing camera views. Earlier observations around 44 FPS used different scene/time/cache conditions. These are context, not a matched benchmark, a percentage gain or a sustained 60 FPS result.

First encounters with new shaders or pipeline combinations can still compile during play. A warmed cache only knows previously encountered compatible records. No complete prebuilt shader pack is provided.

An earlier experiment using the same reviewed timing payload received strong positive feedback and movement closer to normal speed. It changes a guest timing step; it does not increase rendering throughput. The built-in Adaptive Timing (Experimental) Add-On is enabled by default only when the exact supported title, build and layout guards pass.

## Experimental timing limits

Adaptive Timing (Experimental) is enabled by default for the exact supported build. Other builds skip it. Matching enabled external executable patches take precedence over the built-in timing change. Do not assume a generic patch for a different update is compatible.

The reviewed timing algorithm has a 50 ms delta cap, approximately one-eighth smoothing and a 3% deadband. For measured intervals longer than 50 ms, the cap prevents full compensation; the hook's relationship to updates and frames is not proven. Transitions can take time to settle.

Shared state is unsynchronized. The hook's invocation schedule, concurrent/reentrant use and complete menu, cutscene, audio and physics coverage remain unproven. The context wrapper preserves the reviewed normal-return state; unusual floating-point traps, stack faults and asynchronous unwinding have not been qualified.

Use backed-up saves when testing. If timing or audio regresses, save and close normally, uncheck Adaptive Timing (Experimental) in game Properties → Add-Ons, and restart the game.

The shipped preload-all file override can increase startup compilation and memory use. Remove the EDEN_NX2_PRELOAD_ALL_PIPELINES line from dist/nx2_switches.txt to use the unchanged bounded used-list policy. This only changes preparation of encountered compatible records; new shaders can still compile during play.

## Memory, queries and lifecycle

- The inherited sparse-bind CPU fence remains. A proven asynchronous replacement is not implemented.
- The inherited bounded query-sample discard remains. Query bank growth and FIFO copy fixes do not provide lossless sample accumulation.
- Large-ASTC fine-mip trimming remains an inherited allocation safeguard; a quality-preserving residency replacement is unfinished.
- Capture-ring and shutdown fixes have bounded checks, not exhaustive long-session, repeated-resolution-change or device-loss stress.
- Guarded/trap-prefix shader-environment persistence remains excluded. Stage identity must not be guessed from a compute-only validation result.
- Remap notifications repair a source-level invalidation defect, but their contribution to any particular scene or FPS result is unknown.

## Validation coverage

The predecessor's native tests exercised host helpers and production fixtures. They did not execute the GUI/GPU path or establish full game speed.

The predecessor also received a bounded ordinary gameplay review and actual Vulkan core, synchronization, parameter, object-lifetime and thread-safety checks through normal shutdown. It was not fully Vulkan clean. Those results do not cover every game, GPU vendor, driver, configuration or later rebuild.

For Lumaquince 0.1.0-preview.1, consult the packaged [VALIDATION.md](VALIDATION.md) for exact Release/LTO build, native-test, GPU/gameplay, timing-default and package-review results. Skipped tests, a successful compile or historical predecessor checks must not be read as a current runtime pass. Whole-game, multi-device and exact wall-clock timing coverage remain incomplete.

See [release notes](RELEASE_NOTES.md) for historical results and [privacy guidance](PRIVACY.md) before sharing logs.
