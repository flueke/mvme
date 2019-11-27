#ifndef __MVLC_STACK_ERRORS_H__
#define __MVLC_STACK_ERRORS_H__

#include <unordered_map>
#include "mvlc/mvlc_constants.h"
#include "mvlc/mvlc_threading.h"
#include "mvlc/mvlc_util.h"

// Utilities to deal with stack error notification frames (0xF7) produced by the
// MVLC.

namespace mesytec
{
namespace mvlc
{

struct StackErrorInfo
{
    u16 line; // the number of the stack line that caused the error
    u8 flags; // frame_flags from mvlc_constants
};

inline bool operator==(const StackErrorInfo &a, const StackErrorInfo &b)
{
    return a.line == b.line && a.flags == b.flags;
}

} // end namespace mvlc
} // end namespace mesytec

namespace std
{
    template<> struct hash<mesytec::mvlc::StackErrorInfo>
    {
        std::size_t operator()(const mesytec::mvlc::StackErrorInfo &ei) const noexcept
        {
            auto h1 = std::hash<u16>{}(ei.line);
            auto h2 = std::hash<u8>{}(ei.flags);
            return h1 ^ (h2 << 1);
        }
    };
} // end namespace std

namespace mesytec
{
namespace mvlc
{

// Records the number of errors for each distinct combination of
// (error_line, error_flags).
using ErrorInfoCounts = std::unordered_map<StackErrorInfo, size_t>;

struct StackErrorCounters
{
    std::array<ErrorInfoCounts, stacks::StackCount> stackErrors;
    size_t nonErrorFrames;
};

struct GuardedStackErrorCounters
{
    mutable Mutex guardMutex;
    StackErrorCounters counters;

    inline UniqueLock lock() const
    {
        return UniqueLock(guardMutex);
    }
};

#if 0
// Extract frame error info from a given Container of u32 values.
template<typename C>
StackErrorInfo stack_error_info_from_buffer(const C &errorFrame)
{
    if (errorFrame.size() != 2)
        return {};

    auto frameInfo = extract_frame_info(errorFrame[0]);
    auto stackLine = errorFrame[1] & stack_error_info::StackLineMask;

    return { stackLine, frameInfo.flags };
}
#endif

// This function expect C to be a container of u32 values holding a sinlge mvlc
// stack error frame.
template<typename C>
void update_stack_error_counters(StackErrorCounters &counters, const C &errorFrame)
{
    if (errorFrame.size() != 2)
    {
        ++counters.nonErrorFrames;
        return;
    }

    auto frameInfo = extract_frame_info(errorFrame[0]);

    if (frameInfo.type == frame_headers::StackError
        && frameInfo.stack < stacks::StackCount)
    {
        u16 stackLine = errorFrame[1] & stack_error_info::StackLineMask;
        StackErrorInfo ei = { stackLine, frameInfo.flags };
        ++counters.stackErrors[frameInfo.stack][ei];
    }
    else
    {
        ++counters.nonErrorFrames;
    }
}

} // end namespace mvlc
} // end namespace mesytec



#endif /* __MVLC_STACK_ERRORS_H__ */
