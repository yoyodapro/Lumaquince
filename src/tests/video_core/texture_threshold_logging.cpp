// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "video_core/texture_cache/threshold_logging.h"

using namespace Common::Literals;

TEST_CASE("Texture threshold logging compares the applied cap and overrides",
          "[video_core][texture_threshold_logging]") {
    for (const u64 override_mib : {0ULL, 10240ULL}) {
        const u64 previous = override_mib != 0 ? override_mib << 20 : 5_GiB;
        for (const u64 raw : {8_GiB, 16_GiB, 20_GiB, 20_GiB + 32_MiB}) {
            u64 critical = raw;
            u32 applications{};
            const bool log = VideoCommon::ApplyTextureThresholdsAndShouldLog(previous, false, [&] {
                ++applications;
                critical = (std::min)(critical, u64{5_GiB});
                if (override_mib != 0)
                    critical = override_mib << 20;
                return critical;
            });
            CAPTURE(override_mib, raw);
            CHECK_FALSE(log);
            CHECK(critical == previous);
            CHECK(applications == 1);
        }
    }
}

TEST_CASE("Texture threshold logging retains first samples and substantial effective changes",
          "[video_core][texture_threshold_logging]") {
    for (const bool first : {false, true}) {
        for (const u64 previous : {4_GiB, 5_GiB}) {
            for (const u64 delta : {0_MiB, 255_MiB, 256_MiB, 512_MiB}) {
                for (const bool increase : {false, true}) {
                    const u64 effective = increase ? previous + delta : previous - delta;
                    u64 critical = 20_GiB;
                    u32 applications{};
                    const bool log = VideoCommon::ApplyTextureThresholdsAndShouldLog(
                        previous, first, [&] {
                            ++applications;
                            critical = effective;
                            return critical;
                        });
                    CAPTURE(first, previous, delta, increase);
                    CHECK(log == (first || delta >= 256_MiB));
                    CHECK(critical == effective);
                    CHECK(applications == 1);
                }
            }
        }
    }
}
