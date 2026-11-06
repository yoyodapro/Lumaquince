<!--
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Lumaquince 0.1.0-preview.1

Lumaquince is a new independent fork name for a focused Windows development line based on Lichii and Eglantine. The target is a Windows x86-64 Release build with link-time optimization, accompanied by its matching full modified source.

No public repository or release URL is announced in these notes.

The GUI executable is lumaquince.exe and the command-line frontend is lumaquince-cli.exe. Lumaquince uses its own application-data directory. Update checking, web service and Discord presence are disabled in the release configuration.

## Included source work

- Retained task/mesh shader-cache persistence, bounded descriptor storage, source-owner retirement, exact command-state reuse, numeric binding memoization, bounded texture workers and callback/wait lifetime work.
- Shader CAS retry, packed half-float/shared-store and high-register operand fixes; Vulkan point/line mesh output and point-size handling.
- Safe empty-range handling, bounded/batched readbacks, query CPU publication and FIFO-preserving overlapping copy ordering.
- Mapped physical-page replacement notifications and Windows host-memory cleanup/move ownership fixes.
- Descriptor-buffer view elision and capture/presentation slot, noncoherent-write, stride-change and teardown synchronization fixes.
- Image-layout restoration and early depth-upload publication, Win32 surface instance, dynamic vertex-stride declaration and ASTC 8x6 linear/sRGB view compatibility.
- Bounded exact-key encountered-pipeline preload validation and reduced duplicate cheat-file scanning/logging.

See [DIFFERENCES.md](DIFFERENCES.md) for scope, retained behavior and unfinished architecture.

## Optional timing work

A separately reviewed timing change adjusts a guest timing step using elapsed counter time. An earlier experiment with the same payload received strong positive feedback and movement closer to normal speed. It does not increase rendered frame rate or repair lighting; water strobing at Lake Hylia remains.

The built-in **Adaptive Timing (Experimental)** Add-On is enabled by default only for the exact supported Ocarina of Time title/Main Build-ID and expected module layout. Original-operation, zero-cave and metadata-backed padding guards must pass; other builds skip it. Matching enabled external executable patches take precedence. Max Resolution remains a separate inherited option.

To disable it, save and close normally, open game Properties → Add-Ons, uncheck Adaptive Timing (Experimental), and restart. The algorithm has a 50 ms cap, one-eighth smoothing and a 3% deadband, with unresolved concurrency, unusual floating-point/exception and complete-game timing coverage limits.

Original and modified guest timing must be evaluated separately. Earlier unmodified-guest checks do not qualify changed guest timing.

The shipped dist/nx2_switches.txt profile selects split mode 2, fence batching, a 4096 draw-flush budget and encountered-pipeline preload. C++ defaults are unchanged: preload-all remains false and the native preload budget is 128. Remove the profile's EDEN_NX2_PRELOAD_ALL_PIPELINES line for the bounded used-list policy. Preload-all trades startup time and memory for preparation of encountered compatible records; it cannot prebuild unseen shaders.

## Validation and evidence

| Evidence | Result and scope |
|---|---|
| Development predecessor Release/LTO build | Compiled successfully. This is an earlier executable, not the new branded binary. |
| Predecessor full native suites | 837 cases and 17,135,396 assertions passed across main and updater suites. |
| Predecessor required validation commands | All 41 passed, including 39 focused checks. Focused reruns are not added to the full-suite totals. |
| Predecessor emitted shader modules | 28 actual SPIR-V modules passed validation: six matrix, seventeen CAS and five mesh modules. |
| Predecessor ordinary gameplay | Bounded load, camera, movement and shutdown observations completed on a Windows NVIDIA test PC. |
| Predecessor actual Vulkan review | Six earlier error classes were absent in one bounded post-fix replay; two invalid binding classes and a 3D compatibility warning remained. |
| Optional timing experiment | Qualitative gameplay feedback was positive; full wall-clock speed, audio/cutscene and concurrency validation remain incomplete. |
| Lumaquince branded build and runtime | See the packaged [VALIDATION.md](VALIDATION.md) for exact build and check status. No inherited pass is claimed for a new executable. |

Native tests do not run the GUI/GPU path. SPIR-V validation does not prove resource-binding or rendered-image correctness. The Vulkan validation run had diagnostic overhead and is excluded from performance comparisons.

Selected ordinary predecessor observations were around 45 FPS in a static view and 49–60 in changing views. Scene, time and cache differences prevent a matched speedup claim. No percentage improvement or sustained 60 FPS result is established.

## Remaining issues

Water strobing at Lake Hylia, invalid texture bindings, incomplete GPU-selected textures and broader rendering coverage remain open. Predictive preparation of unseen shaders, lossless query accumulation, a complete parallel scheduler and quality-preserving virtual residency are unfinished.

This preview has no complete lighting, exact-speed or whole-game stability guarantee. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Distribution

The application and source must be delivered together with the source/build/dependency information and license notices needed for the distributed components. A patch or upstream download link alone is not the promised full modified source package.

Games, keys, firmware, personal profiles, saves and game-derived caches are excluded by the package policy. Source author credit and applicable GPL/SPDX notices remain. Dependency recovery, mixed license notices and package privacy results belong in [VALIDATION.md](VALIDATION.md); no blanket dependency-license or completed-compliance claim is made here. See [UPSTREAM.md](UPSTREAM.md) and [PRIVACY.md](PRIVACY.md).
