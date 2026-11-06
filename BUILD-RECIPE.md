# Windows release build recipe

The matching project source is in `source`. Dependency source, applicable patches,
producer recipes and notices are in `dependency-sources`. Read `VALIDATION.md` and
the current dependency provenance report before claiming reproducibility.

The release used MSVC 19.51.36260.0 (Visual Studio 2026 Build Tools), the Windows
10.0.26100 SDK, CMake with Ninja, GNU patch, and glslang 16.6.0. Qt and the other
prebuilt static dependency versions are pinned separately in the dependency
provenance manifest. Do not silently substitute a similarly named source-cache
alternative for a selected prebuilt dependency.

Use a fresh neutral working directory. The build script locates a compatible MSVC
installation and accepts explicit build, dependency and tool directories:

```powershell
& .\source\tools\windows\build-lumaquince.ps1 `
    -BuildDir '<neutral-build-directory>' `
    -DepsDir '<neutral-dependency-directory>' `
    -ToolDir '<build-tools-directory>' `
    -Parallel 4 -SkipTests
```

`ToolDir` must contain `PortableGit/usr/bin/patch.exe` and
`glslang-16.6.0/bin/glslang.exe`. These are build tools, not part of the application
runtime. `-SkipTests` means compilation only and does not qualify a release.
`-ConfigureOnly` stops after configuration. On a suitable native test environment,
omit `-SkipTests` or explicitly run CTest after compilation.

The release configuration is Release with `ENABLE_LTO=ON`, `YUZU_TESTS=ON`,
`BUILD_TESTING=ON`, `DYNARMIC_TESTS=OFF`, `ZLIB_BUILD_TESTING=OFF`, `YUZU_CMD=ON`,
`YUZU_ROOM=OFF`, `YUZU_ROOM_STANDALONE=OFF`, bundled Qt/FFmpeg/SDL3 enabled,
`VulkanHeaders_FORCE_BUNDLED=ON`, `VulkanUtilityLibraries_FORCE_BUNDLED=ON`,
`USE_DISCORD_PRESENCE=OFF`, `ENABLE_UPDATE_CHECKER=OFF` and `ENABLE_WEB_SERVICE=OFF`.
The executable linker uses `/PDBALTPATH:%_PDB%` so its CodeView reference contains a
PDB basename. Full debug files and compiler intermediates are not distributed.

The release uses `SOURCE_DATE_EPOCH=1793923200` for neutral release metadata.
Inherited compiler/linker environment overrides were cleared. It is a plain LTO
build, without instrumentation, a PGO training runtime or an optimized PGO claim.

For the exact public generated timezone data, extract the supplied 250725 archive
into a fresh data directory, then set `YUZU_TZDB_PATH` to the directory containing
its `zoneinfo` subdirectory. This can be set in the CMake cache before invoking the
helper, or supplied with the equivalent explicit CMake configuration. Do not use
console firmware as a substitute. The original IANA input is not fully proved;
the provenance report distinguishes the supplied candidate source.

The inherited CI-package helper can still request remote checksums. A fully
offline source-dependency build is not promised. Producer CI files inside source
archives are historical build evidence: do not replay host setup, security,
installer, publishing or account operations from them. Use only reviewed required
compilation steps. Any reconstructed dependency recipe is labeled separately from
the original producer recipe and from an executed rebuild.

Build the CMake targets `yuzu`, `yuzu-cmd`, `tests` and `update_checker_tests`.
Their application outputs are `lumaquince.exe` and `lumaquince-cli.exe`. Internal
target and namespace names remain compatible with inherited code.

The packaged source identity lists compiler inputs separately from release records
such as this recipe and `VALIDATION.md`. A rebuild changes the executable identity
and requires its own tests and privacy review; the provided results do not transfer
to changed source, tools or dependency inputs.
