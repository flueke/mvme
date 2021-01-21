#include "mvlc/trigger_io_scope.h"
#include "mesytec-mvlc/mvlc_constants.h"
#include <chrono>

#ifndef __WIN32
#include <sys/prctl.h>
#endif

#include <QDebug>

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_scope
{

static const unsigned UnitNumber = 48;

std::error_code start_scope(mvlc::MVLC mvlc, ScopeSetup setup)
{
    auto write = [&mvlc] (u32 addr, u16 value)
    {
        if (auto ec = mvlc.vmeWrite(
                mvlc::SelfVMEAddress + addr, value,
                mvlc::vme_amods::A32, mvlc::VMEDataWidth::D16))
            throw ec;
    };

    try
    {
        write(0x0200, UnitNumber); // select osci unit
        write(0x0300, setup.preTriggerTime);
        write(0x0302, setup.postTriggerTime);
        write(0x0304, setup.triggerChannels.to_ulong());
        write(0x0306, 1); // start capturing
    }
    catch (const std::error_code &ec)
    {
        return ec;
    }

    return {};
}

std::error_code stop_scope(mvlc::MVLC mvlc)
{
    u16 addr = 0x0306;
    u16 value = 0;

    return mvlc.vmeWrite(
        mvlc::SelfVMEAddress + addr, value,
        mvlc::vme_amods::A32, mvlc::VMEDataWidth::D16);
}

namespace
{
bool is_fatal(const std::error_code &ec)
{
    return (ec == mvlc::ErrorType::ConnectionError
            || ec == mvlc::ErrorType::ProtocolError);
}
}

std::error_code read_scope(mvlc::MVLC mvlc, std::vector<u32> &dest)
{
    // XXX: this doesn't skip over stack error notification buffers that
    // might arrive on the command pipe.

    // block read from the mvlc
    auto ec = mvlc.vmeBlockRead(
        mvlc::SelfVMEAddress, mvlc::vme_amods::MBLT64,
        std::numeric_limits<u16>::max(), dest);

    if (is_fatal(ec))
        return ec;

    // readout reset
    return mvlc.vmeWrite(
        mvlc::SelfVMEAddress + 0x6034, 1,
        mvlc::vme_amods::A32, mvlc::VMEDataWidth::D16);
}

std::error_code acquire_scope_sample(
    mvlc::MVLC mvlc, ScopeSetup setup,
    std::vector<u32> &dest, std::atomic<bool> &cancel)
{
    // Stop the stack error poller so that it doesn't read our samples off the
    // command pipe.
    auto errPollerLock = mvlc.suspendStackErrorPolling();

    // start, read until we get a sample, stop
    if (auto ec = start_scope(mvlc, setup))
        return ec;

    dest.clear();

    while (!cancel && dest.size() <= 2)
    {
        auto ec = read_scope(mvlc, dest);

        if (is_fatal(ec))
            return ec;
    }

    if (auto ec = stop_scope(mvlc))
        return ec;

    // Read and throw away any additional samples (needed to clear the command
    // pipe). Do this even if we got canceled as a sample might have become
    // available in the meantime.
    std::vector<u32> tmpBuffer;

    do
    {
        tmpBuffer.clear();

        auto ec = read_scope(mvlc, tmpBuffer);

        if (is_fatal(ec))
            return ec;
    } while (tmpBuffer.size() > 2);

    return {};
}

namespace
{
struct BufferEntry
{
    u8 address;
    Edge edge;
    u16 time;
};

inline BufferEntry extract_entry(u32 dataWord)
{
    BufferEntry result;
    result.address = (dataWord >> data_format::AddressShift) & data_format::AddressMask;
    result.edge = Edge((dataWord >> data_format::EdgeShift) & data_format::EdgeMask);
    result.time = (dataWord >> data_format::TimeShift) & data_format::TimeMask;
    return result;
}
}

Snapshot fill_snapshot_from_mvlc_buffer(const std::vector<u32> &buffer)
{
    using namespace std::chrono_literals;

    if (buffer.size() < 2 + 2)
    {
        qDebug() << __PRETTY_FUNCTION__ << "got a short buffer";
        return {};
    }

    if ((buffer[0] >> 24) != 0xF3
        || (buffer[1] >> 24) != 0xF5)
    {
        qDebug() << __PRETTY_FUNCTION__ << "invalid frame and block headers";
        return {};
    }

    if (buffer[2] != data_format::Header)
    {
        qDebug() << __PRETTY_FUNCTION__ << "invalid Header";
        return {};
    }

    if (buffer[buffer.size()-1] != data_format::EoE)
    {
        qDebug() << __PRETTY_FUNCTION__ << "invalid EoE";
        return {};
    }

    Snapshot result;
    result.reserve(NIM_IO_Count);

    for (size_t i=3; i<buffer.size()-1; ++i)
    {
        const u32 word = buffer[i];
        const auto entry = extract_entry(word);

        qDebug("entry: addr=%u, time=%u, edge=%s",
               entry.address, entry.time, to_string(entry.edge));

        if (entry.address >= result.size())
            result.resize(entry.address + 1);

        auto &timeline = result[entry.address];
        timeline.push_back({std::chrono::nanoseconds(entry.time), entry.edge});

        // This is the FIFO overflow marker: the first sample of each channel
        // will have the time set to 1 (the first samples time is by definition
        // 0 so no information is lost). The code replaces the 1 with a 0 to
        // make plotting work just like for the non-overflow case.
        // TODO: use the overflow information somewhere?
        if (timeline.size() == 1)
            if (timeline[0].time == 1ns)
                timeline[0].time = 0ns;
    }

    return result;
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
