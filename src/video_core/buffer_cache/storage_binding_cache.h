// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "common/common_types.h"

namespace VideoCommon {

struct StorageBindingPolicy {
    bool written{};
    bool native_pointer{};
    bool table_pointer{};
    bool mapping_size{};

    bool ReadsSizeWord() const {
        return !(table_pointer && !written) && !(native_pointer && mapping_size);
    }

    u32 Flags() const {
        return u32{written} | (u32{native_pointer} << 1) | (u32{table_pointer} << 2) |
               (u32{mapping_size} << 3);
    }
};

/// Only address translation and size policy are reusable: host buffers and sparse segments are not.
struct NumericStorageBinding {
    DAddr device_addr{};
    GPUVAddr gpu_addr{};
    u32 size{};

    bool operator==(const NumericStorageBinding&) const = default;
};

class StorageBindingCache {
public:
    struct Key {
        size_t owner{};
        u64 serial{};
        GPUVAddr gpu_addr{};
        u64 next_qword{};
        u32 alignment{};
        u32 flags{};

        bool operator==(const Key&) const = default;
    };

    template <typename Memory, typename Resolve>
    NumericStorageBinding Get(Memory& memory, GPUVAddr descriptor, u32 alignment,
                              StorageBindingPolicy policy, Resolve&& resolve) {
        // Guest pointer/size words can change without a mapping change. Read them on every bind.
        const GPUVAddr gpu_addr = memory.template Read<u64>(descriptor);
        if (gpu_addr == 0) {
            return {};
        }
        const u64 next_qword = policy.ReadsSizeWord() ? memory.template Read<u64>(descriptor + 8) : 0;
        const Key key{memory.GetID(), memory.MapSerial(), gpu_addr, next_qword, alignment,
                      policy.Flags()};
        auto& set = entries[((gpu_addr ^ next_qword ^ key.flags) * 0x9E3779B97F4A7C15ULL) >> 55];
        for (size_t way = 0; way < Ways; ++way) {
            if (set[way].valid && set[way].key == key) {
                const auto result = set[way].binding;
                std::rotate(set.begin(), set.begin() + way, set.begin() + way + 1);
                return result;
            }
        }
        const NumericStorageBinding result = resolve(key);
        std::rotate(set.begin(), set.end() - 1, set.end());
        set[0] = {key, result, true};
        return result;
    }

private:
    static constexpr size_t Ways = 4;
    struct Entry {
        Key key;
        NumericStorageBinding binding;
        bool valid{};
    };
    std::array<std::array<Entry, Ways>, 512> entries{};
};

} // namespace VideoCommon
