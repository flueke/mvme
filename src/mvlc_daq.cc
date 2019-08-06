#include "mvlc_daq.h"
#include "mvlc/mvlc_error.h"
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
                    u16 timerId = timersInUse;

                    logger(QSL("  Event %1: Stack %2, Timer %3")
                           .arg(event->objectName()).arg(stackId).arg(timerId));

                    if (!event->triggerOptions.contains("mvlc.timer_period"))
                    {
                        logger("No trigger period given");
                        return make_error_code(MVLCErrorCode::ReadoutSetupError);
                    }

                    std::chrono::milliseconds period(
                        event->triggerOptions["mvlc.timer_period"].toUInt());

                    struct Write { u16 reg; u16 val; };

                    std::vector<Write> writes;

                    // target selection: lvl=0, unit=0..3 (timers)
                    writes.push_back({ 0x0200, timerId });

                    writes.push_back({ 0x0302, (u16)stacks::TimerUnits::ms }); // timer period base
                    writes.push_back({ 0x0304, 0 }); // delay
                    writes.push_back({ 0x0306, (u16)period.count() }); // timer period value

                    // target selection: lvl=3, unit=timerId
                    writes.push_back({ 0x0200, static_cast<u16>(0x0300 | timerId) });
                    writes.push_back({ 0x0380, timerId });  // connect to our timer
                    writes.push_back({ 0x0300, 1 });        // activate stack output
                    writes.push_back({ 0x0302, stackId });  // send output to our stack

                    // trigger setup
                    writes.push_back({ stacks::get_trigger_register(stackId),
                        stacks::External << stacks::TriggerTypeShift });

                    for (const auto w: writes)
                    {
#if 0
                        logger(QString("    0x%1 -> 0x%2")
                               .arg(w.reg, 4, 16, QLatin1Char('0'))
                               .arg(w.val, 4, 16, QLatin1Char('0')));
#endif

                        if (auto ec = mvlc.writeRegister(w.reg, w.val))
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

std::error_code setup_mvlc(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger logger)
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

    logger("Enabling triggers");

    if (auto ec = enable_triggers(mvlc, vmeConfig, logger))
        return ec;

    return {};
}

} // end namespace mvlc
} // end namespace mesytec
