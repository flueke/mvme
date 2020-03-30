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
#ifndef __MVME_MVLC_DIALOG_UTIL_H__
#define __MVME_MVLC_DIALOG_UTIL_H__

#include <utility>
#include <vector>

#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "mvlc_error.h"

namespace mesytec
{
namespace mvlc
{

struct StackInfo
{
    u32 triggers;
    u32 offset;
    u16 startAddress;
    std::vector<u32> contents;
};

struct StackTrigger
{
    stacks::TriggerType triggerType;
    u8 irqLevel = 0;
};

template<typename DIALOG_API>
std::pair<std::vector<u32>, std::error_code>
read_stack_contents(DIALOG_API &mvlc, u16 startAddress)
{
    using namespace stack_commands;

    u32 stackHeader = 0u;

    if (auto ec = mvlc.readRegister(startAddress, stackHeader))
        return std::make_pair(std::vector<u32>{}, ec);

    std::vector<u32> contents;
    contents.reserve(64);
    contents.push_back(stackHeader);

    u8 headerType = (stackHeader >> CmdShift) & CmdMask; // 0xF3

    if (headerType != static_cast<u8>(StackCommandType::StackStart))
        return { contents, make_error_code(MVLCErrorCode::InvalidStackHeader) };

    u32 addr  = startAddress + AddressIncrement;
    u32 value = 0u;

    do
    {
        if (addr >= stacks::StackMemoryEnd)
            return { contents, make_error_code(MVLCErrorCode::StackMemoryExceeded) };

        if (auto ec = mvlc.readRegister(addr, value))
            return { contents, ec };

        contents.push_back(value);
        addr += AddressIncrement;

    } while (((value >> CmdShift) & CmdMask) != static_cast<u8>(StackCommandType::StackEnd)); // 0xF4

    return { contents, {} };
}

template<typename DIALOG_API>
std::pair<StackInfo, std::error_code>
read_stack_info(DIALOG_API &mvlc, u8 id)
{
    StackInfo result = {};

    if (id >= stacks::StackCount)
        return { result, make_error_code(MVLCErrorCode::StackCountExceeded) };

    if (auto ec = mvlc.readRegister(stacks::get_trigger_register(id), result.triggers))
        return { result, ec };

    if (auto ec = mvlc.readRegister(stacks::get_offset_register(id), result.offset))
        return { result, ec };

    result.startAddress = stacks::StackMemoryBegin + result.offset;

    auto sc = read_stack_contents(mvlc, result.startAddress);

    result.contents = sc.first;

    return { result, sc.second };
}

template<typename DIALOG_API>
std::error_code disable_all_triggers(DIALOG_API &mvlc)
{
    for (u8 stackId = 0; stackId < stacks::StackCount; stackId++)
    {
        u16 addr = stacks::get_trigger_register(stackId);

        if (auto ec = mvlc.writeRegister(addr, stacks::NoTrigger))
            return ec;
    }

    return {};
}

template<typename DIALOG_API>
std::error_code reset_stack_offsets(DIALOG_API &mvlc)
{
    for (u8 stackId = 0; stackId < stacks::StackCount; stackId++)
    {
        u16 addr = stacks::get_offset_register(stackId);

        if (auto ec = mvlc.writeRegister(addr, 0))
            return ec;
    }

    return {};
}

// Builds, uploads and sets up the readout stack for each event in the vme
// config.
template<typename DIALOG_API>
std::error_code setup_readout_stacks(
    DIALOG_API &mvlc,
    const std::vector<StackCommandBuilder> &readoutStacks)
{
    // Stack0 is reserved for immediate exec
    u8 stackId = stacks::ImmediateStackID + 1;

    // 1 word gap between immediate stack and first readout stack
    u16 uploadWordOffset = stacks::ImmediateStackReservedWords + 1;

    std::vector<u32> responseBuffer;

    for (const auto &stackBuilder: readoutStacks)
    {
        if (stackId >= stacks::StackCount)
            return make_error_code(MVLCErrorCode::StackCountExceeded);

        auto stackBuffer = make_stack_buffer(stackBuilder);

        u16 uploadAddress = uploadWordOffset * AddressIncrement;
        u16 endAddress    = uploadAddress + stackBuffer.size() * AddressIncrement;

        if (endAddress >= stacks::StackMemoryEnd)
            return make_error_code(MVLCErrorCode::StackMemoryExceeded);

        SuperCommandBuilder sb;
        sb.addStackUpload(stackBuffer, DataPipe, uploadAddress);

        auto uploadCommands = make_command_buffer(sb);

        if (auto ec = mvlc.mirrorTransaction(uploadCommands, responseBuffer))
            return ec;

        u16 offsetRegister = stacks::get_offset_register(stackId);

        if (auto ec = mvlc.writeRegister(offsetRegister, uploadAddress & stacks::StackOffsetBitMaskBytes))
            return ec;

        stackId++;
        // again leave a 1 word gap between stacks
        uploadWordOffset += stackBuffer.size() + 1;
    }

    return {};
}

template<typename DIALOG_API>
std::error_code setup_stack_trigger(
    DIALOG_API &mvlc, u8 stackId,
    stacks::TriggerType triggerType, u8 irqLevel = 0)
{
    u16 triggerReg = stacks::get_trigger_register(stackId);
    u32 triggerVal = triggerType << stacks::TriggerTypeShift;

    if ((triggerType == stacks::TriggerType::IRQNoIACK
         || triggerType == stacks::TriggerType::IRQWithIACK)
        && irqLevel > 0)
    {
        triggerVal |= (irqLevel - 1) & stacks::TriggerBitsMask;
    }

    return mvlc.writeRegister(triggerReg, triggerVal);
}

template<typename DIALOG_API>
std::error_code setup_stack_trigger(
    DIALOG_API &mvlc, u8 stackId, const StackTrigger &st)
{
    return setup_stack_trigger(mvlc, stackId, st.triggerType, st.irqLevel);
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_DIALOG_UTIL_H__ */
