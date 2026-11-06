# Agent guidance

Read `docs/Coding.md` before source changes. Preserve public copyright, license notices, and upstream attribution. Lumaquince derives from Lichii, Eglantine and Eden, including work inherited from yuzu and Sudachi.

Keep changes bounded and review upstream fixes before duplicating work. Do not fetch, merge or change remotes unless the task authorizes it. Do not embed developer names, home directories, machine names, private logs or game data in source or release artifacts.

## Build and validation

Use an out-of-source build and an explicit dependency cache. On Windows use `tools/windows/build-lumaquince.ps1` with a compatible MSVC or clang-cl environment, CMake and Ninja. Pass build, dependency and tools directories explicitly when defaults are unsuitable. Select parallelism for the machine. Test the changed behavior and report source checks, compilation, synthetic tests and live observations separately.

## Debugging

Diagnostics are off unless explicitly enabled. Existing `EDEN_NX2_*` variable names remain for compatibility. See `docs/nx2/debug-mcp.md` and `tools/nx2/shader-sweep.md` for tooling. Use full shader hashes and restore reversible rejection experiments. Frame dumps, minidumps and RenderDoc captures can contain game assets and private paths; keep them out of source and distribution packages.

Never start, stop, inject into, capture, send input to or query a running private game without explicit authorization for that session. The owner may be playing. Use synthetic fixtures for automated testing; do not ship extracted game bytes or personal profiles. A shader replacement or changed timing is not proof of a graphics fix or measured FPS gain.

## Work ownership

Keep one accountable owner for each source change. Delegate only when the user or task specifically requests it. Use GPT-6 Astra with high reasoning for hard emulation, shader and GPU changes and their independent review. Do not substitute synthetic test success for runtime acceptance.

Experimental game-specific behavior must be visibly labeled, reversible through settings or Add-Ons, and guarded to the reviewed title and build. Unknown layouts must be refused without partial mutation.
