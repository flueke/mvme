/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_MVLC_UTIL_H__
#define __MVME_MVLC_UTIL_H__

#include <iomanip>
#include <vector>

#include "mesytec-mvlc_export.h"
#include "mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

struct FrameInfo
{
    u16 len;
    u8 type;
    u8 flags;
    u8 stack;
    u8 ctrl;
};

inline FrameInfo extract_frame_info(u32 header)
{
    using namespace frame_headers;

    FrameInfo result;

    result.len   = (header >> LengthShift) & LengthMask;
    result.type  = (header >> TypeShift) & TypeMask;
    result.flags = (header >> FrameFlagsShift) & FrameFlagsMask;
    result.stack = (header >> StackNumShift) & StackNumMask;
    result.ctrl  = (header >> CtrlIdShift) & CtrlIdMask;

    return result;
}

MESYTEC_MVLC_EXPORT std::string decode_frame_header(u32 header);
MESYTEC_MVLC_EXPORT std::string format_frame_flags(u8 frameFlags);

inline bool has_error_flag_set(u8 frameFlags)
{
    return (frameFlags & frame_flags::AllErrorFlags) != 0u;
}

MESYTEC_MVLC_EXPORT void log_buffer(const u32 *buffer, size_t size, const std::string &info = {});
MESYTEC_MVLC_EXPORT void log_buffer(const std::vector<u32> &buffer, const std::string &info = {});

template<typename Out>
void log_buffer(Out &out, const u32 *buffer, size_t size, const char *info)
{
    using std::endl;

    out << "begin " << info << " (size=" << size << ")" << endl;

    for (size_t i=0; i < size; i++)
    {
        out << "  0x"
            << std::setfill('0') << std::setw(8) << std::hex
            << buffer[i]
            << std::dec << std::setw(0)
            << endl
            ;
    }

    out << "end " << info << endl;
}

MESYTEC_MVLC_EXPORT const char *get_system_event_subtype_name(u8 subtype);
MESYTEC_MVLC_EXPORT const char *get_frame_flag_shift_name(u8 flag);


stacks::TimerBaseUnit MESYTEC_MVLC_EXPORT timer_base_unit_from_string(const std::string &str);

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_UTIL_H__ */
