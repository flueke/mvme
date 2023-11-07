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
#include "mvlc_daq.h"

#include <mesytec-mvlc/mesytec-mvlc.h>
#include "mesytec-mvlc/mvlc_command_builders.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_trigger_io_script.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvlc/mvlc_util.h"
#include "util/strings.h"
#include "vme_daq.h"

namespace mesytec
{
namespace mvme_mvlc
{

std::error_code disable_all_triggers_and_daq_mode(MVLCObject &mvlc)
{
    return mvlc::disable_all_triggers_and_daq_mode<mvme_mvlc::MVLCObject>(mvlc);
}

std::error_code reset_stack_offsets(MVLCObject &mvlc)
{
    for (u8 stackId = 0; stackId < mvlc::stacks::StackCount; stackId++)
    {
        u16 addr = mvlc::stacks::get_offset_register(stackId);

        if (auto ec = mvlc.writeRegister(addr, 0))
            return ec;
    }

    return {};
}

mvlc::StackCommandBuilder get_readout_commands(const EventConfig &event)
{
    auto readoutScript = build_event_readout_script(
        &event, EventReadoutBuildFlags::NoModuleEndMarker);
    return build_mvlc_stack(readoutScript);
}

// Builds the readout stack in the form of a StackCommandBuilder for each
// readout event in the vme config. Returns a list of StackCommandBuilder
// instances, one builder per event.
std::vector<mvlc::StackCommandBuilder> get_readout_stacks(const VMEConfig &vmeConfig)
{
    std::vector<mvlc::StackCommandBuilder> stacks;

    for (const auto &event: vmeConfig.getEventConfigs())
        stacks.emplace_back(get_readout_commands(*event));

    return stacks;
}

// Builds, uploads and sets up the readout stack for each event in the vme
// config.
// FIXME: multiple stack conversions. Pretty hacky now
std::error_code setup_readout_stacks(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger)
{
    // Stack0 is reserved for immediate exec
    u8 stackId = mvlc::stacks::ImmediateStackID + 1;

    // 1 word gap between immediate stack and first readout stack
    u16 uploadWordOffset = mvlc::stacks::ImmediateStackStartOffsetWords +
        mvlc::stacks::ImmediateStackReservedWords + 1;

    std::vector<u32> responseBuffer;

    for (const auto &event: vmeConfig.getEventConfigs())
    {
        if (stackId >= mvlc::stacks::StackCount)
            return make_error_code(mvlc::MVLCErrorCode::StackCountExceeded);

        auto stackBuilder = get_readout_commands(*event);
        auto stackBuffer = make_stack_buffer(stackBuilder);

        u16 uploadAddress = uploadWordOffset * mvlc::AddressIncrement;
        u16 endAddress    = uploadAddress + stackBuffer.size() * mvlc::AddressIncrement;

        if (mvlc::stacks::StackMemoryBegin + endAddress >= mvlc::stacks::StackMemoryEnd)
            return make_error_code(mvlc::MVLCErrorCode::StackMemoryExceeded);

        if (auto ec = mvlc.uploadStack(mvlc::DataPipe, uploadAddress, stackBuffer))
            return ec;

        u16 offsetRegister = mvlc::stacks::get_offset_register(stackId);

        uploadAddress = uploadAddress & mvlc::stacks::StackOffsetBitMaskBytes;

        if (auto ec = mvlc.writeRegister(offsetRegister, uploadAddress))
            return ec;

        stackId++;

        // again leave a 1 word gap between stacks and account for the F3/F4 stack begin/end words
        uploadWordOffset += stackBuffer.size() + 1 + 2;
    }

    return {};
}

std::error_code enable_triggers(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger logger)
{
    u8 stackId = mvlc::stacks::ImmediateStackID + 1;
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

                    u16 triggerReg = mvlc::stacks::get_trigger_register(stackId);

                    u32 triggerVal = (useIACK
                                      ? mvlc::stacks::IRQWithIACK
                                      : mvlc::stacks::IRQNoIACK
                                      ) << mvlc::stacks::TriggerTypeShift;

                    triggerVal |= (event->irqLevel - 1) & mvlc::stacks::TriggerBitsMask;

                    if (auto ec = mvlc.writeRegister(triggerReg, triggerVal))
                        return ec;

                } break;

