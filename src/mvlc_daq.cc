#include "mvlc_daq.h"
#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_trigger_io.h"
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
                    u16 timerId = timersInUse;

                    logger(QSL("  Event %1: Stack %2, Timer %3")
                           .arg(event->objectName()).arg(stackId).arg(timerId));

                    if (!event->triggerOptions.contains("mvlc.timer_base"))
                    {
                        logger("No trigger timer base given");
                        return make_error_code(MVLCErrorCode::ReadoutSetupError);
                    }

                    if (!event->triggerOptions.contains("mvlc.timer_period"))
                    {
                        logger("No trigger timer period given");
                        return make_error_code(MVLCErrorCode::ReadoutSetupError);
                    }

                    stacks::TimerBaseUnit timerBase = timer_base_unit_from_string(
                        event->triggerOptions["mvlc.timer_base"].toString());

                    unsigned timerValue = event->triggerOptions["mvlc.timer_period"].toUInt();

                    std::chrono::milliseconds period(
                        event->triggerOptions["mvlc.timer_period"].toUInt());

                    struct Write { u16 reg; u16 val; };

                    std::vector<Write> writes;

                    // target selection: lvl=0, unit=0..3 (timers)
                    writes.push_back({ 0x0200, timerId });

                    writes.push_back({ 0x0302, static_cast<u16>(timerBase) }); // timer period base
                    writes.push_back({ 0x0304, 0 }); // delay
                    writes.push_back({ 0x0306, static_cast<u16>(period.count()) }); // timer period value

                    // target selection: lvl=3, unit=timerId (stack units 0-3, triggering stacks 1-4
                    writes.push_back({ 0x0200, static_cast<u16>(0x0300 | timerId) });
                    writes.push_back({ 0x0380, timerId });  // connect to our timer
                    writes.push_back({ 0x0300, 1 });        // activate stack output
                    writes.push_back({ 0x0302, stackId });  // send output to our stack

                    // Note: the connection from the L0 timer units to the L3
                    // stack units via L2 is not set explicitly here for now.
                    // The setup works because the default setup of the L2 LUTs
                    // is a global OR.

                    //// testing activation of output 0 if any of the configured
                    //// timers activates
                    //// LUT on level 2
                    //u16 level = 2;
                    //u16 unit  = 0;
                    //writes.push_back({ 0x0200, static_cast<u16>(((level << 8) | unit))  });
                    //writes.push_back({ 0x0380, timerId });  // connect to our timer



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

static void write(MVLCObject &mvlc, u16 address, u16 value)
{
    if (auto ec = mvlc.writeRegister(address, value))
        throw ec;
}

static void select(MVLCObject &mvlc, u8 level, u8 unit)
{
    write(mvlc, 0x0200, static_cast<u16>(((level << 8) | unit)));
}

std::error_code do_base_setup(MVLCObject &mvlc, Logger logger)
{
    trigger_io::TriggerIO setup = {};

    // NIM 0-3: busy out
    for (size_t i = 0; i < 4; i++)
    {
#if 0
        auto &io = setup.l0.ioNIM[i];

        io = {};
        io.direction = trigger_io::IO::Direction::out;
        io.activate = true;
#endif

        int unitoffset = 16;
        select(mvlc, 0, unitoffset + i);
        write(mvlc, 0x0300 + 10, 1); // dir: output
        write(mvlc, 0x0300 + 16, 1); // activate
    }

    // NIM 4,5: unused

    // NIM 6-9: Trigger inputs
    for (size_t i = 6; i < 10; i++)
    {
#if 0
        auto &io = setup.l0.ioNIM[i];
        io = {};
        io.direction = trigger_io::IO::Direction::in;
        io.activate = true;
#endif
        int unitoffset = 16;
        select(mvlc, 0, unitoffset + i);
        write(mvlc, 0x0300 + 10, 0); // dir: inpput
        write(mvlc, 0x0300 + 16, 1); // activate
    }

    // NIM 10-13
    for (size_t i = 10; i < 14; i++)
    {
#if 0
        auto &io = setup.l0.ioNIM[i];
        io = {};
        io.direction = trigger_io::IO::Direction::out;
        io.activate = true;
#endif
        int unitoffset = 16;
        select(mvlc, 0, unitoffset + i);
        write(mvlc, 0x0300 + 10, 1); // dir: output
        write(mvlc, 0x0300 + 16, 1); // activate
    }

    // ECL 2: Trigger 0 out
    {
#if 0
        auto &io = setup.l0.ioECL[2];
        io = {};
        io.direction = trigger_io::IO::Direction::out;
        io.activate = true;
#endif
        select(mvlc, 0, 32);
        write(mvlc, 0x0300 + 16, 1); // activate
    }

    // These refer to the same hardware devices.
    setup.l3.ioNIM = setup.l0.ioNIM;
    //setup.l3.ioECL = setup.l0.ioECL;

    // L1.1: OR over inputs 6-9 which are connected to L1.1 pins 0-3
    //       The OR goes to L1.1 output bit 0
    {
        trigger_io::LUT_RAM ram = {};
        u8 mask = 0b1111u;

        for (u8 addr = 0; addr < 64; addr++)
        {
            if (addr & mask)
            {
                trigger_io::set(ram, addr, 0b1);
            }
        }

        select(mvlc, 1, 1);
        write_lut_ram(mvlc, ram, 0x300);
    }

    // L1.0: OR over inputs 0-3 which are connected to L1.0 pins 0-3
    //       The OR goes to L1.0 output bit 0
    {
        trigger_io::LUT_RAM ram = {};
        u8 mask = 0b1111u;

        for (u8 addr = 0; addr < 64; addr++)
        {
            if (addr & mask)
            {
                trigger_io::set(ram, addr, 0b1);
            }
        }

        select(mvlc, 1, 0);
        write_lut_ram(mvlc, ram, 0x300);
    }

    // L1.3: pass through of input 0 to L1 output 0
    //                   and input 3 to L1 output 1
    {
        trigger_io::LUT_RAM ram = {};
        u8 mask = 0b1u;
        u8 mask1 = 0b1u << 3;

        for (u8 addr = 0; addr < 64; addr++)
        {
            if (addr & mask)
            {
                trigger_io::set(ram, addr, 0b1);
            }

            if (addr & mask1)
            {
                trigger_io::set(ram, addr, 0b1 << 1);
            }
        }

        select(mvlc, 1, 3);
        write_lut_ram(mvlc, ram, 0x300);
    }

    // => or of NIM6-9 is available at L1 output bit 0
    //    or of NIM0-3 is available at L1 output bit 1

    // Setup L2.0.strobeGG and connect Trig In from (L1 out 0) to L2.0.strobeGG.
    {
        setup.l2.luts[0].strobedOutputs.set();
        // TODO: connect input_strobe
        // connect l1.out.1 to L2.0.0 (TrigIn)
        // connect(l0.out.12 to L2.0.1 (StackBusy)
        // build an or of L2.0.0 and L2.0.1, negate it and use it for TrigOut 10-13 ECL2 out

        trigger_io::LUT_RAM ram = {};
        u8 mask = 0b11u;

        for (u8 addr = 0; addr < 64; addr++)
        {
            if (addr & mask)
            {
                trigger_io::set(ram, addr, 0b1);
            }
        }

        select(mvlc, 2, 0);
        write_lut_ram(mvlc, ram, 0x300);
        //write(mvlc, 0x386,
        // XXX: leftoff
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

#if 0
    if (auto ec = do_base_setup(mvlc, logger))
    {
        logger(QString("Error doing MVLC base setup: %1")
               .arg(ec.message().c_str()));
        return ec;
    }
#endif

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
