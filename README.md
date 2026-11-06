<!--
SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2018 yuzu Emulator Project
SPDX-License-Identifier: GPL-2.0-or-later
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

<p align="center">
  <img src="dist/lumaquince.svg" width="120" height="120" alt="Lumaquince logo">
</p>

<h1 align="center">Lumaquince</h1>

<p align="center"><strong>Our recommended OOT Remake PC setup.</strong><br>
A focused Windows emulator fork, a complete setup guide, and a preview with its evidence in the open.</p>

<p align="center">
  <a href="https://github.com/yoyodapro/Lumaquince/releases/download/v0.1.0-preview.1-quality.3/Lumaquince-quality-03-complete.zip"><strong>Download for Windows x64</strong></a>
  · <a href="https://github.com/yoyodapro/Lumaquince/releases/tag/v0.1.0-preview.1-quality.3">Release notes &amp; checksums</a>
  · <a href="#quick-start">Quick start</a>
  · <a href="KNOWN_ISSUES.md">Known issues</a>
</p>

**Quality 03 · Experimental · Windows x86-64**

GitHub release **`v0.1.0-preview.1-quality.3`**. The validated binary retains internal version **`0.1.0-preview.1`**; use the release tag, source commit and hashes below to distinguish it from the original Preview 1 build.

