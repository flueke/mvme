#ifndef __MVLC_UTIL_CORE_H__
#define __MVLC_UTIL_CORE_H__

#include <functional>
#include "mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

using BufferHeaderValidator = std::function<bool (u32 header)>;

inline bool is_super_buffer(u32 header)
{
    return get_frame_type(header) == frame_headers::SuperFrame;
}

inline bool is_stack_buffer(u32 header)
{
    return get_frame_type(header) == frame_headers::StackFrame;
}

inline bool is_blockread_buffer(u32 header)
{
    return get_frame_type(header) == frame_headers::BlockRead;
}

inline bool is_stackerror_notification(u32 header)
{
    return get_frame_type(header) == frame_headers::StackError;
}

inline bool is_stack_buffer_continuation(u32 header)
{
    return get_frame_type(header) == frame_headers::StackContinuation;
}

inline bool is_system_event(u32 header)
{
    return get_frame_type(header) == frame_headers::SystemEvent;
}

inline bool is_known_frame_header(u32 header)
{
    const u8 type = get_frame_type(header);

    return (type == frame_headers::SuperFrame
            || type == frame_headers::StackFrame
            || type == frame_headers::BlockRead
            || type == frame_headers::StackError
            || type == frame_headers::StackContinuation
            || type == frame_headers::SystemEvent
            );
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_UTIL_CORE_H__ */
