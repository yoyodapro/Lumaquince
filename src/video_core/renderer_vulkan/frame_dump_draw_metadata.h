// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <span>
#include <string>
#include <fmt/format.h>
#include "common/common_types.h"

namespace Vulkan {

// Capture-only metadata. Fixed six-slot records cannot grow with the number of unique shaders.
class FrameDumpDrawMetadata {
public:
    static constexpr u32 MaxDraws = 16384;

    std::optional<std::string> Record(u32 draw, u32 pass, std::span<const u64, 6> hashes) {
        if (recorded == MaxDraws) {
            ++dropped;
            return std::nullopt;
        }
        ++recorded;
        return fmt::format(
            R"({{"kind":"draw","draw":{},"pass":{},"shader_slots":["{:016x}","{:016x}","{:016x}","{:016x}","{:016x}","{:016x}"]}})",
            draw, pass, hashes[0], hashes[1], hashes[2], hashes[3], hashes[4], hashes[5]);
    }

    std::string Summary() const {
        return fmt::format(
            R"("draw_metadata_limit":{},"draw_metadata_records":{},"draw_metadata_dropped":{},"draw_metadata_truncated":{})",
            MaxDraws, recorded, dropped, dropped != 0);
    }

private:
    u32 recorded{};
    u64 dropped{};
};

} // namespace Vulkan
