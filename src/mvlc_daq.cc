#include "mvlc/mvlc_daq.h"
#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_util.h"
#include "vme_daq.h"

namespace mesytec
{
namespace mvlc
{

std::error_code disable_all_triggers(MVLCObject &mvlc)
{
    for (u8 stackId = 0; stackId < stacks::StackCount; stackId++)
    {
        u16 addr = stacks::get_trigger_register(stackId);

        if (auto ec = mvlc.writeRegister(addr, stacks::NoTrigger))
            return ec;
    }

    return {};
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

        auto readoutScript = build_event_readout_script(event);
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

std::error_code setup_triggers(MVLCObject &mvlc, const VMEConfig &vmeConfig)
{
    u8 stackId = stacks::ImmediateStackID + 1;

    for (const auto &event: vmeConfig.getEventConfigs())
    {
        switch (event->triggerCondition)
        {
            case TriggerCondition::Interrupt:
                {
                    u16 triggerReg = stacks::get_trigger_register(stackId);

                    u32 triggerVal = stacks::IRQNoIACK << stacks::TriggerTypeShift;
                    triggerVal |= (event->irqLevel - 1) & stacks::TriggerBitsMask;

                    if (auto ec = mvlc.writeRegister(triggerReg, triggerVal))
                        return ec;

                } break;

            InvalidDefaultCase;
        }

        stackId++;
    }

    return {};
}

std::error_code setup_mvlc(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger logger)
{
    logger("Disabling readout triggers");

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
    }

    logger("Setting up readout stacks");

    if (auto ec = setup_readout_stacks(mvlc, vmeConfig, logger))
    {
        logger(QString("Error setting up readout stacks: %1").arg(ec.message().c_str()));
        return ec;
    }

    logger("Setting up triggers");

    if (auto ec = setup_triggers(mvlc, vmeConfig))
        return ec;

    return {};
}

} // end namespace mvlc
} // end namespace mesytec
