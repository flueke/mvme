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
#include "mvlc_daq.h"
#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_trigger_io_script.h"
#include "mvlc/mvlc_util.h"
#include "vme_daq.h"

namespace mesytec
{
namespace mvlc
{

std::error_code disable_all_triggers(MVLCObject &mvlc)
{
    return disable_all_triggers<MVLCObject>(mvlc);
}

std::error_code reset_stack_offsets(MVLCObject &mvlc)
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
std::error_code setup_readout_stacks(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger)
{
    // Stack0 is reserved for immediate exec
    u8 stackId = stacks::ImmediateStackID + 1;

    // 1 word gap between immediate stack and first readout stack
    u16 uploadOffset = stacks::ImmediateStackReservedWords + 1;

    QVector<u32> responseBuffer;

    for (const auto &event: vmeConfig.getEventConfigs())
    {
        if (stackId >= stacks::StackCount)
            return make_error_code(MVLCErrorCode::StackCountExceeded);

        auto readoutScript = build_event_readout_script(
            event, EventReadoutBuildFlags::NoModuleEndMarker);

        auto stackContents = build_stack(readoutScript, DataPipe);

        u16 uploadAddress = stacks::StackMemoryBegin + uploadOffset * 4;
        u16 endAddress    = uploadAddress + stackContents.size() * 4;

        if (endAddress >= stacks::StackMemoryEnd)
            return make_error_code(MVLCErrorCode::StackMemoryExceeded);

        auto uploadCommands = build_upload_command_buffer(stackContents, uploadAddress);

        if (auto ec = mvlc.mirrorTransaction(uploadCommands, responseBuffer))
            return ec;

        u16 offsetRegister = stacks::get_offset_register(stackId);

        if (auto ec = mvlc.writeRegister(offsetRegister, uploadAddress & stacks::StackOffsetBitMaskBytes))
            return ec;

        stackId++;
        // again leave a 1 word gap between stacks
        uploadOffset += stackContents.size() + 1;
    }

    return {};
}

std::error_code enable_triggers(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger logger)
{
    u8 stackId = stacks::ImmediateStackID + 1;
    u16 timersInUse = 0u;

    for (const auto &event: vmeConfig.getEventConfigs())
    {
        switch (event->triggerCondition)
        {
            case TriggerCondition::Interrupt:
                {
                    logger(QSL("    Event %1: Stack %2, IRQ %3")
                           .arg(event->objectName()).arg(stackId)
                           .arg(event->irqLevel));

                    bool useIACK = event->triggerOptions["IRQUseIACK"].toBool();

                    u16 triggerReg = stacks::get_trigger_register(stackId);

                    u32 triggerVal = (useIACK
                                      ? stacks::IRQWithIACK
                                      : stacks::IRQNoIACK
                                      ) << stacks::TriggerTypeShift;

                    triggerVal |= (event->irqLevel - 1) & stacks::TriggerBitsMask;

                    if (auto ec = mvlc.writeRegister(triggerReg, triggerVal))
                        return ec;

                } break;

            case TriggerCondition::Periodic:
                if (timersInUse >= stacks::TimerCount)
                {
                    return make_error_code(MVLCErrorCode::TimerCountExceeded);
                }
                else
                {
                    logger(QSL("    Event %1: Stack %2, periodic")
                           .arg(event->objectName()).arg(stackId));

                    // Set the stack trigger to 'External'. The actual setup of
                    // the timer and the connection between the Timer and
                    // StackStart units is done in setup_trigger_io().
                    if (auto ec = mvlc.writeRegister(
                            stacks::get_trigger_register(stackId),
                            stacks::External << stacks::TriggerTypeShift))
                    {
                        return ec;
                    }

                    ++timersInUse;
                } break;

            case TriggerCondition::TriggerIO:
                    logger(QSL("    Event %1: Stack %2, via MVLC Trigger I/O")
                           .arg(event->objectName()).arg(stackId));

                    // Set the stack trigger to 'External'. The actual trigger
                    // setup is done by the user via the trigger io gui.
                    if (auto ec = mvlc.writeRegister(
                            stacks::get_trigger_register(stackId),
                            stacks::External << stacks::TriggerTypeShift))
                    {
                        return ec;
                    }
                break;

            InvalidDefaultCase;
        }

        stackId++;
    }

    return {};
}

std::error_code setup_trigger_io(
    MVLCObject &mvlc, VMEConfig &vmeConfig, Logger logger)
{
    auto scriptConfig = qobject_cast<VMEScriptConfig *>(
        vmeConfig.getGlobalObjectRoot().findChildByName("mvlc_trigger_io"));

    assert(scriptConfig);
    if (!scriptConfig)
        return make_error_code(MVLCErrorCode::ReadoutSetupError);

    auto ioCfg = trigger_io::parse_trigger_io_script_text(
        scriptConfig->getScriptContents());


    // First disable all of the StackStart units. This is to avoid any side
    // effects from vme events that have been removed or any other sort of
    // stack triggering.
#if 0
    // FIXME
    // Disabled for now as it conflicts with user-defined StackStart
    // configurations. It basically disables them and undoes the changes done
    // in the GUI. A fix would be to regnerate and execute the trigger io
    // script at this point so that the units are disabled but the
    // modifications are not written back to the trigger io config.
    // Even better would be a way to create a snippet containing only the units
    // that should be reset and execute that.
    for (auto &ss: ioCfg.l3.stackStart)
        ss.activate = false;
#endif

    u8 stackId = stacks::ImmediateStackID + 1;
    u16 timersInUse = 0u;

    for (const auto &event: vmeConfig.getEventConfigs())
    {
        if (event->triggerCondition == TriggerCondition::Periodic)
        {
            if (timersInUse >= stacks::TimerCount)
            {
                logger("No more timers available");
                return make_error_code(MVLCErrorCode::TimerCountExceeded);
            }

            // Setup the l0 timer unit
            auto &timer = ioCfg.l0.timers[timersInUse];
            timer.period = event->triggerOptions["mvlc.timer_period"].toUInt();

            timer.range = timer_base_unit_from_string(
                    event->triggerOptions["mvlc.timer_base"].toString());

            timer.softActivate = true;

            ioCfg.l0.unitNames[timersInUse] = QString("t_%1").arg(event->objectName());

            // Setup the l3 StackStart unit
            auto &ss = ioCfg.l3.stackStart[timersInUse];

            ss.activate = true;
            ss.stackIndex = stackId;

            ioCfg.l3.unitNames[timersInUse] = QString("ss_%1").arg(event->objectName());

            // Connect StackStart to the Timer
            auto choices = ioCfg.l3.DynamicInputChoiceLists[timersInUse][0];
            auto it = std::find(
                choices.begin(), choices.end(),
                trigger_io::UnitAddress{0, timersInUse});

            ioCfg.l3.connections[timersInUse][0] = it - choices.begin();

            ++timersInUse;
        }

        ++stackId;
    }

    auto ioCfgText = trigger_io::generate_trigger_io_script_text(ioCfg);

    // Update the trigger io script stored in the VMEConfig in case we modified
    // it.
    if (ioCfgText != scriptConfig->getScriptContents())
    {
        scriptConfig->setScriptContents(ioCfgText);
    }

    // Parse the trigger io script and run the writes contained within.
    auto commands = vme_script::parse(ioCfgText);

    for (auto &cmd: commands)
    {
        if (cmd.type != vme_script::CommandType::Write)
            continue;

        if (auto ec = mvlc.vmeSingleWrite(
                cmd.address, cmd.value,
                cmd.addressMode, convert_data_width(cmd.dataWidth)))
        {
            return ec;
        }

    }

    return {};
}

inline std::error_code read_vme_reg(MVLCObject &mvlc, u16 reg, u32 &dest)
{
    return mvlc.vmeSingleRead(SelfVMEAddress + reg, dest,
                              vme_address_modes::a32UserData, VMEDataWidth::D16);
}

inline std::error_code write_vme_reg(MVLCObject &mvlc, u16 reg, u16 value)
{
    return mvlc.vmeSingleWrite(SelfVMEAddress + reg, value,
                               vme_address_modes::a32UserData, VMEDataWidth::D16);
}

std::error_code setup_mvlc(MVLCObject &mvlc, VMEConfig &vmeConfig, Logger logger)
{
    logger("Initializing MVLC Triggers and I/O");

    logger("  Disabling triggers");

    if (auto ec = disable_all_triggers(mvlc))
    {
        logger(QString("Error disabling readout triggers: %1")
               .arg(ec.message().c_str()));
        return ec;
    }

    logger("  Resetting stack offsets");

    if (auto ec = reset_stack_offsets(mvlc))
    {
        logger(QString("Error resetting stack offsets: %1")
               .arg(ec.message().c_str()));
        return ec;
    }

    logger("  Setting up readout stacks");

    if (auto ec = setup_readout_stacks(mvlc, vmeConfig, logger))
    {
        logger(QString("Error setting up readout stacks: %1").arg(ec.message().c_str()));
        return ec;
    }

    logger("  Applying trigger & I/O setup");

    if (auto ec = setup_trigger_io(mvlc, vmeConfig, logger))
    {
        logger(QString("Error applying trigger & I/O setup: %1").arg(ec.message().c_str()));
        return ec;
    }

    logger("  Enabling triggers");

    if (auto ec = enable_triggers(mvlc, vmeConfig, logger))
        return ec;

    return {};
}

} // end namespace mvlc
} // end namespace mesytec
