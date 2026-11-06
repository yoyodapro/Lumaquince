> **Publication note:** This record describes the qualified package before public publication. The earlier local-only destination statement below is superseded by the official public repository and release at https://github.com/yoyodapro/Lumaquince and https://github.com/yoyodapro/Lumaquince/releases/tag/v0.1.0-preview.1. The immutable complete ZIP retains its original prepublication record. Publication documentation changes do not rebuild its executables.

# Lumaquince 0.1.0-preview.1 validation

This record covers the files in this release. Source review, compilation, synthetic tests and gameplay observations are separate results.

## Release identity

| Item | Result |
|---|---|
| Project source | `e1e8299456cbccc35c88c9ddd9070e15fa81c7f0` â€” fresh local history under Lumaquince Contributors |
| GUI SHA256 | `29c20f1002bae38a2581cc21779e41731db5a7162a2a1199561ace4730fad802` |
| CLI SHA256 | `4571de7dd58a25a302a4074973622948649bb5aa1d8708f5dbd5198223c82f81` |
| Build | Windows x86-64 Release, MSVC 19.51, link-time optimization |
| GUI mode | Plain LTO; actual expanded link recipe, source inputs and PE imports verified, with no PGO runtime dependency |
| Native tests | PASS — 841 total cases across the unfiltered main and updater suites (833 + 8); 17,140,991 assertions |
| Focused timing tests | PASS — four cases and 117 assertions in an additional focused run |
| Emitted SPIR-V validation | PASS — all 28 actual emitted modules validated offline |
| Complete required checks | PASS — all 42 required checks |
| File privacy review | Frozen source and all 15 release binary inputs independently reviewed; final archive review is recorded alongside the archive |

The full native suite ran through the same physical PC's isolated Windows Sandbox route, using the release's exact test binaries and thirteen pinned Microsoft runtime DLLs. No game, keys, firmware, save or personal profile was a test input. Host security policy was unchanged. Native success does not establish game behavior, normal simulation speed, complete Vulkan correctness or support on every PC.

Host GUI startup, save transfer, new gameplay, image correctness and live GPU performance were not exercised for this rebuilt executable. The player retained control of an existing game session. Static PE/link inspection and native tests are not substitutes for those runtime checks.

## Gameplay evidence and remaining issues

An earlier private experiment using the same 176-byte timing payload received strong positive user feedback, including movement closer to normal speed. The new built-in integration is guarded and tested separately. That observation is qualitative feedback about the earlier executable; it is not a controlled benchmark or a complete-game acceptance test of this rebuilt executable.

Lake Hylia water strobing remains unresolved. Invalid GPU-dependent texture bindings and incomplete lighting/color correctness remain open. The timing option has shared-state, smoothing, low-throughput, transition, audio, physics and exception/unwind limitations. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) and [DIFFERENCES.md](DIFFERENCES.md).

The editable `windows/nx2_switches.txt` matches the tested profile: split mode 2, fence batching enabled, native draw flush budget 4096 and encountered-cache preload-all enabled. These are file overrides; the C++ bounded preload and native draw-budget defaults are unchanged. Preload-all can increase startup time and memory use and cannot prepare unseen shaders. No game-derived cache is supplied.

## Source and dependencies

The `source` directory contains the matching modified project source and build scripts. `SOURCE-AND-BINARY-IDENTITY.json` identifies the compiled tree; the packaged `VALIDATION.md` copies are release records added after compilation, not compiler inputs. Git history and local repository configuration are excluded from the archive.

The separately inventoried `dependency-sources` directory contains recovered source, applicable patches, recipes and notices for the selected dependencies, including Qt, FFmpeg and its AV1 dependency, OpenSSL, SDL, Sirit and the selected CPM components. Use its current provenance manifest, source allowlist and public report; do not treat upstream CI workflow files as local setup instructions.

Qt module source identities and cached SDK SBOM checksums were reconciled. QtCharts retains its GPL terms. OpenSSL's artifact label and actual development-library version are distinguished. Sirit's BSD source headers and differing root license files are all preserved. No blanket single-license assertion is made for third-party components.

A clearly labeled, untested FFmpeg/dav1d reconstruction recipe is included with recovered source pins, observed options and guarded object-archive merging. The original producer invocation and byte-identical output remain unproved. Do not present the reconstruction as an executed dependency rebuild.

The exact public generated timezone data used by this build is supplied separately from console firmware. Its recorded converter tag does not prove the original IANA input. A candidate IANA source is included, and byte-identical conversion was not established. Independent bit-for-bit rebuilds of all prebuilt static dependencies were not performed. A completely offline dependency build is not claimed; the inherited CI-package helper may still fetch checksums.

Microsoft runtime libraries were copied unchanged from official installed redistributable directories, with valid Microsoft signatures and recorded hashes. Their applicable notices and SDK license accompany the application. Optional NVIDIA runtime DLLs are absent. System libraries and build tools retain their own licensing.

## Privacy scope

The release was assembled from explicit source, executable, runtime and dependency-source inventories. Personal profiles, accounts, saves, game files, decryption keys, firmware, shader caches, logs, dumps, screenshots, private build receipts, PDBs, object files and personal Git history were excluded. Normal source code and public cryptographic test fixtures supplied by dependencies are distinct from personal or console decryption keys.

All 5,051 frozen project-source pins and the 15 release binary hashes were independently reconciled. The two owned executable CodeView references contain PDB basenames. Twelve scanner findings in the GUI were matched to six exact boundaries in a public Qt geographic timezone table, with duplicate UTF-16 interpretations; the entire canonical table matches the supplied Qt source and static library. This is an exact provenance classification, not a general exemption for an identifier. Public vendor signatures and build metadata are preserved in unchanged Microsoft DLLs. Public Gradle build-tool JARs and upstream compression test fixtures remain identified source materials, not personal compiler output. See the accompanying package review and checksum file for final archive readback.

Public upstream author names, copyright notices and licenses were retained. Known-identifier and path scans cannot prove anonymity against every encoding, arbitrary personal identifier or external publication trail. Publishing through a personal account can identify the publisher independently of the archive contents.

## Rebuilding

Use `source/tools/windows/build-lumaquince.ps1` with explicit source-adjacent build, dependency and tools directories. See the source README and dependency recipes for versions and input selection. `-ConfigureOnly` performs configuration; `-SkipTests` performs compilation without qualifying the result. Do not reuse the native test result here for a changed executable or dependency set.

No online repository, release endpoint or update service is represented as this fork's official destination. This package is a local experimental preview.
