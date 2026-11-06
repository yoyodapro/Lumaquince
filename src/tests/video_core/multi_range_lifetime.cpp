// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <map>
#include <memory>
#include <vector>

#include <boost/container/static_vector.hpp>
#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/multi_range_lifetime.h"

namespace {

struct Source {
    u32 handle;
    std::array<u32, 2> words;
};

using Resource = std::shared_ptr<std::vector<u32>>;

struct Entry {
    Resource gathered;
    Resource sparse_handle;
    std::vector<u32> owners;
};

struct Retired {
    Resource handle;
    Resource gathered;
    u64 tick;
};

struct Scheduler {
    u64 current{1};
    u64 completed{};
    u64 observed{};
    u32 refreshes{};
    std::vector<u64> waits;

    u64 CurrentTick() const {
        return current;
    }

    bool IsFree(u64 tick) const {
        return tick <= observed;
    }

    Scheduler& GetMasterSemaphore() {
        return *this;
    }

    void Refresh() {
        observed = completed;
        ++refreshes;
    }

    void Wait(u64 tick) {
        waits.push_back(tick);
        completed = tick;
        observed = tick;
        if (current <= tick) {
            current = tick + 1;
        }
    }
};

// Resource creation is stubbed; dependency eviction and retirement use the production helpers.
struct Cache {
    Scheduler scheduler;
    std::map<u64, Entry> entries;
    boost::container::static_vector<Retired, 2> retired;

    Entry& Create(u64 key, std::span<const Source> sources, bool sparse = false) {
        Entry entry;
        entry.owners = Vulkan::MultiRangeOwners(sources);
        auto& resource = sparse ? entry.sparse_handle : entry.gathered;
        resource = std::make_shared<std::vector<u32>>();
        for (const auto& source : sources) {
            resource->insert(resource->end(), source.words.begin(), source.words.end());
        }
        return entries.emplace(key, std::move(entry)).first->second;
    }

    void DropOwner(u32 owner) {
        Vulkan::DropMultiRangeOwner(entries, owner, [&](Entry& entry) {
            Vulkan::RetireMultiRangeBuffer(retired, scheduler, entry);
        });
    }

    void TickFrame() {
        Vulkan::TickMultiRangeBuffers(retired, scheduler);
    }
};

} // namespace

TEST_CASE("Gathered dependencies retire on deletion before source handle reuse",
          "[video_core][multi_range_lifetime]") {
    Cache cache;
    const std::array sources{Source{11, {1, 2}}, Source{22, {3, 4}}};
    auto& original = cache.Create(123, sources);
    REQUIRE_FALSE(original.sparse_handle);
    REQUIRE(*original.gathered == std::vector<u32>{1, 2, 3, 4});
    const std::weak_ptr old_resource = original.gathered;

    // Later draws may use the same entry, so retirement must use the deletion tick, not creation.
    cache.scheduler.current = 7;
    cache.DropOwner(11);
    REQUIRE(cache.entries.empty());
    REQUIRE(cache.retired.size() == 1);
    REQUIRE(cache.retired.front().tick == 7);
    REQUIRE_FALSE(old_resource.expired());

    // Same key, raw handles, sizes and write tick can now refer to unrelated source contents.
    const std::array reused{Source{11, {5, 6}}, Source{22, {7, 8}}};
    auto& replacement = cache.Create(123, reused);
    REQUIRE(*replacement.gathered == std::vector<u32>{5, 6, 7, 8});
    REQUIRE(replacement.gathered != old_resource.lock());
    cache.scheduler.completed = 6;
    cache.TickFrame();
    REQUIRE_FALSE(old_resource.expired());
    cache.scheduler.completed = 7;
    cache.TickFrame();
    REQUIRE(old_resource.expired());
    REQUIRE(cache.retired.empty());
    REQUIRE(cache.entries.size() == 1);
    REQUIRE(cache.scheduler.waits.empty());
}

