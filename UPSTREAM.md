<!--
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Upstream credit and source provenance

Lumaquince is based on [Lichii](https://github.com/zortos293/Lichii), which is a fork of [Eglantine](https://github.com/Djihads80/Eglantine). It also inherits substantial contributions from Eden, yuzu, Sudachi and their contributors.

Eglantine's original developer Camille LaVey and the original project contributors retain their credit. Lichii artwork credited to YOB (yobslab) retains that credit wherever the artwork is retained. Fork-specific changes and new documentation are credited to Lumaquince Contributors.

A rebrand does not transfer authorship of inherited code, artwork or third-party components. Existing source headers, copyright notices, SPDX identifiers, license texts and dependency notices remain applicable.

## Selected source references

These are public provenance references for ideas or selected changes reviewed during development. An adaptation is not necessarily a complete cherry-pick of the linked proposal, and a link does not imply upstream endorsement or a local runtime/performance result.

| Public reference | Relationship to this fork |
|---|---|
| [Lichii PR 121](https://github.com/zortos293/Lichii/pull/121) | Inherited query submission/sample-bound behavior; its remaining count-loss limitation is documented. |
| [Lichii PR 150](https://github.com/zortos293/Lichii/pull/150) | Inherited Ocarina rendering/pointer work. It is not a complete GPU-indexed texture-selection solution. |
| [Eglantine PR 276](https://github.com/Djihads80/Eglantine/pull/276) | Source-owner tracking approach adapted for gathered-buffer dependencies and retirement. |
| [Lichii PR 166](https://github.com/zortos293/Lichii/pull/166) | Selected diagnostic synchronization; the complete experimental bundle was not imported. |
| [Lichii PR 173](https://github.com/zortos293/Lichii/pull/173) | Depth-comparison floating-point image typing. |
| [Lichii PR 182](https://github.com/zortos293/Lichii/pull/182) | Present-interval diagnostics; present intervals are not simulation updates or scanout measurements. |
| [Lichii PR 184](https://github.com/zortos293/Lichii/pull/184) | Selected bounded texture-conversion worker work, adapted with ownership/admission safeguards. |
| [Lichii PR 185](https://github.com/zortos293/Lichii/pull/185) | Deferred-clear ordering before image writes and copies. |
| [Lichii PR 189](https://github.com/zortos293/Lichii/pull/189) | Selected shader operand/shadowing work; unrelated frame-generation/fence edits were excluded. |
| [Lichii PR 193](https://github.com/zortos293/Lichii/pull/193) | Portable collected-range size type, adapted to the retained coalescer. |
| [Lichii PR 194](https://github.com/zortos293/Lichii/pull/194) | Selected command/buffer/readback work. Its full settings and feature bundle was not imported. |
| [Lichii PR 206](https://github.com/zortos293/Lichii/pull/206) | CAS retry and associated packed/shared-memory shader fixes. |
| [Lichii PR 215](https://github.com/zortos293/Lichii/pull/215) | Query report publication before macro DMA reads. |
| [Eglantine PR 344](https://github.com/Djihads80/Eglantine/pull/344) | Windows allocation failure/fallback cleanup. |
| [sharkiix-v2 release](https://github.com/Shakoquo/Lichii-sharkiix-version/releases/tag/sharkiix-v2) | Source of the elapsed-time timing approach reviewed for the optional experiment; this fork adds context-preservation work and does not import the entire release patch. |

Additional upstream-derived work includes:

- Eglantine's “shader: mesh programs that output points or lines.”
- Historical Lichii descriptor-buffer work that avoids unused texel buffer views.
- Selected adaptations from Eglantine's “Keep adjusting memory barriers to be more explicit,” covering image-copy/reinterpretation source-layout restoration and matching stage/access publication.

Their adaptations are described in [DIFFERENCES.md](DIFFERENCES.md). They are not presented as inventions unique to Lumaquince. No private development branch names or local commit identities are required in public user documentation.

The sharkiix release asset's included source patch was the reviewed timing source. The release tag's source tree did not fully represent that asset's changes. The whole release must not be treated as if every feature had been imported or qualified here.

## Licenses and full source

The root [LICENSE.txt](LICENSE.txt) contains the GNU General Public License version 3. Source files retain their existing SPDX licensing, including inherited GPL version alternatives where applicable. Third-party components retain their own licenses.

Binary distribution must be accompanied by the matching full modified source promised for this preview, including the fork's changes, applicable build scripts, dependency versions and local patches. Dependency source and notices must be supplied as required by the applicable component licenses. A source archive containing only tracked project files is not, by itself, a verified complete-source audit of every distributed dependency.

Release preparation must reconcile generated/build inputs and dependency delivery with the actual binaries, including components with GPL terms and mixed source notices. See the packaged [VALIDATION.md](VALIDATION.md) for actual source-recovery, notice and package-review results. This documentation makes no blanket claim that all dependencies use one license or that a complete corresponding-source audit has passed.

Do not remove inherited source credit to sanitize a public package. Remove personal local development metadata while preserving legitimate public upstream authorship and licensing.

## Independence

Lumaquince is not an official Lichii, Eglantine, Eden, yuzu or Sudachi release. It is not affiliated with or endorsed by Nintendo. Referencing an upstream change acknowledges provenance; it does not imply support or endorsement by that project's contributors.


The release source omits an unused upstream opaque Clang PGO profile. This Windows build uses plain MSVC LTO. An inherited ignore rule is anchored to the root output directory so the audio source directory is included in clean checkouts. Inherited CI workflows are retained as inactive references under `.github/upstream-reference`; they do not publish or manage issues for this fork. Upstream source-image filename metadata was removed without changing image pixels or copyright notices.