            case TriggerCondition::Periodic:
                if (timersInUse >= mvlc::stacks::TimerCount)
                {
                    return make_error_code(mvlc::MVLCErrorCode::TimerCountExceeded);
                }
                else
                {
                    logger(QSL("    Event %1: Stack %2, periodic")
                           .arg(event->objectName()).arg(stackId));

                    // Set the stack trigger to 'External'. The actual setup of
                    // the timer and the connection between the Timer and
                    // StackStart units is done in setup_trigger_io().
                    if (auto ec = mvlc.writeRegister(
                            mvlc::stacks::get_trigger_register(stackId),
                            mvlc::stacks::External << mvlc::stacks::TriggerTypeShift))
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
                            mvlc::stacks::get_trigger_register(stackId),
                            mvlc::stacks::External << mvlc::stacks::TriggerTypeShift))
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

std::pair<std::vector<u32>, std::error_code> get_trigger_values(const VMEConfig &vmeConfig, Logger logger)
{
    std::vector<u32> triggers;

    u8 stackId = mvlc::stacks::ImmediateStackID + 1;
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

                    mvlc::StackTrigger st;
                    st.triggerType = (useIACK ? mvlc::stacks::IRQWithIACK : mvlc::stacks::IRQNoIACK);
                    st.irqLevel = event->irqLevel;
                    triggers.push_back(mvlc::trigger_value(st));
                } break;

            case TriggerCondition::Periodic:
                if (timersInUse >= mvlc::stacks::TimerCount)
                {
                    auto ec = make_error_code(mvlc::MVLCErrorCode::TimerCountExceeded);
                    return std::make_pair(triggers, ec);
                }
                else
                {
                    logger(QSL("    Event %1: Stack %2, periodic")
                           .arg(event->objectName()).arg(stackId));

                    // Set the stack trigger to 'External'. The actual setup of
                    // the timer and the connection between the Timer and
                    // StackStart units is done in setup_trigger_io().
                    mvlc::StackTrigger st{ mvlc::stacks::External, 0 };
                    st.triggerType = mvlc::stacks::External;
                    triggers.push_back(mvlc::trigger_value(st));
                    ++timersInUse;
                } break;

            case TriggerCondition::TriggerIO:
                {
                    logger(QSL("    Event %1: Stack %2, via MVLC Trigger I/O")
                           .arg(event->objectName()).arg(stackId));

                    // Set the stack trigger to 'External'. The actual trigger
                    // setup is done by the user via the trigger io gui.
                    mvlc::StackTrigger st{ mvlc::stacks::External, 0 };
                    st.triggerType = mvlc::stacks::External;
                    triggers.push_back(mvlc::trigger_value(st));
                }
                break;

            InvalidDefaultCase;
        }

        stackId++;
    }

    return std::make_pair(triggers, std::error_code{});
}

std::error_code setup_trigger_io(
    MVLC_VMEController *mvlc, VMEConfig &vmeConfig, Logger logger)
{
    auto scriptConfig = qobject_cast<VMEScriptConfig *>(
        vmeConfig.getGlobalObjectRoot().findChildByName("mvlc_trigger_io"));

    assert(scriptConfig);
    if (!scriptConfig)
        return make_error_code(mvlc::MVLCErrorCode::ReadoutSetupError);

    auto ioCfg = update_trigger_io(vmeConfig);
    auto ioCfgText = trigger_io::generate_trigger_io_script_text(ioCfg);

    // Update the trigger io script stored in the VMEConfig in case we modified
    // it.
    if (ioCfgText != scriptConfig->getScriptContents())
    {
        scriptConfig->setScriptContents(ioCfgText);
    }

    // Parse the trigger io script and run the writes contained within.
    auto commands = vme_script::parse(ioCfgText);
    auto results = vme_script::run_script(mvlc, commands, logger,
        vme_script::run_script_options::AbortOnError);

    if (vme_script::has_errors(results))
    {
        auto firstError = std::find_if(std::begin(results), std::end(results),
            [] (const vme_script::Result &r) { return r.error.isError(); });
        if (firstError != std::end(results))
            logger(firstError->error.toString());
        return mvlc::MVLCErrorCode::ReadoutSetupError;
    }

    return {};
}

inline std::error_code read_vme_reg(MVLCObject &mvlc, u16 reg, u32 &dest)
{
    return mvlc.vmeRead(mvlc::SelfVMEAddress + reg, dest,
                        vme_address_modes::a32UserData, mvlc::VMEDataWidth::D16);
}

inline std::error_code write_vme_reg(MVLCObject &mvlc, u16 reg, u16 value)
{
    return mvlc.vmeWrite(mvlc::SelfVMEAddress + reg, value,
                         vme_address_modes::a32UserData, mvlc::VMEDataWidth::D16);
}

