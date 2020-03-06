/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020 mesytec GmbH & Co. KG
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __MESYTEC_MVLC_MVLC_THREADING_H__
#define __MESYTEC_MVLC_MVLC_THREADING_H__

#include <cassert>
#include <mutex>
#include "util/ticketmutex.h"
#include "mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

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

#endif /* __MESYTEC_MVLC_MVLC_THREADING_H__ */
