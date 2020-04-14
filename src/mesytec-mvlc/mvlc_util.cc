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
#include "mvlc_util.h"

#include <cassert>
#include <iostream>
#include <sstream>

#include "util/string_util.h"

using namespace mesytec::mvlc;

namespace mesytec
{
namespace mvlc
{

std::string format_frame_flags(u8 frameFlags)
{
    if (!frameFlags)
        return "none";

    std::vector<std::string> buffer;

    if (frameFlags & frame_flags::Continue)
        buffer.emplace_back("continue");

    if (frameFlags & frame_flags::SyntaxError)
        buffer.emplace_back("syntax");

    if (frameFlags & frame_flags::BusError)
        buffer.emplace_back("BERR");

    if (frameFlags & frame_flags::Timeout)
        buffer.emplace_back("timeout");

    return util::join(buffer, ", ");
}

std::string decode_frame_header(u32 header)
{
    std::string result;
    std::ostringstream ss(result);

    auto headerInfo = extract_frame_info(header);

    switch (static_cast<frame_headers::FrameTypes>(headerInfo.type))
    {
        case frame_headers::SuperFrame:
            ss << "Super Frame (len=" << headerInfo.len;
            break;

        case frame_headers::StackFrame:
            ss << "Stack Result Frame (len=" << headerInfo.len;
            break;

        case frame_headers::BlockRead:
            ss << "Block Read Frame (len=" << headerInfo.len;
            break;

        case frame_headers::StackError:
            ss << "Stack Error Frame (len=" << headerInfo.len;
            break;

        case frame_headers::StackContinuation:
            ss << "Stack Result Continuation Frame (len=" << headerInfo.len;
            break;

        case frame_headers::SystemEvent:
            ss << "System Event (len=" << headerInfo.len;
            break;
    }

    switch (static_cast<frame_headers::FrameTypes>(headerInfo.type))
    {
        case frame_headers::StackFrame:
        case frame_headers::BlockRead:
        case frame_headers::StackError:
        case frame_headers::StackContinuation:
            {
                u16 stackNum = (header >> frame_headers::StackNumShift) & frame_headers::StackNumMask;
                ss << ", stackNum=" << stackNum;
            }
            break;

        case frame_headers::SuperFrame:
        case frame_headers::SystemEvent:
            break;
    }

    u8 frameFlags = (header >> frame_headers::FrameFlagsShift) & frame_headers::FrameFlagsMask;

    ss << ", frameFlags=" << format_frame_flags(frameFlags) << ")";

    return result;
}

void log_buffer(const u32 *buffer, size_t size, const std::string &info)
{
    using std::cout;
    using std::endl;

    cout << "begin " << info << " (size=" << size << ")" << endl;

    for (size_t i = 0; i < size; i++)
    {
        //printf("  %3lu: 0x%08x\n", i, buffer[i]);
        printf("  0x%08X\n", buffer[i]);
    }

    cout << "end " << info << endl;
}

void log_buffer(const std::vector<u32> &buffer, const std::string &info)
{
    log_buffer(buffer.data(), buffer.size(), info);
}

const char *get_system_event_subtype_name(u8 subtype_)
{
    switch (subtype_)
    {
        using namespace system_event::subtype;

        case EndianMarker:
            return "EndianMarker";

        case MVMEConfig:
            return "MVMEConfig";

        case MVLCConfig:
            return "MVLCConfig";

        case UnixTimestamp:
            return "UnixTimestamp";

        case Pause:
            return "Pause";

        case Resume:
            return "Resume";

        case EndOfFile:
            return "EndOfFile";
    }

    return "unknown system event subtype";
}

const char *get_frame_flag_shift_name(u8 flag_shift)
{
    if (flag_shift == frame_flags::shifts::Timeout)
        return "Timeout";

    if (flag_shift == frame_flags::shifts::BusError)
        return "BusError";

    if (flag_shift == frame_flags::shifts::SyntaxError)
        return "SyntaxError";

    if (flag_shift == frame_flags::shifts::Continue)
        return "Continue";

    return "Unknown";
}

stacks::TimerBaseUnit timer_base_unit_from_string(const std::string &str_)
{
    auto str = util::str_tolower(str_);

    if (str == "ns")
        return stacks::TimerBaseUnit::ns;

    if (str == "us" || str == "µs")
        return stacks::TimerBaseUnit::us;

    if (str == "ms")
        return stacks::TimerBaseUnit::ms;

    if (str == "s")
        return stacks::TimerBaseUnit::s;

    return {};
}

} // end namespace mvlc
} // end namespace mesytec