mesytec::mvme_mvlc::trigger_io::TriggerIO
    update_trigger_io(const VMEConfig &vmeConfig)
{
    auto scriptConfig = qobject_cast<VMEScriptConfig *>(
        vmeConfig.getGlobalObjectRoot().findChildByName("mvlc_trigger_io"));

    assert(scriptConfig);

    if (!scriptConfig) return {}; // TODO: error_code

    auto ioCfg = trigger_io::parse_trigger_io_script_text(
        scriptConfig->getScriptContents());

    u8 stackId = mvlc::stacks::ImmediateStackID + 1;
    u16 timersInUse = 0u;

    for (const auto &event: vmeConfig.getEventConfigs())
    {
        if (event->triggerCondition == TriggerCondition::Periodic)
        {
            if (timersInUse >= mvlc::stacks::TimerCount)
                return {}; // TODO: error_code

            // Setup the l0 timer unit
            auto &timer = ioCfg.l0.timers[timersInUse];
            timer.period = event->triggerOptions["mvlc.timer_period"].toUInt();

            timer.range = mvlc::timer_base_unit_from_string(
                    event->triggerOptions["mvlc.timer_base"].toString().toStdString());

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

    return ioCfg;
}

void update_trigger_io_inplace(const VMEConfig &vmeConfig)
{
    auto scriptConfig = qobject_cast<VMEScriptConfig *>(
        vmeConfig.getGlobalObjectRoot().findChildByName("mvlc_trigger_io"));

    if (!scriptConfig) return; // TODO: error_code

    auto ioCfg = mesytec::mvme_mvlc::update_trigger_io(vmeConfig);
    auto ioCfgText = mesytec::mvme_mvlc::trigger_io::generate_trigger_io_script_text(ioCfg);

    // Update the trigger io script stored in the VMEConfig in case it changed.
    if (ioCfgText != scriptConfig->getScriptContents())
    {
        scriptConfig->setScriptContents(ioCfgText);
    }
}

mvlc::StackCommandBuilder make_module_init_stack(const VMEConfig &config)
{
    mvlc::StackCommandBuilder stack;

    for (auto eventConfig: config.getEventConfigs())
    {
        for (auto module: eventConfig->getModuleConfigs())
        {
            if (!module->isEnabled())
                continue;

            auto path = QSL("%1.%2")
                .arg(eventConfig->objectName())
                .arg(module->objectName()).toStdString();

            stack.addGroup(
                path + ".Module Reset",
                mvme::convert_script(module->getResetScript(), module->getBaseAddress()));

            for (auto initScript: module->getInitScripts())
            {
                stack.addGroup(
                    path + "." + initScript->objectName().toStdString(),
                    mvme::convert_script(initScript, module->getBaseAddress()));
            }
        }
    }



    return stack;
}

std::vector<mvlc::StackCommandBuilder> sanitize_readout_stacks(
    const std::vector<mvlc::StackCommandBuilder> &inputStacks)
{
    std::vector<mvlc::StackCommandBuilder> sanitizedReadoutStacks;

    for (auto &srcStack: inputStacks)
    {
        mvlc::StackCommandBuilder dstStack;

        for (const auto &srcGroup: srcStack.getGroups())
        {
            if (mvlc::produces_output(srcGroup))
                dstStack.addGroup(srcGroup);
        }

        sanitizedReadoutStacks.emplace_back(dstStack);
    }

    return sanitizedReadoutStacks;
}

mvlc::listfile::SplitListfileSetup make_listfile_setup(ListFileOutputInfo &outInfo, const std::vector<u8> &preamble)
{
    using namespace mesytec::mvlc;

    listfile::SplitListfileSetup lfSetup;
    lfSetup.entryType = (outInfo.format == ListFileFormat::ZIP
                            ? listfile::ZipEntryInfo::ZIP
                            : listfile::ZipEntryInfo::LZ4);
    lfSetup.compressLevel = outInfo.compressionLevel;
    if (outInfo.flags & ListFileOutputInfo::SplitBySize)
        lfSetup.splitMode = listfile::ZipSplitMode::SplitBySize;
    else if (outInfo.flags & ListFileOutputInfo::SplitByTime)
        lfSetup.splitMode = listfile::ZipSplitMode::SplitByTime;

    lfSetup.splitSize = outInfo.splitSize;
    lfSetup.splitTime = outInfo.splitTime;

    QFileInfo lfInfo(make_new_listfile_name(&outInfo));
    auto lfDir = lfInfo.path();
    auto lfBase = lfInfo.completeBaseName();
    auto lfPrefix = lfDir + "/" + lfBase;

    lfSetup.filenamePrefix = lfPrefix.toStdString();
    lfSetup.preamble = preamble;

    return lfSetup;
}

} // end namespace mvme_mvlc
} // end namespace mesytec
