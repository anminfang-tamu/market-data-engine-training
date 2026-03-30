#include <array>

#include <gtest/gtest.h>

#include "containers/frame_pool.hpp"
#include "containers/ring_buffer.hpp"

TEST(RingBufferTest, UsesFullCapacity)
{
    containers::RingBuffer<int, 8> ring;

    for (int i = 0; i < 8; ++i)
    {
        EXPECT_TRUE(ring.push(i));
    }
    EXPECT_TRUE(ring.full());
    EXPECT_FALSE(ring.push(8));

    for (int i = 0; i < 8; ++i)
    {
        int value = -1;
        ASSERT_TRUE(ring.pop(value));
        EXPECT_EQ(value, i);
    }
    EXPECT_TRUE(ring.empty());
}

TEST(FramePoolTest, InitResetsQueuesAndRepopulatesAllSlots)
{
    containers::FramePool<8> pool;
    pool.init();

    size_t idx = 0;
    ASSERT_TRUE(pool.free.pop(idx));
    ASSERT_TRUE(pool.ready.push(idx));

    pool.init();

    EXPECT_FALSE(pool.ready.pop(idx));

    std::array<bool, 8> seen{};
    for (size_t i = 0; i < seen.size(); ++i)
    {
        ASSERT_TRUE(pool.free.pop(idx));
        ASSERT_LT(idx, seen.size());
        EXPECT_FALSE(seen[idx]);
        seen[idx] = true;
    }

    EXPECT_FALSE(pool.free.pop(idx));
}
