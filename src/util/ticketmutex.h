/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __MVME_UTIL_TICKETMUTEX_H__
#define __MVME_UTIL_TICKETMUTEX_H__

#include <mutex>
#include <condition_variable>
#include "typedefs.h"

namespace mesytec
{
namespace mvme
{

// Ticket mutex system based on
// https://stackoverflow.com/questions/6449732/fair-critical-section-linux/6453925#6453925
//
// Note: when using multiple TicketLocks and you want to lock all of them while
// at least one is under heavy contention then it is a bad idea to use
// std::lock which will cause frequent locking and unlocking until all locks
// are acquired. This does not work well with the ticket system.
// Instead write code that locks all of your mutexes in the same order in all
// execution paths.

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
        using TicketTypeStore = TicketType;

        std::condition_variable m_cond;
        std::mutex m_mutex;
        TicketTypeStore m_queue_head; // current ticket number
        TicketTypeStore m_queue_tail; // next ticket number to take
};

}
}

#endif /* __MVME_UTIL_TICKETMUTEX_H__ */
