<!--
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Quality 03 validation

The GitHub release is **`v0.1.0-preview.1-quality.3`**, titled **Lumaquince Quality 03 — Renderer and pipeline update**. The validated executables retain the internal version **`0.1.0-preview.1`**; they are not the original Preview 1 binaries. Identify this build by the Quality 03 release tag, compiled-source commit `bcbe1db80b66c33deb2fc9e96fffbd1a9229dd91` and executable checksums below. No executable was rebuilt just to change its displayed version.

The release archive is **`Lumaquince-quality-03-complete.zip`**, containing **`Lumaquince-quality-03/`**. Its `source/` directory preserves the exact compiled source. Updated documentation at the package root and in `windows/` is a release-record overlay; historical documentation inside `source/` remains part of the frozen compiler-source snapshot. A later public documentation commit does not change the binary identity.

## Exact build and isolated checks

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

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `lumaquince.exe` | 60,953,088 | `d5bee379450344b61a951cef04e7748411a4cc6a81c1835034623a79e10b9811` |
| `lumaquince-cli.exe` | 38,233,600 | `8187e62196837551ff8edac47d4a232b829c6dfd1e6b3f8bf0b0c09de0a435e6` |
| `tests.exe` | 33,531,392 | `857d28bd859cebaa9c7a9724a97bcb911ac2230872e91e370da3abbb4862d6b5` |
| `update_checker_tests.exe` | 7,266,816 | `98a81d4b71764d83cdb7002740473db8022a524639a519a84547b00ed5f1945d` |

The completed validation record is dated 2026-11-06. Its full-suite assertion total is 17,338,931. Focused reruns and their assertions are separate from that full-suite result. The required-command inventory, execution order and exported-module inventory were checked against the exact test binaries and completed outputs; the 150 new alignment modules are included in the 178 total, not an additional set.

The exact native test binaries ran in an isolated Windows Sandbox on the same physical test PC. No game, decryption keys, firmware, saves or personal profile were test inputs. Production NONE mode and existing validation guards were retained. Compilation, host fixtures and offline shader validators do not run the game renderer or demonstrate correct GPU data, images or performance.

Coverage includes production-path uniform ownership and mixed bindings; mapped and staged host-read synchronization; shader register pressure and storage alignment; query publication; pipeline ownership, exceptions, multiple waiters, destruction and bounded reload reuse; worker cancellation; and the existing GPU split lifetime checks. These are bounded fixtures, not exhaustive scheduling or device coverage.

## Gameplay evidence and remaining issues

The latest accepted Quality 02 predecessor received qualitative feedback that the sky beams were gone and materials looked normal. That observation belongs to the earlier executable built from source `a52eada6b0a7e02b162db5bd0467e8afd1e0e927` and does not qualify the new Quality 03 build. Other reported issues are unresolved or unverified on this candidate, not assertions that each symptom is still visible after those earlier improvements.

The previously accepted development gameplay session and positive movement feedback are evidence for an earlier executable. The same reviewed timing payload remains, but **Quality 03 GUI startup, gameplay, image correctness and live FPS were not exercised**. Earlier observations do not qualify these new renderer and pipeline changes.

Historical observations around 45 FPS in a static view and 49–60 in changing views were made under differing scene/cache conditions. No matched speedup, sustained 60 FPS result, exact whole-game simulation-speed result or broad hardware compatibility is established.

Excessive glare, glowing/stretched triangles, black materials, invalid texture bindings and Lake Hylia water strobing remain checks for a real gameplay session. Shader-time GPU-dependent texture selection, general aliases, lossless query accumulation and quality-preserving large-ASTC residency remain unfinished. Timing retains its shared-state, smoothing, low-throughput, transitions, audio, physics and exceptional-return limits. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

The editable `windows/nx2_switches.txt` retains split mode 2, fence batching, draw-flush budget 4096 and encountered-cache preload-all. These are file overrides; the C++ bounded-preload and draw-budget defaults remain unchanged. Preload-all may increase startup work and memory use. No game-derived cache is supplied.

## Source and dependencies

The complete package's `source/` preserves the compiled project tree and build scripts at the commit stated above. Updated release documents are supplied separately at the package root and in `windows/`. Public repository documentation can be newer than the compiled source; a documentation commit does not rebuild the executables. `SOURCE-AND-BINARY-IDENTITY.json` and the package source inventory identify that separation. Git history and local repository configuration are excluded from the application archive.

The separately inventoried `dependency-sources/` materials contain recovered source, applicable patches, producer/reconstruction recipes and notices. Consult the accompanying provenance report for the selected versions and their relationship to the prebuilt libraries. Do not substitute a similarly named source-cache alternative or treat historical producer CI as local setup instructions.

Third-party components retain their own terms and attribution. QtCharts retains its GPL terms; OpenSSL artifact labels and development-library versions must be distinguished; Sirit's source headers and root license files remain preserved. No blanket single-license or completed legal-compliance claim is made.

Independent bit-for-bit rebuilds of every prebuilt static dependency have not been performed. The labeled FFmpeg/dav1d reconstruction recipe is untested; the original invocation and byte-identical output remain unproved. Supplied generated timezone data is separate from console firmware; the converter tag does not establish the original IANA input, and byte-identical conversion remains unproved. A fully offline dependency build is not claimed.

## Distribution review

Use the release's `SHA256SUMS.txt`, source/binary identity and accompanying package-review records for the final archive size, hashes, inventory and privacy-review result. Earlier Preview 1 package file counts and scanner classifications are not reused as proof for this archive.

The distribution policy excludes games, keys, firmware, personal accounts/profiles, saves, controller mappings, game-derived caches, logs, dumps, screenshots, private build receipts, PDBs, object files and personal Git history. Public upstream author names, copyright notices, public dependency test fixtures and applicable license notices are retained. Hashes prove observed byte identity, not gameplay compatibility or freedom from malware; bounded privacy scans do not prove anonymity against every encoding or identifier.

## Rebuilding

Use [BUILD-RECIPE.md](BUILD-RECIPE.md) with the exact packaged source and selected dependency materials. `-ConfigureOnly` performs configuration; `-SkipTests` compiles without qualifying the build. A changed source tree, executable, toolchain or dependency selection requires its own validation and distribution review.

Official release: [v0.1.0-preview.1-quality.3](https://github.com/yoyodapro/Lumaquince/releases/tag/v0.1.0-preview.1-quality.3). Automatic update checking, web service and Discord presence remain disabled in the build configuration.
