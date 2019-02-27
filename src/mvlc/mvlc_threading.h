#ifndef __MVLC_THREADING_H__
#define __MVLC_THREADING_H__

#include <mutex>

namespace mesytec
{
namespace mvlc
{

using Mutex = std::mutex;
using LockGuard = std::lock_guard<Mutex>;
using UniqueLock = std::unique_lock<Mutex>;

#if 0
// This was in MVLCObject but it does not seem to be the right place.
// What's needed is a lock on the command pipe while a dialog transaction is in
// progress so that
        Mutex &cmdMutex() { return m_cmdMutex; }
        Mutex &dataMutex() { return m_dataMutex; }
        Mutex &getMutex(Pipe pipe)
        {
            return pipe == Pipe::Data ? dataMutex() : cmdMutex();
        }
        mutable Mutex m_cmdMutex;
        mutable Mutex m_dataMutex;

// This works because moving std::pair moves both of it's objects and
// std::unique_lock is movable.
using LockPair = std::pair<MVLCObject::UniqueLock, MVLCObject::UniqueLock>;

LockPair lock_both(MVLCObject::Mutex &m1, MVLCObject::Mutex &m2)
{
    MVLCObject::UniqueLock l1(m1, std::defer_lock);
    MVLCObject::UniqueLock l2(m2, std::defer_lock);
    std::lock(l1, l2);
    return std::make_pair(std::move(l1), std::move(l2));
}
#endif

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_THREADING_H__ */
