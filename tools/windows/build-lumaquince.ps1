# SPDX-FileCopyrightText: Copyright 2026 Lumaquince contributors
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$BuildDir = '',
    [string]$DepsDir = '',
    [string]$ToolDir = '',
    [ValidateRange(1, 256)][int]$Parallel = 12,
    [switch]$DisableLto,
    [switch]$ConfigureOnly,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$sourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$projectDir = Split-Path $sourceDir
if (!$BuildDir) { $BuildDir = Join-Path $projectDir 'build' }
if (!$DepsDir) { $DepsDir = Join-Path $projectDir 'deps' }
if (!$ToolDir) { $ToolDir = Join-Path $projectDir 'tools' }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$DepsDir = [IO.Path]::GetFullPath($DepsDir)
$ToolDir = [IO.Path]::GetFullPath($ToolDir)
if ($BuildDir -eq $sourceDir) { throw 'An out-of-source build directory is required.' }

. (Join-Path $PSScriptRoot 'load-msvc-env.ps1')
$patchDir = Join-Path $ToolDir 'PortableGit\usr\bin'
$glslang = Join-Path $ToolDir 'glslang-16.6.0\bin\glslang.exe'
if (!(Test-Path -LiteralPath (Join-Path $patchDir 'patch.exe')) -or
    !(Test-Path -LiteralPath $glslang)) {
    throw 'ToolDir must contain PortableGit/usr/bin/patch.exe and glslang-16.6.0/bin/glslang.exe.'
}
$env:Path = $patchDir + [IO.Path]::PathSeparator + $env:Path
$env:CPM_SOURCE_CACHE = $DepsDir
$ltoSetting = if ($DisableLto) { 'OFF' } else { 'ON' }
& cmake -S $sourceDir -B $BuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release -DYUZU_TESTS=ON -DBUILD_TESTING=ON -DDYNARMIC_TESTS=OFF `
    -DZLIB_BUILD_TESTING=OFF "-DCPM_SOURCE_CACHE=$DepsDir" "-DENABLE_LTO=$ltoSetting" `
    -DYUZU_CMD=ON -DYUZU_ROOM=OFF -DYUZU_ROOM_STANDALONE=OFF `
    -DYUZU_USE_BUNDLED_QT=ON -DYUZU_USE_BUNDLED_FFMPEG=ON -DYUZU_USE_BUNDLED_SDL3=ON `
    -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON `
    "-DGLSLANGVALIDATOR=$glslang" -DUSE_DISCORD_PRESENCE=OFF `
    -DENABLE_UPDATE_CHECKER=OFF -DENABLE_WEB_SERVICE=OFF `
    '-DCMAKE_EXE_LINKER_FLAGS=/PDBALTPATH:%_PDB%'
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
if ($ConfigureOnly) { return }
& cmake --build $BuildDir --target yuzu yuzu-cmd tests update_checker_tests --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed; do not package this build.' }
if ($SkipTests) {
    Write-Warning 'Compilation only: tests were NOT RUN. This build is not qualified for distribution.'
    return
}
& ctest --test-dir $BuildDir -C Release --output-on-failure --timeout 180
if ($LASTEXITCODE -ne 0) { throw 'Tests failed; do not package this build.' }

# Distribution is a separate explicit step: binaries, required runtime DLLs, source and notices.
# Never copy a user directory, game, firmware, keys, log, dump, cache or PDB into the package.
