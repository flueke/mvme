#ifndef __MVLC_THREADING_H__
#define __MVLC_THREADING_H__

#include <cassert>
#include <condition_variable>
#include <mutex>
#include "util/ticketmutex.h"
#include "mvlc/mvlc_constants.h"

namespace mesytec
{
namespace mvlc
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

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_THREADING_H__ */
