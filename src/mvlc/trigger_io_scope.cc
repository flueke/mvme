#include "mvlc/trigger_io_scope.h"
#include "mesytec-mvlc/mvlc_constants.h"

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
        qDebug() << __PRETTY_FUNCTION__
            << "pre=" << setup.preTriggerTime
            << ", post=" << setup.postTriggerTime
            << ", trigChans=" << setup.triggerChannels.to_ulong();

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

std::error_code read_scope(mvlc::MVLC mvlc, std::vector<u32> &dest)
{
    return mvlc.vmeBlockRead(
        mvlc::SelfVMEAddress, mvlc::vme_amods::MBLT64,
        std::numeric_limits<u16>::max(), dest);
}

void reader(
    mvlc::MVLC mvlc,
    ScopeSetup setup,
    mvlc::ThreadSafeQueue<std::vector<u32>> &buffers,
    std::atomic<bool> &quit,
    std::promise<std::error_code> ec_promise)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"osci_reader",0,0,0);
#endif

    std::error_code ec = {};

    auto write = [&mvlc] (u16 addr, u16 value)
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
        qDebug() << __PRETTY_FUNCTION__ << "error setting up trigger_io osci:" << ec.message().c_str();
        ec_promise.set_value(ec);
        return;
    }

    qDebug() << __PRETTY_FUNCTION__ << "osci_reader entering loop";

    while (!quit)
    {
        std::vector<u32> dest;

        ec = mvlc.vmeBlockRead(
            mvlc::SelfVMEAddress, mvlc::vme_amods::A32, std::numeric_limits<u16>::max(), dest);

        //qDebug() << __PRETTY_FUNCTION__ << ec.message().c_str();

        if (mvlc::is_connection_error(ec))
            break;

        if (!dest.empty())
            buffers.enqueue(std::move(dest));
    }

    try
    {
        write(0x0306, 0); // stop capturing
    }
    catch (const std::error_code &ec)
    {
        qDebug() << __PRETTY_FUNCTION__ << "error stopping trigger_io osci:" << ec.message().c_str();
        ec_promise.set_value(ec);
        return;
    }

    qDebug() << __PRETTY_FUNCTION__ << "osci_reader left loop";

    ec_promise.set_value(ec);
}

namespace
{
struct LineEntry
{
    u8 address;
    Edge edge;
    u16 time;
};

inline LineEntry extract_entry(u32 dataWord)
{
    LineEntry result;
    result.address = (dataWord >> data_format::AddressShift) & data_format::AddressMask;
    result.edge = Edge((dataWord >> data_format::EdgeShift) & data_format::EdgeMask);
    result.time = (dataWord >> data_format::TimeShift) & data_format::TimeMask;
    return result;
}
}

Snapshot fill_snapshot(const std::vector<u32> &buffer)
{
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
        timeline.push_back({entry.time, entry.edge});

        // This is the FIFO overflow marker: the first sample of each channel
        // will have the time set to 1 (the first samples time is by definition
        // 0 so no information is lost).
        if (timeline.size() == 1)
            if (timeline[0].time == 1)
                timeline[0].time = 0;
    }

    return result;
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
