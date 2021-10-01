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
                break;

            case CommandType::Write:
            case CommandType::WriteAbs:
                {
                    result.addVMEWrite(cmd.address, cmd.value, cmd.addressMode,
                                       convert_data_width(cmd.dataWidth));
                } break;

            case CommandType::Read:
            case CommandType::ReadAbs:
                {
                    result.addVMERead(cmd.address, cmd.addressMode,
                                      convert_data_width(cmd.dataWidth));
                } break;

            case CommandType::BLT:
            case CommandType::BLTFifo:
                {
                    result.addVMEBlockRead(cmd.address, vme_address_modes::BLT32, cmd.transfers);
                } break;

            case CommandType::MBLT:
            case CommandType::MBLTFifo:
                {
                    result.addVMEBlockRead(cmd.address, vme_address_modes::MBLT64, cmd.transfers);
                } break;

            case CommandType::MBLTSwapped:
                {
                    result.addVMEMBLTSwapped(cmd.address, vme_address_modes::MBLT64, cmd.transfers);
                } break;

            case CommandType::Marker:
                {
                    result.addWriteMarker(cmd.value);
                } break;

            case CommandType::Wait:
            case CommandType::VMUSB_ReadRegister:
            case CommandType::VMUSB_WriteRegister:
            case CommandType::Blk2eSST64:
            case CommandType::MVLC_WriteSpecial:
                qDebug() << __FUNCTION__ << " unsupported VME Script command:"
                    << to_string(cmd.type);
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

            case CommandType::MVLC_SetAddressIncMode:
                result.addSetAddressIncMode(static_cast<mvlc::AddressIncrementMode>(cmd.value));
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
                result.addReadToAccu(cmd.address, cmd.addressMode,
                                     convert_data_width(cmd.dataWidth));
                break;

            case CommandType::MVLC_CompareLoopAccu:
                result.addCompareLoopAccu(static_cast<mvlc::AccuComparator>(cmd.value), cmd.address);
                break;
        }
    }

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
