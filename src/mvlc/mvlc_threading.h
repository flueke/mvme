#ifndef __MVLC_THREADING_H__
#define __MVLC_THREADING_H__

#include <cassert>
#include <mutex>
#include "mvlc/mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

using Mutex = std::mutex;
using LockGuard = std::lock_guard<Mutex>;
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

        UniqueLock lock(Pipe pipe) { return UniqueLock(mutex(pipe)); }

        UniqueLock lockCmd() { return lock(Pipe::Command); }
        UniqueLock lockData() { return lock(Pipe::Data); }

        LockPair lockBoth()
        {
            UniqueLock l1(cmdMutex(), std::defer_lock);
            UniqueLock l2(dataMutex(), std::defer_lock);
            std::lock(l1, l2);
            return std::make_pair(std::move(l1), std::move(l2));
        }

    private:
        Mutex m_cmdMutex;
        Mutex m_dataMutex;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_THREADING_H__ */
