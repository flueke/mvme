#include "mvlc/trigger_io_scope.h"

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

void reader(
    mvlc::MVLC mvlc,
    const OsciSetup &setup,
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
                0xffff + addr, value,
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
            0xffff0000u, mvlc::vme_amods::A32, std::numeric_limits<u16>::max(), dest);

        if (mvlc::is_connection_error(ec))
            break;

        if (!dest.empty())
            buffers.enqueue(std::move(dest));
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
    assert(buffer.size() >= 2);

    if (buffer.size() < 2)
    {
        qDebug() << __PRETTY_FUNCTION__ << "got a short buffer";
        return {};
    }

    if (buffer[0] != data_format::Header)
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

    // FIXME: is overflow bit in the header or in the first data word?
    //bool overflow = 

    for (size_t i=1; i<buffer.size()-1; ++i)
    {
        const u32 word = buffer[i];
        const auto entry = extract_entry(word);

        qDebug("entry: addr=%u, time=%u, edge=%s",
               entry.address, entry.time, to_string(entry.edge));

        if (entry.address >= result.size())
            result.resize(entry.address + 1);

        auto &timeline = result[entry.address];
        timeline.push_back({entry.time, entry.edge});
    }

    return result;
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