TEST_CASE("Gathered resource churn returns to baseline without further cache lookups",
          "[video_core][multi_range_lifetime]") {
    Cache cache;
    for (u32 iteration = 1; iteration <= 128; ++iteration) {
        cache.scheduler.current = iteration;
        const std::array sources{Source{11, {iteration, iteration + 1}}};
        auto& entry = cache.Create(iteration, sources);
        const std::weak_ptr resource = entry.gathered;
        REQUIRE(*entry.gathered == std::vector<u32>{iteration, iteration + 1});
        cache.DropOwner(11);
        REQUIRE(cache.entries.empty());
        REQUIRE(cache.retired.size() == 1);
        REQUIRE_FALSE(resource.expired());
        cache.scheduler.completed = iteration;
        cache.TickFrame();
        REQUIRE(resource.expired());
        REQUIRE(cache.retired.empty());
    }
    REQUIRE(cache.scheduler.waits.empty());
}

TEST_CASE("Owner eviction preserves unrelated entries and retires duplicate owners once",
          "[video_core][multi_range_lifetime]") {
    Cache cache;
    const std::array repeated{Source{11, {1, 2}}, Source{11, {3, 4}}};
    const std::array unrelated{Source{22, {5, 6}}};
    cache.Create(1, repeated);
    cache.Create(2, unrelated, true);
    cache.DropOwner(0);
    cache.DropOwner(33);
    REQUIRE(cache.entries.size() == 2);
    REQUIRE(cache.retired.empty());
    cache.DropOwner(11);
    REQUIRE(cache.entries.size() == 1);
    REQUIRE(cache.entries.contains(2));
    REQUIRE(cache.retired.size() == 1);
    cache.DropOwner(11);
    REQUIRE(cache.retired.size() == 1);
    cache.DropOwner(22);
    REQUIRE(cache.entries.empty());
    REQUIRE(cache.retired.size() == 2);
    REQUIRE(cache.retired[0].gathered);
    REQUIRE(cache.retired[1].handle);
}

TEST_CASE("Retirement pressure waits for the oldest tick and retains in-flight resources",
          "[video_core][multi_range_lifetime]") {
    Cache cache;
    std::array<std::weak_ptr<std::vector<u32>>, 3> resources;
    for (u32 i = 0; i < 3; ++i) {
        cache.scheduler.current = 10 + i;
        const std::array sources{Source{11, {i, i}}};
        resources[i] = cache.Create(i, sources).gathered;
        cache.DropOwner(11);
    }
    REQUIRE(cache.scheduler.waits == std::vector<u64>{10});
    REQUIRE(resources[0].expired());
    REQUIRE_FALSE(resources[1].expired());
    REQUIRE_FALSE(resources[2].expired());
    REQUIRE(cache.retired.size() == 2);
    cache.scheduler.completed = 12;
    cache.TickFrame();
    REQUIRE(resources[1].expired());
    REQUIRE(resources[2].expired());
    REQUIRE(cache.retired.empty());
}

TEST_CASE("Frame retirement observes timeline completion only while resources are pending",
          "[video_core][multi_range_lifetime]") {
    Cache cache;
    cache.TickFrame();
    REQUIRE(cache.scheduler.refreshes == 0);
    const std::array sources{Source{11, {1, 2}}};
    const std::weak_ptr resource = cache.Create(1, sources).gathered;
    cache.DropOwner(11);
    cache.TickFrame();
    REQUIRE(cache.scheduler.refreshes == 1);
    REQUIRE_FALSE(resource.expired());

    cache.scheduler.completed = cache.scheduler.current;
    REQUIRE_FALSE(cache.scheduler.IsFree(cache.scheduler.current));
    cache.TickFrame();
    REQUIRE(cache.scheduler.refreshes == 2);
    REQUIRE(cache.scheduler.IsFree(cache.scheduler.current));
    REQUIRE(resource.expired());
    REQUIRE(cache.retired.empty());
    REQUIRE(cache.scheduler.waits.empty());

    cache.TickFrame();
    REQUIRE(cache.scheduler.refreshes == 2);
}
