#include <gtest/gtest.h>
#include <thread>

#include "containers/blocked_bounded_queue.hpp"
#include "protocol/md_message.hpp"

TEST(BlockedBoundedQueueTest, Init)
{
    containers::BlockedBoundedQueue<protocol::MarketDataMsg> bbq;
    EXPECT_EQ(bbq.size(), 0);
    EXPECT_EQ(bbq.capacity(), 3000);
    EXPECT_FALSE(bbq.isFull());
    EXPECT_TRUE(bbq.isEmpty());
}

TEST(BlockedBoundedQueueTest, Add)
{
    containers::BlockedBoundedQueue<protocol::MarketDataMsg> bbq;
    protocol::MarketDataMsg msg{1, 23, 100, 101, 200, 300};
    EXPECT_TRUE(bbq.enqueue(msg));
    EXPECT_EQ(bbq.size(), 1);
    EXPECT_EQ(bbq.capacity(), 2999);
    EXPECT_FALSE(bbq.isEmpty());
}

TEST(BlockedBoundedQueueTest, Delete)
{
    containers::BlockedBoundedQueue<protocol::MarketDataMsg> bbq;
    protocol::MarketDataMsg msg{1, 23, 100, 101, 200, 300};
    EXPECT_TRUE(bbq.enqueue(msg));
    protocol::MarketDataMsg out;
    EXPECT_TRUE(bbq.dequeue(out));
    EXPECT_EQ(bbq.size(), 0);
    EXPECT_EQ(bbq.capacity(), 3000);
    EXPECT_FALSE(bbq.isFull());
    EXPECT_TRUE(bbq.isEmpty());
    EXPECT_EQ(msg.symbol_id, out.symbol_id);
    EXPECT_EQ(msg.exchange_ts, out.exchange_ts);
    EXPECT_EQ(msg.bid_price, out.bid_price);
    EXPECT_EQ(msg.ask_price, out.ask_price);
    EXPECT_EQ(msg.bid_size, out.bid_size);
    EXPECT_EQ(msg.ask_size, out.ask_size);
}

TEST(BlockedBoundedQueueTest, Clear)
{
    containers::BlockedBoundedQueue<protocol::MarketDataMsg> bbq;
    protocol::MarketDataMsg msg{1, 23, 100, 101, 200, 300};
    bbq.enqueue(msg);
    bbq.clear();
    EXPECT_TRUE(bbq.isEmpty());
    EXPECT_EQ(bbq.size(), 0);
    EXPECT_EQ(bbq.capacity(), 3000);
}

TEST(BlockedBoundedQueueTest, CloseSetsFlagAndRejectsEnqueue)
{
    containers::BlockedBoundedQueue<int> q;
    EXPECT_FALSE(q.closed());
    EXPECT_TRUE(q.enqueue(1));
    q.close();
    EXPECT_TRUE(q.closed());
    EXPECT_FALSE(q.enqueue(2)); // should refuse after close
}

TEST(BlockedBoundedQueueTest, CloseWakeDequeue)
{
    containers::BlockedBoundedQueue<int> q;
    std::atomic<bool> woke{false};

    std::thread t([&]
                  {
        int value;
        bool ok = q.dequeue(value);
        EXPECT_FALSE(ok);
        woke = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    q.close();
    t.join();
    EXPECT_TRUE(woke);
}

TEST(BlockedBoundedQueueTest, ProducerBlock)
{
    containers::BlockedBoundedQueue<int> q;
    std::atomic<bool> woke{false};

    for (size_t i = 0; i < 3000; ++i)
    {
        EXPECT_TRUE(q.enqueue(static_cast<int>(i)));
    }

    std::thread p([&]
                  {
        int value{9};
        bool ok = q.enqueue(value);
        EXPECT_FALSE(ok); 
        woke = true; });

    q.close();
    p.join();
    EXPECT_TRUE(woke);
}
