#ifndef __MVME_A2_UTIL_THREADING_H__
#define __MVME_A2_UTIL_THREADING_H__

// Ticket mutex system based on
// https://stackoverflow.com/questions/6449732/fair-critical-section-linux/6453925#6453925

#include <cassert>
#include <condition_variable>
#include <mutex>
#include "util/typedefs.h"

namespace a2
{

class TicketMutex
{
    public:
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

        std::condition_variable m_cond;
        std::mutex m_mutex;
        TicketType m_queue_head = 0; // current ticket number
        TicketType m_queue_tail = 0; // next ticket number to take
};

using Mutex = TicketMutex;
using UniqueLock = std::unique_lock<Mutex>;

} // end namespace a2

#endif /* __MVME_A2_UTIL_THREADING_H__ */
