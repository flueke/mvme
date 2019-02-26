#include "mvlc/mvlc_dialog.h"
#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_usb_impl.h"
#include "mvlc/mvlc_util.h"
#include <cassert>
#include <iostream>

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::usb;

using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
    {
        Impl mvlcUSB(0);

        // connect
        if (auto ec = mvlcUSB.connect())
        {
            assert(!mvlcUSB.is_connected());
            cerr << "connect: " << ec.category().name() << ": " << ec.message() << endl;
            return 1;
        }
        assert(mvlcUSB.is_connected());

        // disconnect
        if (auto ec = mvlcUSB.disconnect())
        {
            assert(!mvlcUSB.is_connected());
            cerr << "disconnect: " << ec.category().name() << ": " << ec.message() << endl;
        }
        assert(!mvlcUSB.is_connected());

        // connect again
        if (auto ec = mvlcUSB.connect())
        {
            assert(!mvlcUSB.is_connected());
            cerr << "connect: " << ec.category().name() << ": " << ec.message() << endl;
            return 1;
        }
        assert(mvlcUSB.is_connected());
    }

    // Use MVLCObject and MVLCDialog to spam requests on the Command Pipe

    MVLCObject mvlc(make_mvlc_usb());

    if (auto ec = mvlc.connect())
    {
        assert(!mvlc.isConnected());
        cerr << "connect: " << ec.category().name() << ": " << ec.message() << endl;
        return 1;
    }
    assert(mvlc.isConnected());

    MVLCDialog dlg(&mvlc);
    static const size_t MaxIterations = 100000;
    size_t iteration = 0u;

    try
    {
        for (iteration = 0; iteration < MaxIterations; iteration++)
        {
#if 0
            if (auto ec = dlg.writeRegister(0x2000 + 512, iteration))
                throw ec;

            u32 regVal = 0u;
            if (auto ec = dlg.readRegister(0x2000 + 512, regVal))
                throw ec;

            assert(regVal == iteration);
#else
            u32 value = iteration % 0xFFFFu;
            if (value == 0) value = 1;

            if (auto ec = dlg.vmeSingleWrite(0x0000601A, value,
                                             AddressMode::A32, VMEDataWidth::D16))
            {
                throw ec;
            }

            u32 result = 0u;

            if (auto ec = dlg.vmeSingleRead(0x0000601A, result,
                                             AddressMode::A32, VMEDataWidth::D16))
            {
                throw ec;
            }

            assert(result == value);
#endif
        }
    }
    catch (const std::error_code &ec)
    {
        cout << "Error from iteration " << iteration
            << ": " << ec.message()
            << " (" << ec.category().name() << ")"
            << endl;

        auto buffer = dlg.getResponseBuffer();
        log_buffer(std::cout, buffer.data(), buffer.size(), "last response buffer");

        return 1;
    }

    cout << "Did " << MaxIterations << " test iterations without error" << endl;

    return 0;
}
