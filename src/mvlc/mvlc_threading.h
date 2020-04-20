/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVLC_THREADING_H__
#define __MVLC_THREADING_H__

#include <cassert>
#include <mutex>
#include "util/ticketmutex.h"
#include "mvlc/mvlc_constants.h"

namespace mesytec
{
namespace mvme_mvlc
{

using Mutex = mvme::TicketMutex;
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

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVLC_THREADING_H__ */
