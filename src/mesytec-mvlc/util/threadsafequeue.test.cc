#include "gtest/gtest.h"
#include "mesytec-mvlc/util/threadsafequeue.h"

using namespace mesytec::mvlc;

// Note: this does not test the actual thread-safety of the queue. It just does
// some basic functionality checks.

TEST(threadsafequeue, Basic)
{
    ThreadSafeQueue<int> queue;

    ASSERT_TRUE(queue.empty());
    ASSERT_EQ(queue.dequeue(), int{});

    queue.enqueue(42);

    ASSERT_FALSE(queue.empty());
    ASSERT_EQ(queue.size(), 1);

    queue.enqueue(21);

    ASSERT_FALSE(queue.empty());
    ASSERT_EQ(queue.size(), 2);

    ASSERT_EQ(queue.dequeue(), 42);
    ASSERT_EQ(queue.size(), 1);
    ASSERT_EQ(queue.dequeue(), 21);
    ASSERT_EQ(queue.size(), 0);
    ASSERT_TRUE(queue.empty());
}
