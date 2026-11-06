// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "common/literals.h"

namespace VideoCommon {

// Both sides of the comparison must include the cap and environment overrides.
template <typename ApplyThresholds>
bool ApplyTextureThresholdsAndShouldLog(u64 previous_critical, bool first_sample,
                                      ApplyThresholds&& apply_thresholds) {
    using namespace Common::Literals;
    const u64 critical = apply_thresholds();
    const u64 change = critical > previous_critical ? critical - previous_critical
                                                   : previous_critical - critical;
    return first_sample || change >= 256_MiB;
}

} // namespace VideoCommon
