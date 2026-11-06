// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace Vulkan {

template <typename Source>
auto MultiRangeOwners(std::span<const Source> sources) {
    std::vector<decltype(Source::handle)> owners;
    owners.reserve(sources.size());
    for (const auto& source : sources) {
        owners.push_back(source.handle);
    }
    return owners;
}

template <typename RetiredList, typename Scheduler>
void DrainRetiredMultiRangeBuffers(RetiredList& retired, Scheduler& scheduler) {
    std::size_t index = 0;
    while (index < retired.size()) {
        if (scheduler.IsFree(retired[index].tick)) {
            if (index + 1 != retired.size()) {
                retired[index] = std::move(retired.back());
            }
            retired.pop_back();
        } else {
            ++index;
        }
    }
}

template <typename RetiredList, typename Scheduler, typename Entry>
void RetireMultiRangeBuffer(RetiredList& retired, Scheduler& scheduler, Entry& entry) {
    if (!entry.sparse_handle && !entry.gathered) {
        return;
    }
    if (retired.size() == retired.capacity()) {
        DrainRetiredMultiRangeBuffers(retired, scheduler);
    }
    if (retired.size() == retired.capacity()) {
        auto oldest = retired.front().tick;
        for (const auto& item : retired) {
            oldest = std::min(oldest, item.tick);
        }
        scheduler.Wait(oldest);
        DrainRetiredMultiRangeBuffers(retired, scheduler);
    }
    retired.push_back(typename RetiredList::value_type{
        .handle = std::move(entry.sparse_handle),
        .gathered = std::move(entry.gathered),
        .tick = scheduler.CurrentTick(),
    });
}

template <typename RetiredList, typename Scheduler>
void TickMultiRangeBuffers(RetiredList& retired, Scheduler& scheduler) {
    if (retired.empty()) {
        return;
    }
    // IsFree reads the cached timeline value; observe completion without waiting for it.
    scheduler.GetMasterSemaphore().Refresh();
    DrainRetiredMultiRangeBuffers(retired, scheduler);
}

template <typename Entries, typename Handle, typename Retire>
void DropMultiRangeOwner(Entries& entries, Handle owner, Retire retire) {
    if (owner == Handle{}) {
        return;
    }
    for (auto it = entries.begin(); it != entries.end();) {
        auto& entry = it->second;
        if (std::find(entry.owners.begin(), entry.owners.end(), owner) == entry.owners.end()) {
            ++it;
            continue;
        }
        retire(entry);
        it = entries.erase(it);
    }
}

} // namespace Vulkan
