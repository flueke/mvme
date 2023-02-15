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
namespace mvme_mvlc
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

} // end namespace mvme_mvlc
} // end namespace mesytec

namespace std
{
    template<> struct hash<mesytec::mvme_mvlc::StackErrorInfo>
    {
        std::size_t operator()(const mesytec::mvme_mvlc::StackErrorInfo &ei) const noexcept
        {
            auto h1 = std::hash<u16>{}(ei.line);
            auto h2 = std::hash<u8>{}(ei.flags);
            return h1 ^ (h2 << 1);
        }
    };
} // end namespace std

namespace mesytec
{
namespace mvme_mvlc
{

// Records the number of errors for each distinct combination of
// (error_line, error_flags).
using ErrorInfoCounts = std::unordered_map<StackErrorInfo, size_t>;

struct StackErrorCounters
{
    std::array<ErrorInfoCounts, stacks::StackCount> stackErrors;
    size_t nonErrorFrames;
    std::unordered_map<u32, size_t> nonErrorHeaderCounts; // headerValue -> count
    QVector<QVector<u32>> framesCopies;
};

struct GuardedStackErrorCounters
{
    mutable Mutex guardMutex;
    StackErrorCounters counters = {};

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

// This function expects C to be a container of u32 values holding a single
// mvlc stack error frame.
template<typename C>
void update_stack_error_counters(StackErrorCounters &counters, const C &errorFrame)
{
    assert(errorFrame.size() > 0);

    bool isErrorFrame = false;
    FrameInfo frameInfo = {};

    // Error frames consist of the frame header and a second word containing
    // the stack number and the stack line number where the error occured.
    if (errorFrame.size() == 2)
    {
        frameInfo = extract_frame_info(errorFrame[0]);

        if (frameInfo.type == frame_headers::StackError
            && frameInfo.stack < stacks::StackCount)
        {
            isErrorFrame = true;
        }
    }

    if (isErrorFrame)
    {
        assert(errorFrame.size() == 2);
        u16 stackLine = errorFrame[1] & stack_error_info::StackLineMask;
        StackErrorInfo ei = { stackLine, frameInfo.flags };
        ++counters.stackErrors[frameInfo.stack][ei];
    }
    else if (errorFrame.size() > 0)
    {
        ++counters.nonErrorFrames;
        ++counters.nonErrorHeaderCounts[errorFrame[0]];
        QVector<u32> frameCopy;
        std::copy(errorFrame.begin(), errorFrame.end(), std::back_inserter(frameCopy));
        counters.framesCopies.push_back(frameCopy);
    }
}

} // end namespace mvme_mvlc
} // end namespace mesytec



#endif /* __MVLC_STACK_ERRORS_H__ */
