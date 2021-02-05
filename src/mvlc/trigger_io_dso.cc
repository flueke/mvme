#include "mvlc/trigger_io_dso.h"
#include "mesytec-mvlc/mvlc_constants.h"
#include "mesytec-mvlc/mvlc_dialog.h"
#include <chrono>

#ifndef __WIN32
#include <sys/prctl.h>
#endif

#include <QDebug>

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_dso
{

namespace
{

static const unsigned UnitNumber = 48;
static const u32 DSOReadAddress = mvlc::SelfVMEAddress + 4;

bool is_fatal(const std::error_code &ec)
{
    return (ec == mvlc::ErrorType::ConnectionError
            || ec == mvlc::ErrorType::ProtocolError);
}

void self_write_throw(mvlc::MVLCDialog &mvlc, u32 addr, u16 value)
{
    if (auto ec = mvlc.vmeWrite(
            mvlc::SelfVMEAddress + addr, value,
            mvlc::vme_amods::A32, mvlc::VMEDataWidth::D16))
        throw ec;
}

std::error_code start_dso(mvlc::MVLCDialog &mvlc, DSOSetup setup)
{
    try
    {
        self_write_throw(mvlc, 0x0200, UnitNumber); // select DSO unit
        self_write_throw(mvlc, 0x0300, setup.preTriggerTime);
        self_write_throw(mvlc, 0x0302, setup.postTriggerTime);
        self_write_throw(mvlc, 0x0304, setup.nimTriggers.to_ulong());
        self_write_throw(mvlc, 0x0308, setup.irqTriggers.to_ulong());
        self_write_throw(mvlc, 0x0306, 1); // start capturing
    }
    catch (const std::error_code &ec)
    {
        return ec;
    }

    return {};
}

std::error_code stop_dso(mvlc::MVLCDialog &mvlc)
{
    try
    {
        self_write_throw(mvlc, 0x0200, UnitNumber); // select DSO unit
        self_write_throw(mvlc, 0x0306, 0); // stop the DSO
    }
    catch (const std::error_code &ec)
    {
        return ec;
    }

    return {};
}

std::error_code read_dso(mvlc::MVLCDialog &mvlc, std::vector<u32> &dest)
{
    // block read
    auto ec = mvlc.vmeBlockRead(
        DSOReadAddress, mvlc::vme_amods::MBLT64,
        std::numeric_limits<u16>::max(), dest);

    if (is_fatal(ec))
        return ec;

    // readout reset
    return mvlc.vmeWrite(
        mvlc::SelfVMEAddress + 0x6034, 1,
        mvlc::vme_amods::A32, mvlc::VMEDataWidth::D16);
}
}

std::error_code acquire_dso_sample(
    mvlc::MVLC mvlc, DSOSetup setup,
    std::vector<u32> &dest,
    std::atomic<bool> &cancel,
    const std::chrono::milliseconds &timeout)
{
    auto tStart = std::chrono::steady_clock::now();

    // Stop the stack error poller so that it doesn't read our samples off the
    // command pipe.
    auto errPollerLock = mvlc.suspendStackErrorPolling();

    // To enforce that no other communication takes places on the command pipe
    // while the DSO is active we lock the command pipe here, then create a
    // local MVLCDialog instance which works directly on the underlying
    // low-level MVLCBasicInterface and thus doesn't do any locking itself.
    //
    // Note: any stack errors accumulated in the local MVLCDialog instance are
    // discarded. For total correctness the stack error counters would have to
    // be updated with the locally accumulated error counts.

    auto cmdLock = mvlc.getLocks().lockCmd();
    mvlc::MVLCDialog dlg(mvlc.getImpl());

    // start, then read until we get a sample, stop

    if (auto ec = start_dso(dlg, setup))
        return ec;

    dest.clear();
    bool timed_out = false;

    while (!cancel && dest.size() <= 2 && !timed_out)
    {
        auto ec = read_dso(dlg, dest);

        if (is_fatal(ec))
            return ec;

        auto elapsed = std::chrono::steady_clock::now() - tStart;

        if (elapsed >= timeout)
            timed_out = true;
    }

    if (auto ec = stop_dso(dlg))
        return ec;

    // Read and throw away any additional samples (needed to clear the command
    // pipe). Do this even if we got canceled as a sample might have become
    // available in the meantime.
    std::vector<u32> tmpBuffer;

    do
    {
        tmpBuffer.clear();

        auto ec = read_dso(dlg, tmpBuffer);

        if (is_fatal(ec))
            return ec;

    } while (tmpBuffer.size() > 2);

    if (timed_out && dest.size() <= 2)
        return make_error_code(std::errc::timed_out);

    return {};
}

Snapshot fill_snapshot_from_mvlc_buffer(const std::vector<u32> &buffer)
{
    using namespace std::chrono_literals;

    if (buffer.size() < 2 + 2)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "got a short buffer";
        return {};
    }

    if ((buffer[0] >> 24) != 0xF3
        || (buffer[1] >> 24) != 0xF5)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "invalid frame and block headers";
        return {};
    }

    if (buffer[2] != data_format::Header)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "invalid Header";
        return {};
    }

    if (buffer[buffer.size()-1] != data_format::EoE)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "invalid EoE";
        return {};
    }

    Snapshot result;
    result.reserve(NIM_IO_Count + Level0::IRQ_Inputs_Count);

    for (size_t i=3; i<buffer.size()-1; ++i)
    {
        const u32 word = buffer[i];
        const auto entry = extract_dso_entry(word);

        //qDebug("entry: addr=%u, time=%u, edge=%s",
        //       entry.address, entry.time, to_string(entry.edge));

        if (entry.address >= result.size())
            result.resize(entry.address + 1);

        assert(entry.address < result.size());

        auto &timeline = result[entry.address];
        timeline.push_back({std::chrono::nanoseconds(entry.time), entry.edge});

        // This is the FIFO overflow marker: the first sample of each channel
        // will have the time set to 1 (the first samples time is by definition
        // 0 so no information is lost). The code replaces the 1 with a 0 to
        // make plotting work just like for the non-overflow case.
        // TODO: use the overflow information somewhere? Keep the 1 and handle
        // it in some upper layer?
        if (timeline.size() == 1)
            if (timeline[0].time == 1ns)
                timeline[0].time = 0ns;
    }

    return result;
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
