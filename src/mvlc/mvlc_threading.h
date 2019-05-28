#ifndef __MVLC_THREADING_H__
#define __MVLC_THREADING_H__

#include <cassert>
#include <condition_variable>
#include <mutex>
#include "mvlc/mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

// Ticket mutex system based on
// https://stackoverflow.com/questions/6449732/fair-critical-section-linux/6453925#6453925

class TicketMutex
{
    public:
        TicketMutex() : m_queue_head(0) , m_queue_tail(0) {}

        TicketMutex(const TicketMutex &) = delete;
        TicketMutex &operator=(const TicketMutex &) = delete;

        void lock()
        {
            UniqueLock lock(m_mutex);
            TicketType my_ticket_number = m_queue_tail++;

            while (my_ticket_number != m_queue_head)
                m_cond.wait(lock);
        }

        void unlock()
        {
            {
                UniqueLock lock(m_mutex);
                m_queue_head++;
            }
            m_cond.notify_all();
        }

        bool try_lock()
        {
            UniqueLock lock(m_mutex, std::try_to_lock);

            if (lock)
            {
                if (m_queue_head == m_queue_tail)
                {
                    m_queue_tail++;
                    return true;
                }
            }

            return false;
        }

    private:
        using UniqueLock = std::unique_lock<std::mutex>;
        using TicketType = u16;
        //using TicketTypeStore = std::atomic<TicketType>;
        using TicketTypeStore = TicketType;

        std::condition_variable m_cond;
        std::mutex m_mutex;
        TicketTypeStore m_queue_head; // current ticket number
        TicketTypeStore m_queue_tail; // next ticket number to take
};

using Mutex = TicketMutex;
using UniqueLock = std::unique_lock<Mutex>;
using LockPair = std::pair<UniqueLock, UniqueLock>;

class Locks
{
    public:
        Mutex &cmdMutex() { return m_cmdMutex; }
        Mutex &dataMutex() { return m_dataMutex; }

        Mutex &mutex(Pipe pipe)
        {
            return pipe == Pipe::Data ? dataMutex() : cmdMutex();
        }

        UniqueLock lock(Pipe pipe)
        {
            return UniqueLock(mutex(pipe));
        }

        UniqueLock lockCmd() { return lock(Pipe::Command); }
        UniqueLock lockData() { return lock(Pipe::Data); }

        LockPair lockBoth()
        {
            // Note: since switching from std::mutex to the TicketMutex as the
            // mutex type this code should not use std::lock() anymore. The
            // reason is that if one of the mutexes is locked most of the time,
            // e.g. by the readout loop, std::lock() will create high CPU load.
#if 0
            UniqueLock l1(cmdMutex(), std::defer_lock);
            UniqueLock l2(dataMutex(), std::defer_lock);
            std::lock(l1, l2);
#else
            UniqueLock l1(cmdMutex());
            UniqueLock l2(dataMutex());
#endif
            return std::make_pair(std::move(l1), std::move(l2));
        }

    private:
        Mutex m_cmdMutex;
        Mutex m_dataMutex;
};


} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_THREADING_H__ */
