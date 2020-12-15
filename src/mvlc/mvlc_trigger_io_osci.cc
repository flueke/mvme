#include "mvlc/mvlc_trigger_io_osci.h"

#ifndef __WIN32
#include <sys/prctl.h>
#endif

#include <QDebug>

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_osci
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

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
