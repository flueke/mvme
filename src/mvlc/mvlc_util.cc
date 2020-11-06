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
#include <QDebug>

#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvme_mvlc;

namespace mesytec
{
namespace mvme_mvlc
{

//
// vme_script -> mvlc
//
mvlc::VMEDataWidth convert_data_width(vme_script::DataWidth width)
{
    switch (width)
    {
        case vme_script::DataWidth::D16: return mvlc::VMEDataWidth::D16;
        case vme_script::DataWidth::D32: return mvlc::VMEDataWidth::D32;
    }

    return mvlc::VMEDataWidth::D16;
}

u8 convert_data_width_untyped(vme_script::DataWidth width)
{
    return static_cast<u8>(convert_data_width(width));
}

vme_script::DataWidth convert_data_width(mvlc::VMEDataWidth dataWidth)
{
    switch (dataWidth)
    {
        case mvlc::VMEDataWidth::D16: return vme_script::DataWidth::D16;
        case mvlc::VMEDataWidth::D32: return vme_script::DataWidth::D32;
    }

    throw std::runtime_error("invalid mvlc::VMEDataWidth given");
}

std::vector<u32> build_stack(const vme_script::VMEScript &script, u8 outPipe)
{
    using namespace mesytec::mvlc::stack_commands;
    std::vector<u32> result;

    u32 firstWord = static_cast<u32>(StackCommandType::StackStart) << CmdShift | outPipe << CmdArg0Shift;
    result.push_back(firstWord);

    for (auto &cmd: script)
    {
        using vme_script::CommandType;

        switch (cmd.type)
        {
            case CommandType::Invalid:
            case CommandType::SetBase:
            case CommandType::ResetBase:
            case CommandType::MetaBlock:
            case CommandType::SetVariable:
            case CommandType::Print:
                break;

            case CommandType::Write:
            case CommandType::WriteAbs:
                {
                    firstWord = static_cast<u32>(StackCommandType::VMEWrite) << CmdShift;
                    firstWord |= cmd.addressMode << CmdArg0Shift;
                    firstWord |= convert_data_width_untyped(cmd.dataWidth) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                    result.push_back(cmd.value);
                } break;

            case CommandType::Read:
                {
                    firstWord = static_cast<u32>(StackCommandType::VMERead) << CmdShift;
                    firstWord |= cmd.addressMode << CmdArg0Shift;
                    firstWord |= convert_data_width_untyped(cmd.dataWidth) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                } break;

            case CommandType::BLT:
            case CommandType::BLTFifo:
                {
                    firstWord = static_cast<u32>(StackCommandType::VMERead) << CmdShift;
                    firstWord |= vme_address_modes::BLT32 << CmdArg0Shift;
                    firstWord |= (cmd.transfers & CmdArg1Mask) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                } break;

            case CommandType::MBLT:
            case CommandType::MBLTFifo:
                {
                    firstWord = static_cast<u32>(StackCommandType::VMERead) << CmdShift;
                    firstWord |= vme_address_modes::MBLT64 << CmdArg0Shift;
                    firstWord |= (cmd.transfers & CmdArg1Mask) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                } break;

            case CommandType::MBLTSwapped:
                {
                    firstWord = static_cast<u32>(StackCommandType::VMEMBLTSwapped) << CmdShift;
                    firstWord |= vme_address_modes::MBLT64 << CmdArg0Shift;
                    firstWord |= (cmd.transfers & CmdArg1Mask) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                } break;

            case CommandType::Blk2eSST64:
                {
                    firstWord = static_cast<u32>(StackCommandType::VMERead) << CmdShift;
                    firstWord |= (vme_address_modes::Blk2eSST64
                                  | (cmd.blk2eSSTRate << mvlc::Blk2eSSTRateShift))
                        << CmdArg0Shift;
                    firstWord |= (cmd.transfers & CmdArg1Mask) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                } break;

            case CommandType::Marker:
                {
                    firstWord = static_cast<u32>(StackCommandType::WriteMarker) << CmdShift;
                    result.push_back(firstWord);
                    result.push_back(cmd.value);
                } break;

            case CommandType::MVLC_WriteSpecial:
                {
                    firstWord = static_cast<u32>(StackCommandType::WriteSpecial) << CmdShift;
                    firstWord |= cmd.value & 0x00FFFFFFu;
                    result.push_back(firstWord);
                } break;

            case CommandType::Wait:
            case CommandType::VMUSB_ReadRegister:
            case CommandType::VMUSB_WriteRegister:
                qDebug() << __FUNCTION__ << " unsupported VME Script command:"
                    << to_string(cmd.type);
                break;
        }
    }

    firstWord = static_cast<u32>(StackCommandType::StackEnd) << CmdShift;
    result.push_back(firstWord);

    return result;
}

std::vector<u32> build_upload_commands(
    const vme_script::VMEScript &script, u8 outPipe, u16 startAddress)
{
    auto stack = build_stack(script, outPipe);
    return build_upload_commands(stack, startAddress);
}

std::vector<u32> build_upload_commands(const std::vector<u32> &stack, u16 startAddress)
{
    using namespace mesytec::mvlc::super_commands;

    std::vector<u32> result;
    result.reserve(stack.size() * 2);

    u16 address = startAddress;

    for (u32 stackValue: stack)
    {
        u32 cmdValue = static_cast<u32>(SuperCommandType::WriteLocal) << SuperCmdShift;
        cmdValue |= address;
        address += mvlc::AddressIncrement;
        result.push_back(cmdValue);
        result.push_back(stackValue);
    }

    return result;
}

std::vector<u32> build_upload_command_buffer(const vme_script::VMEScript &script, u8 outPipe,
                                         u16 startAddress)
{
    auto stack = build_stack(script, outPipe);
    return build_upload_command_buffer(stack, startAddress);
}

std::vector<u32> build_upload_command_buffer(const std::vector<u32> &stack, u16 startAddress)
{
    using namespace mesytec::mvlc::super_commands;

    std::vector<u32> result;
    auto uploadData = build_upload_commands(stack, startAddress);
    result.reserve(uploadData.size() + 2);
    result.push_back(static_cast<u32>(SuperCommandType::CmdBufferStart) << SuperCmdShift);
    std::copy(uploadData.begin(), uploadData.end(), std::back_inserter(result));
    result.push_back(static_cast<u32>(SuperCommandType::CmdBufferEnd) << SuperCmdShift);

    return result;
}

void log_buffer(const QVector<u32> &buffer, const QString &info)
{
    std::vector<u32> vec;
    std::copy(buffer.begin(), buffer.end(), std::back_inserter(vec));
    mvlc::util::log_buffer(std::cout, vec, info.toStdString());
}

} // end namespace mvme_mvlc
} // end namespace mesytec
