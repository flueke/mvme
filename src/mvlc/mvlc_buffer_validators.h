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
#ifndef __MVLC_UTIL_CORE_H__
#define __MVLC_UTIL_CORE_H__

#include <functional>
#include "mvlc/mvlc_constants.h"

namespace mesytec
{
namespace mvme_mvlc
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

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVLC_UTIL_CORE_H__ */
