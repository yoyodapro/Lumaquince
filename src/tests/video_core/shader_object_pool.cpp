// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <memory>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/object_pool.h"

namespace {

struct TrackedObject {
    int& live;
    int& destroyed;
    std::unique_ptr<int> value;

    TrackedObject(int& live_, int& destroyed_, int value_)
        : live{live_}, destroyed{destroyed_}, value{std::make_unique<int>(value_)} {
        ++live;
    }

    ~TrackedObject() {
        --live;
        ++destroyed;
    }
};

} // namespace

TEST_CASE("Shader object pools reuse an exactly full chunk", "[video_core][shader][pool]") {
    int live{};
    int destroyed{};
    Shader::ObjectPool<TrackedObject> pool{4};
    std::array<TrackedObject*, 4> first;
    for (int index = 0; index < 4; ++index) {
        first[index] = pool.Create(live, destroyed, index);
    }
    pool.ReleaseContents();
    REQUIRE(live == 0);
    REQUIRE(destroyed == 4);
    for (int index = 0; index < 4; ++index) {
        const auto object = pool.Create(live, destroyed, index + 10);
        REQUIRE(object == first[index]);
        REQUIRE(*object->value == index + 10);
    }
    pool.ReleaseContents();
    REQUIRE(live == 0);
    REQUIRE(destroyed == 8);
    pool.ReleaseContents();
    REQUIRE(destroyed == 8);
}

TEST_CASE("Shader object pools destroy every object across growth and reuse",
          "[video_core][shader][pool]") {
    int live{};
    int destroyed{};
    {
        Shader::ObjectPool<TrackedObject> pool{2};
        std::array<TrackedObject*, 7> objects;
        for (int index = 0; index < 7; ++index) {
            objects[index] = pool.Create(live, destroyed, index);
        }
        REQUIRE(live == 7);
        REQUIRE(destroyed == 0);
        for (int index = 0; index < 7; ++index) {
            REQUIRE(*objects[index]->value == index);
        }
        pool.ReleaseContents();
        REQUIRE(live == 0);
        REQUIRE(destroyed == 7);
        for (int index = 0; index < 8; ++index) {
            (void)pool.Create(live, destroyed, index);
        }
        pool.ReleaseContents();
        REQUIRE(live == 0);
        REQUIRE(destroyed == 15);
        (void)pool.Create(live, destroyed, 99);
        REQUIRE(live == 1);
    }
    REQUIRE(live == 0);
    REQUIRE(destroyed == 16);
}
