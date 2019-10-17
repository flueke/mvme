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
std::error_code setup_readout_stacks(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger logger)
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

static std::error_code write_lut_ram(MVLCObject &mvlc, const trigger_io::LUT_RAM &lut, u16 address)
{
    for (u16 value: lut)
    {
        if (auto ec = mvlc.writeRegister(address, value))
            return ec;

        address += sizeof(u16);
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
                    logger(QSL("  Event %1: Stack %2, IRQ %3")
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
                    logger("No more timers available");
                    return make_error_code(MVLCErrorCode::TimerCountExceeded);
                }
                else
                {
                    // Trigger setup only. The actual setup of the timer and
                    // the connection between the Timer and StackStart units is
                    // done via the trigger/IO setup.
                    if (auto ec = mvlc.writeRegister(
                            stacks::get_trigger_register(stackId),
                            stacks::External << stacks::TriggerTypeShift))
                    {
                        return ec;
                    }

                    ++timersInUse;
                } break;

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

            // FIXME: ugly. Get rid of stacks::TimerBaseUnit. Use only trigger_io::Timer::Range
            timer.range = static_cast<trigger_io::Timer::Range>(timer_base_unit_from_string(
                    event->triggerOptions["mvlc.timer_base"].toString()));

            timer.softActivate = true;

            ioCfg.l0.unitNames[timersInUse] = QString("t_%1").arg(event->objectName());

            // Setup the l3 StackStart unit
            auto &ss = ioCfg.l3.stackStart[timersInUse];

            ss.activate = true;
            ss.stackIndex = stackId;

            ioCfg.l3.unitNames[timersInUse] = QString("ss_%1").arg(event->objectName());

            // Connect StackStart to the Timer
            auto choices = ioCfg.l3.DynamicInputChoiceLists[timersInUse];
            auto it = std::find(
                choices.begin(), choices.end(),
                trigger_io::UnitAddress{0, timersInUse});

            ioCfg.l3.connections[timersInUse] = it - choices.begin();

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

std::error_code setup_mvlc(MVLCObject &mvlc, VMEConfig &vmeConfig, Logger logger)
{
    if (auto ec = disable_all_triggers(mvlc))
    {
        logger(QString("Error disabling readout triggers: %1")
               .arg(ec.message().c_str()));
        return ec;
    }

    logger("Resetting stack offsets");

    if (auto ec = reset_stack_offsets(mvlc))
    {
        logger(QString("Error resetting stack offsets: %1")
               .arg(ec.message().c_str()));
        return ec;
    }

    logger("Setting up readout stacks");

    if (auto ec = setup_readout_stacks(mvlc, vmeConfig, logger))
    {
        logger(QString("Error setting up readout stacks: %1").arg(ec.message().c_str()));
        return ec;
    }

    logger("Applying trigger & I/O setup");

    if (auto ec = setup_trigger_io(mvlc, vmeConfig, logger))
    {
        logger(QString("Error applying trigger & I/O setup: %1").arg(ec.message().c_str()));
        return ec;
    }

    logger("Enabling triggers");

    if (auto ec = enable_triggers(mvlc, vmeConfig, logger))
        return ec;

    return {};
}

} // end namespace mvlc
} // end namespace mesytec
