#include "mvlc/mvlc_daq.h"
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

// Builds, uploads and sets up the readout stack for each event in the vme
// config.
void setup_readout_stacks(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger logger)
{
    // Stack0 is reserved for immediate exec
    u8 stackId = stacks::ImmediateStackID + 1;

    // 1 word gap between immediate stack and first readout stack
    u16 uploadOffset = stacks::ImmediateStackReservedWords + 1;

    QVector<u32> responseBuffer;

    for (const auto &event: vmeConfig.getEventConfigs())
    {
        if (stackId >= stacks::StackCount)
            throw std::runtime_error("number of available stacks exceeded");

        auto readoutScript = build_event_readout_script(event);
        auto stackContents = build_stack(readoutScript, DataPipe);

        u16 uploadAddress = stacks::StackMemoryBegin + uploadOffset * 4;
        u16 endAddress    = uploadAddress + stackContents.size() * 4;

        if (endAddress >= stacks::StackMemoryEnd)
            throw std::runtime_error("stack memory exceeded");

        auto uploadCommands = build_upload_command_buffer(stackContents, uploadAddress);

        if (auto ec = mvlc.mirrorTransaction(uploadCommands, responseBuffer))
            throw std::system_error(ec);

        u16 offsetRegister = stacks::get_offset_register(stackId);

        if (auto ec = mvlc.writeRegister(offsetRegister, uploadAddress & stacks::StackOffsetBitMaskBytes))
            throw std::system_error(ec);

        stackId++;
        // again leave a 1 word gap between stacks
        uploadOffset += stackContents.size() + 1;
    }
}

void setup_triggers(MVLCObject &mvlc, const VMEConfig &vmeConfig)
{
    u8 stackId = stacks::ImmediateStackID + 1;

    for (const auto &event: vmeConfig.getEventConfigs())
    {
        switch (event->triggerCondition)
        {
            case TriggerCondition::Interrupt:
                {
                    u16 triggerReg = stacks::get_trigger_register(stackId);

                    u32 triggerVal = stacks::IRQ << stacks::TriggerTypeShift;
                    triggerVal |= (event->irqLevel - 1) & stacks::TriggerBitsMask;

                    if (auto ec = mvlc.writeRegister(triggerReg, triggerVal))
                        throw std::system_error(ec);

                } break;

            InvalidDefaultCase;
        }

        stackId++;
    }
}

void setup_mvlc(MVLC_VMEController &mvlc, const VMEConfig &vmeConfig, Logger logger)
{
    auto &mvlcObj = *mvlc.getMVLCObject();

    disable_all_triggers(mvlcObj);
    setup_readout_stacks(mvlcObj, vmeConfig, logger);
    setup_triggers(mvlcObj, vmeConfig);
}

} // end namespace mvlc
} // end namespace mesytec
