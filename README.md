<!--
SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2018 yuzu Emulator Project
SPDX-License-Identifier: GPL-2.0-or-later
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Lumaquince

**0.1.0-preview.1 — experimental Windows fork**

Lumaquince is an independent, open-source Nintendo Switch and Switch 2 emulator fork based on Lichii and Eglantine. It retains the work of Eden, yuzu, Sudachi and their contributors, with a set of focused renderer, shader, cache, memory and synchronization changes.

This preview focuses on the Windows x86-64 Release build with link-time optimization. It is not an official release of Lichii or Eglantine and is not affiliated with or endorsed by Nintendo.

The release is intended to provide the application and its matching full modified source. Games, keys and firmware are excluded. Supply your own lawfully obtained files; this project does not provide download links for them. See the packaged [VALIDATION.md](VALIDATION.md) for the exact build, check and package-review status.

## What is different?

The fork combines retained work on task/mesh shader-cache persistence, command-state reuse, buffer ownership and bounded worker lifetimes with additional fixes for shader atomics, high-register operands, readback bounds, query ordering, physical remap notifications, presentation resource reuse and Vulkan synchronization.

It also includes narrow Windows surface, dynamic vertex-stride and ASTC linear/sRGB-view fixes. These changes preserve required guest work and synchronization. They are not a complete renderer rewrite or a guarantee that every scene is correct.

The built-in **Adaptive Timing (Experimental)** option is enabled by default for one exact supported Ocarina of Time build. It adjusts a guest timing step using elapsed time; it does not increase the number of rendered frames. Unsupported builds skip it. To turn it off, open the game's Properties → Add-Ons, uncheck Adaptive Timing (Experimental), and restart the game. See [the differences](DIFFERENCES.md) for compatibility guards and [known issues](KNOWN_ISSUES.md) for limitations.

## Preview status

See the packaged [VALIDATION.md](VALIDATION.md) for the exact Lumaquince build and check status. Earlier native, shader and gameplay checks apply to the development predecessor, not automatically to a rebuilt Lumaquince executable.

That predecessor passed substantial native and SPIR-V checks and a bounded Vulkan/gameplay review on a Windows NVIDIA test PC. Invalid texture bindings and visible water strobing remain unresolved. An earlier experiment using the same reviewed timing payload received strong positive feedback and movement closer to normal speed; this is qualitative feedback, not a controlled whole-game timing result.

This is an experimental preview. Rendering errors, slowdowns, crashes and save-affecting bugs remain possible. There is no sustained 60 FPS, complete lighting, exact simulation-speed or whole-game stability guarantee.

## Running the Windows package

1. Extract the complete application archive into a new folder. Keep its runtime libraries beside the executable.
2. Back up existing saves and settings before opening them in an experimental version.
3. Run lumaquince.exe. The command-line frontend is lumaquince-cli.exe.
4. Configure your own input device and legally obtained game/system files.
5. Check [known issues](KNOWN_ISSUES.md) before enabling experimental options.

Lumaquince uses its own Lumaquince application-data directory, normally %APPDATA%\Lumaquince on Windows. The public package must not contain a personal profile, saves, game-derived shader caches, account data or a preconfigured game library. Existing settings and environment variables can override clean defaults.

Do not copy another person's entire profile to reproduce a problem. Make a separate test profile when comparing settings, and preserve the original save backup.

The package's editable dist/nx2_switches.txt profile selects split mode 2, fence batching, a 4096 draw-flush budget and preload of encountered compatible pipelines. Preloading can use more startup time and memory. Remove its EDEN_NX2_PRELOAD_ALL_PIPELINES line to use the unchanged bounded used-list preload policy. Update checking, web service and Discord presence are disabled in the release configuration.

## Building from the source

Use the matching Lumaquince source package. The inherited [build guide](docs/Build.md) and [dependency guide](docs/Deps.md) describe the toolchain; references to upstream repositories in those guides are background, not instructions to replace this fork's source.

Use the source package's Windows build helper with your dependency and tool directories:

~~~powershell
$SOURCE = "path\to\matching-source"
$BUILD = "path\to\new-build-directory"
$DEPS = "path\to\dependencies"
$TOOLS = "path\to\build-tools"
& "$SOURCE\tools\windows\build-lumaquince.ps1" -BuildDir $BUILD -DepsDir $DEPS -ToolDir $TOOLS -Parallel 12
~~~

Run this with the prerequisites from the dependency guide and the matching source/dependency package. Dependency selection, shader tools and compiler versions must match the release build recipe for a meaningful comparison. Configuration may need dependency downloads if inputs are missing.

The helper also accepts ConfigureOnly and SkipTests. ConfigureOnly stops after configuration. SkipTests bypasses testing and does not produce a test-qualified build. Some inherited CMake targets and internal names still use yuzu, Eden, Eglantine or Lichii; they do not identify an official upstream release.

These commands describe a build path; they do not assert byte-for-byte reproducibility or replace the release-specific toolchain and dependency manifest. See [release notes](RELEASE_NOTES.md) for the validation boundary.

## Reporting problems

Until a public issue tracker is announced, use the channel that supplied the preview. Include:

- The exact Lumaquince version and executable checksum.
- Game/update version, relevant settings and whether the timing Add-On was enabled.
- GPU and driver version, and clear reproduction steps.
- A minimal, reviewed log excerpt. Remove personal paths, account data and unrelated sessions.

Please do not attach games, keys, firmware, executable game dumps, save files, shader caches or frame captures containing game resources. See [privacy guidance](PRIVACY.md).

## Credits and license

Lumaquince builds on [Lichii](https://github.com/zortos293/Lichii), [Eglantine](https://github.com/Djihads80/Eglantine), Eden, yuzu and Sudachi. Original source authors, copyright notices, SPDX headers and third-party licenses remain applicable.

See [LICENSE.txt](LICENSE.txt), the notices shipped with the source and [upstream provenance](UPSTREAM.md). New fork-specific documentation and changes are credited to Lumaquince Contributors. A new name does not replace upstream authorship or licensing.


The release source omits an unused upstream opaque Clang PGO profile. This Windows build uses plain MSVC LTO. An inherited ignore rule is anchored to the root output directory so the audio source directory is included in clean checkouts. Inherited CI workflows are retained as inactive references under `.github/upstream-reference`; they do not publish or manage issues for this fork. Upstream source-image filename metadata was removed without changing image pixels or copyright notices.

See [Recreating the Ocarina gameplay profile](GAMEPLAY-PROFILE.md) for the optional generic settings guide.
