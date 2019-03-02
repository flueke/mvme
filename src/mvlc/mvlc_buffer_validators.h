#ifndef __MVLC_UTIL_CORE_H__
#define __MVLC_UTIL_CORE_H__

#include <functional>
#include "mvlc/mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

using BufferHeaderValidator = std::function<bool (u32 header)>;

// BufferHeaderValidators

inline bool is_super_buffer(u32 header)
{
    return (header >> buffer_types::TypeShift) == buffer_types::SuperBuffer;
}

inline bool is_stack_buffer(u32 header)
{
    return (header >> buffer_types::TypeShift) == buffer_types::StackBuffer;
}

inline bool is_blockread_buffer(u32 header)
{
    return (header >> buffer_types::TypeShift) == buffer_types::BlockRead;
}

inline bool is_stackerror_notification(u32 header)
{
    return (header >> buffer_types::TypeShift) == buffer_types::StackError;
}

inline bool is_known_buffer(u32 header)
{
    const u8 type = (header >> buffer_types::TypeShift);

    return (type == buffer_types::SuperBuffer
            || type == buffer_types::StackBuffer
            || type == buffer_types::BlockRead
            || type == buffer_types::StackError);
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_UTIL_CORE_H__ */