Lumaquince is an independent, open-source fork of [Lichii](https://github.com/zortos293/Lichii), building on Eglantine, Eden, yuzu and Sudachi. This preview brings together a guarded Ocarina of Time timing option, focused shader and Vulkan fixes, cache and memory work, and a documented gameplay profile. It is intended for people who want a clear starting point for **OOT Remake on PC** and enough detail to understand what they are running.

Our recommendation reflects that focus and the documented development experience. There is no controlled comparison establishing Lumaquince as the fastest or best emulator across PCs or games.

> **Read before playing:** Lake Hylia water strobing remains unresolved, and complete lighting/color correctness is unproven. The rebuilt release passed native and offline shader checks; its GUI startup and new gameplay are **NOT RUN**. Earlier gameplay feedback belongs to an earlier executable. There is no sustained 60 FPS, exact simulation-speed or whole-game compatibility claim. See [known issues](KNOWN_ISSUES.md) and [validation](VALIDATION.md).

## Download

Get **[Lumaquince-quality-03-complete.zip](https://github.com/yoyodapro/Lumaquince/releases/download/v0.1.0-preview.1-quality.3/Lumaquince-quality-03-complete.zip)** from the [preview release](https://github.com/yoyodapro/Lumaquince/releases/tag/v0.1.0-preview.1-quality.3).

The complete package includes the Windows application and runtime libraries, matching modified project source and build scripts, recovered dependency-source materials and applicable notices, a build recipe, and release validation records. Consult the release's **SHA256SUMS.txt** and package-review records for the final archive size and checksum.

**No game, console decryption keys, console firmware, saves, personal profile, controller configuration or game-derived shader cache is included.** Supply your own lawfully obtained game and system files. This project does not provide download links for them.

Use the named complete ZIP for the application. GitHub's automatically generated “Source code” archives do not include the Windows application or the separately inventoried dependency-source bundle.

## What changed in Quality 03?

This update includes **all unpublished Quality 02 and Quality 03 work** since the public Preview 1 release:

- GPU-owned uniform/resource reads complete before descriptor binding, with exact ownership spans and preservation of newer CPU data. Device-memory translation caches distinguish owner lifetimes.
- Dynamic topology, restart and mesh/tessellation transitions stay consistent; feedback checks use current sampled views. Read-only storage pools can cover larger mapped tables within actual device limits.
- Query reports retain their real GPU-fence lifetime. Storage vector accesses use alignment-safe lowering; compute, indirect, graphics and mapped-host reads gain the required publication ordering.
- Constant/uniform LOP3 operand handling is corrected. Warm pipelines are retained within driver-budget/count safeguards; native graphics emission owns its work and compatible cached prebuilds have bounded inputs and reload lifetimes.
- Cancelled-worker completion uses the waiting-thread mutex protocol. New fixtures cover these ownership, cancellation, alignment and pipeline boundaries.

The result is **900 passing native test cases and 178 validated emitted SPIR-V modules**. New gameplay and FPS remain unvalidated. Timing controls and diagnostic defaults are unchanged. Read the [full release notes](RELEASE_NOTES.md) for details and upstream credit.

## Quick start

1. **Extract the complete ZIP into a new folder.** Keep the entire extracted package and the DLLs beside the application. Do not run the executable from inside the ZIP or copy only the EXE.
2. **Back up your own existing saves and settings.** Keep the backup separate from the new application folder before testing an experimental build.
3. **Open `Lumaquince-quality-03/windows/lumaquince.exe`.** The command-line frontend is `windows/lumaquince-cli.exe`.
4. **Supply your own system files.** Use the application's **Install Decryption Keys** and **Install Firmware** menu actions with your own lawfully obtained files. Do not reuse someone else's profile to obtain these files or settings.
5. **Add your own game.** Use **Add New Game Directory** in the game list or **File → Load File**. Use the matching game/update build for the timing option described below; another build is not automatically compatible.
6. **Configure your controller.** Open **Emulation → Configure → Controls**, select your input device, map the controls, and save the configuration. No personal controller mapping is shipped.
7. **Apply the optional OOT profile below.** Prefer the game's **Properties** for game-specific graphics choices. Confirm the timing Add-On state, then launch from the game list.

Lumaquince normally keeps application data in `%APPDATA%\Lumaquince` on Windows. This separate location starts without your previous emulator's personal settings or progress. Existing environment variables, user switch files or a profile you import can override the documented settings.

### Bringing your own save

Save migration is manual. Close the source emulator normally, identify the save for the correct game and user, and make a separate backup. In Lumaquince, use the game's **Open Save Data Location** action to identify its destination. Close the game before copying your own save. Preserve the destination backup too, and check dates and progress before replacing anything: an older copy can overwrite newer progress.

The release contains no automated save transfer and no supplied save. A copied personal profile is not required to use the gameplay guide.

## Why this is our OOT-focused recommendation

| Focus | What the preview brings together | What the evidence supports |
|---|---|---|
| Game feel | Built-in **Adaptive Timing (Experimental)** for one exact supported OOT build, using the reviewed 176-byte timing payload. | Earlier development gameplay with the same payload received very positive feedback and movement closer to normal speed. That is qualitative predecessor feedback, not a benchmark of this rebuilt GUI. |
| Shader correctness | CAS retry fixes, packed/shared-memory fixes, high-register operand handling and depth-comparison image typing, plus retained task/mesh work. | Targeted source fixes and offline emitted-module validation. GPU-dependent texture binding and complete lighting remain open. |
| Vulkan behavior | Narrow fixes for query ordering, copy/upload barriers, surface creation, dynamic vertex stride and ASTC linear/sRGB views. | A bounded predecessor replay stopped reproducing six earlier error classes. This is not a clean-Vulkan or all-scene result for the new executable. |
| Memory and host work | Buffer ownership/retirement, bounded readbacks, physical-remap notifications, selected command-state reuse and bounded worker lifetimes. | Specific implementation and test coverage; no measured whole-game FPS or memory percentage is claimed. |
| Cache preparation | Persistence and optional preload of previously encountered compatible pipelines, with the shipped performance switch file. | It can prepare known cache records. It cannot discover unseen shaders or promise stutter-free play. |
| A usable starting point | Clean application data, a documented manual profile, matching source/dependency materials and exact release hashes. | You can identify the package, understand the settings, and inspect the validation limits before using it. |

Read **[DIFFERENCES.md](DIFFERENCES.md)** for the complete technical account, inherited facilities, retained changes, exclusions and unfinished work. The comparison concerns the upstream snapshots used to develop this preview; upstream projects may have changed independently.

## OOT gameplay settings

These are historical graphics/core choices from an earlier development profile, not newly measured Quality 03 settings. **Apply them manually**; they are not all clean-install defaults or a universal performance preset. In particular, the clean C++ GPU-accuracy default remains **High**. Selecting **Low** is your performance/accuracy tradeoff.

| Setting | Historical OOT development profile |
|---|---|
| Graphics backend | Vulkan |
| Resolution scale | 0.75x / three quarters |
| GPU accuracy | Low |
| Anti-aliasing | None |
| Window scaling filter | ScaleForce |
| VSync | FIFO |
| Asynchronous GPU emulation | Enabled |
| Asynchronous shader building | Disabled |
| Disk shader cache | Enabled |
| Reactive flushing | Enabled |
| CPU accuracy / backend | Auto / Dynarmic |
| Speed limit | Enabled, 100% |
| Core speed synchronization | Enabled |
| Fast CPU time override | 0 / normal |
| NVIDIA replacement DLSS / translated game DLSS network | Disabled |
| Adaptive Timing (Experimental) | Enabled, exact supported build only |

Lower resolution trades detail for GPU cost. Low GPU accuracy may affect rendering; try **High** when investigating an image problem and compare the same scene after shader activity settles. These settings are not a demonstrated fix for the earlier water-strobing report. See [GAMEPLAY-PROFILE.md](GAMEPLAY-PROFILE.md) for the full guide.

### The performance file already shipped

The application's editable **`windows/nx2_switches.txt`** supplies these four performance overrides:

| Switch | Shipped value |
|---|---|
| `EDEN_NX2_SPLIT` | `2` |
| `EDEN_NX2_FENCE_BATCH` | `1` |
| `EDEN_NX2_FLUSH_DRAWS` | `4096` |
| `EDEN_NX2_PRELOAD_ALL_PIPELINES` | `1` |

They are file overrides. The underlying C++ preload-all default remains false, and the native draw budget remains 128. Environment variables and a user switch file can take precedence. Restart normally after editing the shipped file.

Preload-all can increase startup compilation time and memory use. If that is excessive, remove the `EDEN_NX2_PRELOAD_ALL_PIPELINES` line to use the unchanged bounded used-list policy. Both policies operate only on previously encountered compatible records. No game-derived cache is distributed.

## Adaptive Timing: scope and controls

**Adaptive Timing (Experimental)** is enabled by default, with exact compatibility guards. It changes a **guest timing step** to aim for movement closer to wall-clock speed when throughput is below the target. It does **not** create more rendered frames or raise rendering throughput.

| Guard | Required value |
|---|---|
| Title ID | `0400486024DEC000` |
| Module | `main` |
| Full 32-byte Build-ID | `E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348000000000000000000000000` |
| Image and layout | Expected decompressed NSO/MOD0 layout, original hook instruction, zero-filled executable padding and allocation tail, and reviewed code-cave/state placement |

The abbreviated Build-ID used in some documentation is the first 20 bytes above; the remaining 12 bytes must be zero. Unsupported titles, builds or layouts skip the change. Guest program arguments that change allocation layout also prevent application. An enabled matching external executable patch takes precedence; do not assume an older private timing mod should remain enabled beside this built-in option.

To turn timing off, close the game normally, open **Properties → Add-Ons**, uncheck **Adaptive Timing (Experimental)**, and restart the game. Recheck it and restart to re-enable it on the supported build. The inherited **Max Resolution (built-in)** Add-On is separate.

The algorithm uses a roughly 3% deadband, one-eighth smoothing and a 50 ms elapsed-delta cap. At longer measured intervals it cannot fully compensate. The hook's relationship to frames/updates and concurrent or reentrant invocation is unproven; shared state is unsynchronized. Menu, transition, cutscene, audio, physics and exceptional-return behavior do not have comprehensive qualification. Use backed-up saves when testing. See [timing details](DIFFERENCES.md#experimental-timing-option) and [known limits](KNOWN_ISSUES.md#experimental-timing-limits).

## What has actually been validated?

The following results apply to the **exact release inputs recorded in [VALIDATION.md](VALIDATION.md)**. They are not a gameplay benchmark.

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

The release's exact native test binaries ran in an isolated Windows Sandbox on the same physical test PC. No game, keys, firmware, save or personal profile was a test input. Passing those tests does not establish GUI/GPU behavior.

Earlier development gameplay using the same reviewed timing payload received strong positive feedback, including movement closer to normal speed. Earlier predecessor observations around 45 FPS in one static view and 49–60 in changing views came from differing scene/cache conditions. They are not matched before/after benchmarks, a percentage improvement, or sustained 60 FPS evidence for this release. Rendered FPS, presentation intervals, scanout and guest simulation speed are distinct measurements.

### Exact release identity

| Item | Identity |
|---|---|
| GitHub release | [`v0.1.0-preview.1-quality.3`](https://github.com/yoyodapro/Lumaquince/releases/tag/v0.1.0-preview.1-quality.3) |
| Internal application version | `0.1.0-preview.1` (retained in the exact validated binaries) |
| Compiled source | [`bcbe1db80b66c33deb2fc9e96fffbd1a9229dd91`](https://github.com/yoyodapro/Lumaquince/tree/bcbe1db80b66c33deb2fc9e96fffbd1a9229dd91) |
| Complete ZIP identity | See the release's `SHA256SUMS.txt` and package-review records |
| `windows/lumaquince.exe` SHA-256 | `d5bee379450344b61a951cef04e7748411a4cc6a81c1835034623a79e10b9811` |
| `windows/lumaquince-cli.exe` SHA-256 | `8187e62196837551ff8edac47d4a232b829c6dfd1e6b3f8bf0b0c09de0a435e6` |

The matching compiled tree is preserved in the package's `source/`. Updated public documentation is a separate release-record overlay at the package root and in `windows/`; documentation within `source/` remains frozen. A later public documentation commit does not rebuild these executables. See `SOURCE-AND-BINARY-IDENTITY.json` and [VALIDATION.md](VALIDATION.md). A checksum establishes byte identity, not game compatibility or malware freedom.

The latest accepted Quality 02 predecessor received qualitative feedback that the sky beams were gone and materials looked normal. That observation belongs to the earlier executable built from source `a52eada6b0a7e02b162db5bd0467e8afd1e0e927` and does not qualify the new Quality 03 build. Other reported issues are unresolved or unverified on this candidate, not assertions that each symptom is still visible after those earlier improvements.

## Known issues to understand first

- **Water strobing at Lake Hylia remains unresolved.**
- **GPU-dependent texture bindings and complete lighting/color correctness remain open.** Selected shader fixes do not resolve all per-fragment, loop-dependent or storage-buffer-selected texture handles.
- **New scenes can still stutter while shaders compile.** Preload cannot prepare records that have never been encountered.
- **The timing option is experimental.** Full-game simulation, transition, audio and physics behavior remain unproven.
- **This is not a fully Vulkan-clean or long-session-qualified release.** The predecessor's bounded validation did not establish every game, GPU, driver, dynamic transition or device-loss path.

The maintained detail is in **[KNOWN_ISSUES.md](KNOWN_ISSUES.md)**. Back up saves before using experimental builds or changing timing behavior.

## FAQ

**Does “Adaptive Timing” mean a 60 FPS patch?**  
No. It changes a guest timing calculation, which may affect movement speed when throughput falls. It does not generate or render extra frames, and the release does not promise sustained 60 FPS.

**Does preload-all compile the whole game before I play?**  
No. It considers previously encountered compatible pipeline records. New shaders, areas, effects and combinations can still compile during play. Considering more known records can also increase startup time and memory use; use the bounded policy described above if needed.

**Are the 0.75x / Low settings already the app's defaults?**  
No. They are a manual, game-specific development profile. The separate shipped switch file already provides its listed overrides; the underlying C++ defaults are retained.

**Do I need another person's profile or a shader pack?**  
No. Configure your own files and input device. The package intentionally excludes personal profiles and game-derived caches.

**Will it work on another OS or every GPU?**  
Only this Windows x86-64 preview has the recorded release build/check coverage. Other platforms, GPU vendors and drivers are not qualified by those results. No universal minimum hardware requirement or compatibility guarantee is established here.

**How do I update?**  
Download a later published package from [Releases](https://github.com/yoyodapro/Lumaquince/releases) when available and extract it into a new folder. Automatic update checking, web service and Discord presence are disabled in this release configuration. That does not constitute a complete audit of every inherited network component.

## Building and inspecting the source

The complete release includes **`source/`**, **`dependency-sources/`**, **`BUILD-RECIPE.md`**, provenance records and applicable license notices. Use those release-specific inputs for a meaningful comparison with the published executables. The GitHub tree is also available for source inspection and development.

Start with the inherited [build guide](docs/Build.md), [dependency guide](docs/Deps.md) and the release's build recipe. An upstream repository mentioned in inherited documentation is background; keep this fork's matching source when reproducing this release.

```powershell
$SourceRoot = "path\to\matching-source"
$BuildRoot = "path\to\new-build-directory"
$DependencyRoot = "path\to\dependencies"
$ToolRoot = "path\to\build-tools"
& "$SourceRoot\tools\windows\build-lumaquince.ps1" `
    -BuildDir $BuildRoot -DepsDir $DependencyRoot -ToolDir $ToolRoot -Parallel 12
```

Prerequisites, dependency versions and shader/compiler tools must match the recipe. `-ConfigureOnly` stops after configuration; `-SkipTests` compiles without qualifying the resulting build. Missing inputs can require downloads. A changed executable or dependency set does not inherit this release's test result.

Recovered dependency materials include source, selected pins, patches, recipes and notices; independent bit-for-bit rebuilds of every prebuilt static dependency were not performed. The FFmpeg/dav1d reconstruction recipe is explicitly untested, and byte-identical timezone-data conversion is not established. A completely offline or byte-for-byte reproducible build is not claimed. See [validation](VALIDATION.md#source-and-dependencies) and [upstream/source guidance](UPSTREAM.md#licenses-and-full-source).

## Reporting an issue

Use [GitHub Issues](https://github.com/yoyodapro/Lumaquince/issues). Include the release version and EXE checksum, game/update version, GPU and driver, relevant settings, timing Add-On state, and reproducible steps. Note whether the behavior also occurs with High GPU accuracy and with Adaptive Timing disabled, if you have checked those comparisons.

Review a minimal log excerpt before posting. Remove personal names, paths, account data, private URLs and unrelated sessions. Do not attach games, keys, firmware, saves, shader caches, executable dumps or raw GPU captures. See **[PRIVACY.md](PRIVACY.md)** for the public-distribution policy and reporting guidance.

## Credits, license and independence

Lumaquince builds on **[Lichii](https://github.com/zortos293/Lichii)** and **[Eglantine](https://github.com/Djihads80/Eglantine)**, with substantial work inherited from **Eden, yuzu, Sudachi and their contributors**. Eglantine's original developer Camille LaVey and the original contributors retain their credit. Retained Lichii artwork credited to YOB (yobslab) retains that credit. The timing approach derives from the optional work in the [sharkiix-v2 release](https://github.com/Shakoquo/Lichii-sharkiix-version/releases/tag/sharkiix-v2), with additional integration and preservation work here. The complete provenance account is in **[UPSTREAM.md](UPSTREAM.md)**.

Fork-specific changes and documentation are credited to **Lumaquince Contributors**. This repository is published through [yoyodapro](https://github.com/yoyodapro). The project name does not replace inherited authorship.

See **[LICENSE.txt](LICENSE.txt)** for the GNU GPL version 3 text. Existing SPDX identifiers and inherited GPL version alternatives remain applicable to their source files; third-party components retain their own licenses and notices. The matching modified source and dependency-source materials accompany the complete application release. Do not assume every component uses a single license or that the package review constitutes blanket legal/license approval.

Lumaquince is independent. It is not an official Lichii, Eglantine, Eden, yuzu or Sudachi release, and it is not affiliated with or endorsed by Nintendo or the OOT remake's developers. Upstream references acknowledge contributions and do not imply endorsement.
