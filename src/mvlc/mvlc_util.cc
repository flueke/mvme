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
#include "mvlc_util.h"

#include <cassert>
#include <iostream>
#include <QDebug>
#include <QVector>

#include <mesytec-mvlc/mesytec-mvlc.h>

#include "vmeconfig_to_crateconfig.h"

using namespace mesytec::mvme_mvlc;

namespace mesytec
{
namespace mvme_mvlc
{

LIBMVME_MVLC_EXPORT mvlc::StackCommandBuilder
    build_mvlc_stack(const vme_script::VMEScript &script)
{
    mvlc::StackCommandBuilder result;

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
            case CommandType::Accu_Set:
            case CommandType::Accu_MaskAndRotate:
            case CommandType::Accu_Test:
                break;

            case CommandType::Write:
            case CommandType::WriteAbs:
                {
                    result.addVMEWrite(cmd.address, cmd.value, cmd.addressMode, cmd.dataWidth);
                } break;

            case CommandType::Read:
            case CommandType::ReadAbs:
                {
                    result.addVMERead(cmd.address, cmd.addressMode, cmd.dataWidth, cmd.mvlcSlowRead);
                } break;

            case CommandType::BLT:
                {
                    result.addVMEBlockRead(cmd.address, vme_address_modes::BLT32, cmd.transfers, false);
                } break;

            case CommandType::BLTFifo:
                {
                    result.addVMEBlockRead(cmd.address, vme_address_modes::BLT32, cmd.transfers, true);
                } break;

            case CommandType::MBLT:
                {
                    result.addVMEBlockRead(cmd.address, vme_address_modes::MBLT64, cmd.transfers, false);
                } break;

            case CommandType::MBLTFifo:
                {
                    result.addVMEBlockRead(cmd.address, vme_address_modes::MBLT64, cmd.transfers, true);
                } break;

            case CommandType::MBLTSwapped:
                {
                    result.addVMEBlockReadSwapped(cmd.address, cmd.transfers, false);
                } break;

            case CommandType::MBLTSwappedFifo:
                {
                    result.addVMEBlockReadSwapped(cmd.address, cmd.transfers, true);
                } break;

            case CommandType::Blk2eSST64:
                {
                    result.addVMEBlockRead(
                        cmd.address, static_cast<mesytec::mvlc::Blk2eSSTRate>(cmd.blk2eSSTRate), cmd.transfers, false);
                } break;

            case CommandType::Blk2eSST64Fifo:
                {
                    result.addVMEBlockRead(
                        cmd.address, static_cast<mesytec::mvlc::Blk2eSSTRate>(cmd.blk2eSSTRate), cmd.transfers, true);
                } break;

            case CommandType::Blk2eSST64Swapped:
                {
                    result.addVMEBlockReadSwapped(
                        cmd.address, static_cast<mesytec::mvlc::Blk2eSSTRate>(cmd.blk2eSSTRate), cmd.transfers, false);
                } break;

            case CommandType::Blk2eSST64SwappedFifo:
                {
                    result.addVMEBlockReadSwapped(
                        cmd.address, static_cast<mesytec::mvlc::Blk2eSSTRate>(cmd.blk2eSSTRate), cmd.transfers, true);
                } break;

            case CommandType::Marker:
                {
                    result.addWriteMarker(cmd.value);
                } break;

            case CommandType::Wait:
            case CommandType::VMUSB_ReadRegister:
            case CommandType::VMUSB_WriteRegister:
                qDebug() << __FUNCTION__ << " unsupported VME Script command:"
                    << to_string(cmd.type);
                throw std::runtime_error(fmt::format("build_mvlc_stack: unsupported VME Script command {}",
                                                     to_string(cmd.type).toStdString()));
                break;

            case CommandType::MVLC_Custom:
                {
                    mvlc::StackCommand stackCmd;
                    stackCmd.type = mvlc::StackCommand::CommandType::Custom;

                    for (u32 value: cmd.mvlcCustomStack)
                        stackCmd.customValues.push_back(value);

                    result.addCommand(stackCmd);
                }
                break;

            case CommandType::MVLC_Wait:
                result.addWait(cmd.value);
                break;

            case CommandType::MVLC_SignalAccu:
                result.addSignalAccu();
                break;

            case CommandType::MVLC_MaskShiftAccu:
                result.addMaskShiftAccu(cmd.address, cmd.value);
                break;

            case CommandType::MVLC_SetAccu:
                result.addSetAccu(cmd.value);
                break;

            case CommandType::MVLC_ReadToAccu:
                result.addReadToAccu(cmd.address, cmd.addressMode, cmd.dataWidth, cmd.mvlcSlowRead);
                break;

            case CommandType::MVLC_CompareLoopAccu:
                result.addCompareLoopAccu(static_cast<mvlc::AccuComparator>(cmd.value), cmd.address);
                break;

            case CommandType::MVLC_WriteSpecial:
                result.addWriteSpecial(cmd.value);
                break;

            case CommandType::MVLC_InlineStack:
                {
                    // This "flattens" the mvlc inline stack defined in a

                    vme_script::VMEScript stackScript;

                    for (const auto &cmdPtr: cmd.mvlcInlineStack)
                        stackScript.push_back(*cmdPtr);

                    auto inlineStack = build_mvlc_stack(stackScript);

                    for (auto &inlineCommand: inlineStack.getCommands())
                        result.addCommand(inlineCommand);

                } break;
        }
    }

    return result;
}

LIBMVME_MVLC_EXPORT mvlc::StackCommandBuilder
    build_mvlc_stack(const std::vector<vme_script::Command> &script)
{
    return build_mvlc_stack(QVector<vme_script::Command>::fromStdVector(script));
}

void log_buffer(const QVector<u32> &buffer, const QString &info)
{
    std::vector<u32> vec;
    std::copy(buffer.begin(), buffer.end(), std::back_inserter(vec));
    mvlc::util::log_buffer(std::cout, vec, info.toStdString());
}

} // end namespace mvme_mvlc
} // end namespace mesytec
