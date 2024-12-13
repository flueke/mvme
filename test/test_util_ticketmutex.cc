#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "gtest/gtest.h"
#include "util/ticketmutex.h"

using namespace mesytec::mvme;

static const auto TestDuration = std::chrono::seconds(1);
static const auto ReportInterval = std::chrono::seconds(1);
static const unsigned ThreadCount = 8;

TEST(util_ticketmutex, SingleMutex)
{
    TicketMutex mutex;
    std::atomic<bool> guardCheck(0);
    std::atomic<size_t> waiters(0);
    std::atomic<bool> keepRunning(true);
    std::vector<size_t> obtainedLocks(ThreadCount);

    auto worker_func = [&mutex, &guardCheck, &waiters, &keepRunning, &obtainedLocks] (unsigned threadId)
    {
        while (keepRunning)
        {
            ++waiters;
            std::unique_lock<TicketMutex> guard(mutex);
            --waiters;

            bool b = guardCheck.exchange(true);
            ASSERT_EQ(b, false);

            ++obtainedLocks[threadId];
            //std::this_thread::sleep_for(std::chrono::milliseconds(1));

            b = guardCheck.exchange(false);
            ASSERT_EQ(b, true);
        }
    };

    auto report_func = [&waiters, &keepRunning] ()
    {
        while (keepRunning)
        {
            std::cerr << "concurrent waiters: " << waiters << std::endl;
            std::this_thread::sleep_for(ReportInterval);
        }
    };

    std::thread report_thread(report_func);

    std::vector<std::thread> threads;

    for (unsigned ti = 0; ti < ThreadCount; ti++)
    {
        threads.emplace_back(std::move(std::thread(worker_func, ti)));
    }

    std::cerr << "Started " << ThreadCount << " workers and one reporting thread" << std::endl;

    std::this_thread::sleep_for(TestDuration);

    std::cerr << "Waiting for threads to finish..." << std::endl;
    keepRunning = false;

    for (unsigned ti = 0; ti < ThreadCount; ti++)
    {
        threads[ti].join();
        assert(!threads[ti].joinable());
    }

    report_thread.join();

    std::cerr << "Threads finished" << std::endl;

    for (unsigned ti = 0; ti < ThreadCount; ti++)
    {
        std::cerr << "thread " << ti << " got " << obtainedLocks[ti] << " locks" << std::endl;
    }
}

TEST(util_ticketmutex, MutexPair)
{
    using MutexPair = std::pair<TicketMutex, TicketMutex>;
    using UniqueLock = std::unique_lock<TicketMutex>;
    using LockPair = std::pair<UniqueLock, UniqueLock>;

    auto lock_both = [] (MutexPair &mp) -> LockPair
    {
        UniqueLock l1(mp.first);
        UniqueLock l2(mp.second);

        return std::make_pair(std::move(l1), std::move(l2));
    };

    MutexPair mutexes;
    std::atomic<bool> guardCheck(0);
    std::atomic<size_t> waiters(0);
    std::atomic<bool> keepRunning(true);
    std::vector<size_t> obtainedLocks(ThreadCount);

    auto worker_func = [&mutexes, &guardCheck, &waiters, &keepRunning,
         &obtainedLocks, &lock_both] (unsigned threadId)
    {
        while (keepRunning)
        {
            ++waiters;
            auto guard = lock_both(mutexes);
            --waiters;

            bool b = guardCheck.exchange(true);
            ASSERT_EQ(b, false);

            ++obtainedLocks[threadId];
            //std::this_thread::sleep_for(std::chrono::milliseconds(1));

            b = guardCheck.exchange(false);
            ASSERT_EQ(b, true);
        }
    };

    auto report_func = [&waiters, &keepRunning] ()
    {
        while (keepRunning)
        {
            std::cerr << "concurrent waiters: " << waiters << std::endl;
            std::this_thread::sleep_for(ReportInterval);
        }
    };

    std::thread report_thread(report_func);

    std::vector<std::thread> threads;

    for (unsigned ti = 0; ti < ThreadCount; ti++)
    {
        threads.emplace_back(std::move(std::thread(worker_func, ti)));
    }

    std::cerr << "Started " << ThreadCount << " workers and one reporting thread" << std::endl;

    std::this_thread::sleep_for(TestDuration);

    std::cerr << "Waiting for threads to finish..." << std::endl;
    keepRunning = false;

    for (unsigned ti = 0; ti < ThreadCount; ti++)
    {
        threads[ti].join();
        assert(!threads[ti].joinable());
    }

    report_thread.join();

    std::cerr << "Threads finished" << std::endl;

    for (unsigned ti = 0; ti < ThreadCount; ti++)
    {
        std::cerr << "thread " << ti << " got " << obtainedLocks[ti] << " locks" << std::endl;
    }
}
