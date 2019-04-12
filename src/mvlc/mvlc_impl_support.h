#ifndef __MVME_MVLC_IMPL_SUPPORT_H__
#define __MVME_MVLC_IMPL_SUPPORT_H__

#include <array>
#include "typedefs.h"

namespace mesytec
{
namespace mvlc
{

template<size_t Capacity>
struct ReadBuffer
{
    std::array<u8, Capacity> data;
    u8 *first;
    u8 *last;

    ReadBuffer() { clear(); }
    size_t size() const { return last - first; }
    size_t free() const { return Capacity - size(); }
    size_t capacity() const { return Capacity; }
    void clear() { first = last = data.data(); }
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_IMPL_SUPPORT_H__ */
