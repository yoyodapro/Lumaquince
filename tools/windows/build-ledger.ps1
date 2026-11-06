# SPDX-FileCopyrightText: Copyright 2026 Lumaquince contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Compatibility entry point. Prefer build-lumaquince.ps1 for new build instructions.
& (Join-Path $PSScriptRoot 'build-lumaquince.ps1') @args
