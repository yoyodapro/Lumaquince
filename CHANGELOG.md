# Changelog

All notable changes to Eglantine are documented here. Each release's notes are taken from its
`## [X.Y.Z]` section (release candidates use the section of the version they are a candidate for).
New entries go under `## [Unreleased]`; `tools/ci/bump-version.sh` turns them into a version section.

## [Unreleased]

- Selectively adapt Lichii PRs 295, 290, 194 and 184: alignment-safe storage accesses,
  compute/indirect/graphics and mapped-host-read ordering, worker cancellation wakeups,
  owned native graphics emission jobs and bounded cached-pipeline prebuilding.
  Preserve native draws, existing cache versions and local resource/state fixes.
  See `docs/nx2/async-native-pipelines.md` and `docs/rendering-quality.md` for limits.
- Reconcile GPU-produced texture handles, image/sampler descriptors and storage pointers before
  resolving draw and dispatch bindings; preserve CPU-owned bytes during readback.
- Check feedback barriers against each draw's current sampled views.
- Keep dynamic topology and primitive restart consistent with actual tessellation stages and
  restore state across mesh and ordinary pipeline transitions.
- Retain warm graphics pipelines while driver memory has room, with a separate retained-count
  guard that resumes conservative age trimming.
- Bind read-only table pools beyond the old 64 MiB cap within actual device and mapping limits,
  including alignment and page-padding bounds. Adapted from Lichii PR 351 by jpkto.
- Preserve fence-flagged query-report lifetime and wait for GPU completion before publication,
  including payload, non-async and direct-flush paths. Adapted from Lichii PR 337.

These development changes require exact-build native checks and gameplay review. They do not
establish complete rendering correctness, a measured speedup or whole-game compatibility.
See `docs/rendering-quality.md` for scope and remaining work.

## [0.1.0]

- Initial release candidate
