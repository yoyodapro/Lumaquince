// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <span>
#include <type_traits>

#include "common/common_types.h"

namespace VideoCommon {

template <class Ref>
class UniformStreamReuse;

/// What a buffer cache runtime offers for stream uniform reuse.
///  - StreamUniformRef: a small trivially copyable description of one stream allocation (the
///    host buffer, its offset and whatever else the runtime needs to add it to a descriptor
///    payload again, e.g. the device address of the descriptor buffer path).
///  - UniformReuse(): state owned by this runtime, never shared with another scheduler.
///  - LastStreamUniformRef(): the allocation made by the latest BindMappedUniformBuffer call.
///  - RebindStreamUniform(ref, size): adds `ref` to the descriptor payload exactly as
///    BindMappedUniformBuffer added the allocation the first time.
template <class R>
concept StreamUniformReusable = requires(R& runtime, u32 size) {
    typename R::StreamUniformRef;
    requires std::is_trivially_copyable_v<typename R::StreamUniformRef>;
    { runtime.LastStreamUniformRef() } -> std::same_as<typename R::StreamUniformRef>;
    runtime.RebindStreamUniform(runtime.LastStreamUniformRef(), size);
    { runtime.CurrentTick() } -> std::convertible_to<u64>;
    { runtime.UniformReuse() } ->
        std::same_as<UniformStreamReuse<typename R::StreamUniformRef>&>;
};

/// EDEN_NX2_UBOREUSE=0 turns the reuse off (every small uniform buffer is copied, as before).
[[nodiscard]] inline bool UniformStreamReuseEnabled() noexcept {
    static const bool enabled = [] {
        const char* const value = std::getenv("EDEN_NX2_UBOREUSE");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

template <class Ref>
class UniformStreamReuse {
public:
    static constexpr u32 MaxBytes = 4096; // BufferCache's DEFAULT_SKIP_CACHE_SIZE
    static constexpr size_t NumStages = 5;
    static constexpr size_t NumSlots = 18; // Maxwell constant buffers per stage

    struct Slot {
        Ref ref{};
        u64 tick{};
        u32 size{}; // 0: nothing streamed yet
        std::array<u8, MaxBytes> bytes{};
    };

    void Reset() noexcept {
        for (auto& stage : slots) {
            for (Slot& slot : stage) {
                slot.size = 0;
                slot.tick = 0;
            }
        }
    }

    std::array<std::array<Slot, NumSlots>, NumStages> slots{};
    std::array<u8, MaxBytes> scratch{};
};

/// Forgets every slot. Call it when a buffer cache (and so its runtime, scheduler and stream
/// buffer) is created: ticks restart with a new scheduler and the old references are dead.
template <class Runtime>
inline void ResetUniformStreamReuse(Runtime& runtime) noexcept {
    if constexpr (StreamUniformReusable<Runtime>) {
        runtime.UniformReuse().Reset();
    }
}

/// The bytes are read by the caller on every bind, including cache hits. Never use a guest
/// write epoch here: native uniform writes are not all visible to the memory tracker.
template <class Runtime>
[[nodiscard]] bool BindUniformBytesReusing(Runtime& runtime, size_t stage, u32 index,
                                          u32 binding_index, std::span<const u8> bytes) {
    using Reuse = UniformStreamReuse<typename Runtime::StreamUniformRef>;
    if (bytes.empty() || bytes.size() > Reuse::MaxBytes || stage >= Reuse::NumStages ||
        index >= Reuse::NumSlots) {
        return false;
    }
    auto& slot = runtime.UniformReuse().slots[stage][index];
    const u32 size = static_cast<u32>(bytes.size());
    const bool same_bytes =
        slot.size == size && std::memcmp(slot.bytes.data(), bytes.data(), size) == 0;
    if (same_bytes && slot.tick == runtime.CurrentTick()) {
        runtime.RebindStreamUniform(slot.ref, size);
        return true;
    }
    const std::span<u8> span = runtime.BindMappedUniformBuffer(stage, binding_index, size);
    std::memcpy(span.data(), bytes.data(), size);
    slot.ref = runtime.LastStreamUniformRef();
    slot.tick = runtime.CurrentTick();
    if (!same_bytes) {
        slot.size = size;
        std::memcpy(slot.bytes.data(), bytes.data(), size);
    }
    return true;
}

template <class Runtime>
[[nodiscard]] bool BindPaddedUniformReusing(Runtime& runtime, size_t stage, u32 index,
                                           u32 binding_index, std::span<const u8> bytes, u32 size) {
    using Reuse = UniformStreamReuse<typename Runtime::StreamUniformRef>;
    if (size == 0 || size > Reuse::MaxBytes || stage >= Reuse::NumStages ||
        index >= Reuse::NumSlots) {
        return false;
    }
    if (bytes.size() >= size) {
        return BindUniformBytesReusing(runtime, stage, index, binding_index, bytes.first(size));
    }
    auto& scratch = runtime.UniformReuse().scratch;
    if (!bytes.empty()) {
        std::memcpy(scratch.data(), bytes.data(), bytes.size());
    }
    std::memset(scratch.data() + bytes.size(), 0, size - bytes.size());
    return BindUniformBytesReusing(runtime, stage, index, binding_index,
                                  std::span<const u8>{scratch.data(), size});
}

/// Binds the small uniform buffer at `device_addr` for (stage, cbuf `index`) through the stream
/// buffer, reusing the slot's previous allocation when the bytes and the command buffer are the
/// same. Returns false (nothing bound) when the buffer does not qualify, so the caller binds it
/// the classic way.
template <class Runtime, class DeviceMemory>
[[nodiscard]] bool BindSmallUniformReusing(Runtime& runtime, DeviceMemory& device_memory,
                                           size_t stage, u32 index, u32 binding_index,
                                           u64 device_addr, u32 size) {
    using Reuse = UniformStreamReuse<typename Runtime::StreamUniformRef>;
    if (size == 0 || size > Reuse::MaxBytes || stage >= Reuse::NumStages ||
        index >= Reuse::NumSlots) {
        return false;
    }
    Reuse& reuse = runtime.UniformReuse();
    u8* const scratch = reuse.scratch.data();
    device_memory.ReadBlockUnsafe(device_addr, scratch, size);

    return BindUniformBytesReusing(runtime, stage, index, binding_index,
                                  std::span<const u8>{scratch, size});
}

} // namespace VideoCommon
