#include "mvlc/mvlc_dialog.h"
#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_impl_usb.h"
#include "mvlc/mvlc_util.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <regex>

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::usb;

using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
    // list devices
    {
        auto dil = get_device_info_list();

        cout << "Devices:" << endl;

        for (const auto &di: dil)
        {
            static const std::regex reDescr("MVLC");
            assert(std::regex_search(di.description, reDescr));

            cout << "  index=" << di.index
                << ", descr=" << di.description
                << ", serial=" << di.serial
                << endl;
        }
    }

    cout << endl;

    // find by serial
    {
        for (unsigned serial = 1; serial <=2; serial++)
        {
            auto di = get_device_info_by_serial(serial);
            if (di)
                cout << "  Found device for serial " << serial << ", index=" << di.index << endl;
            else
                cout << "  Did not find a device for serial " << serial << endl;
        }
    }

    cout << endl << "Connect/Disconnect tests using the first device..." << endl;

    // connect, disconnect, connect
    {
        Impl mvlcUSB(0);

        // connect
        if (auto ec = mvlcUSB.connect())
        {
            assert(!mvlcUSB.isConnected());
            cerr << "connect: " << ec.category().name() << ": " << ec.message() << endl;
            return 1;
        }
        assert(mvlcUSB.isConnected());

        // disconnect
        if (auto ec = mvlcUSB.disconnect())
        {
            assert(!mvlcUSB.isConnected());
            cerr << "disconnect: " << ec.category().name() << ": " << ec.message() << endl;
        }
        assert(!mvlcUSB.isConnected());

        // connect again
        if (auto ec = mvlcUSB.connect())
        {
            assert(!mvlcUSB.isConnected());
            cerr << "connect: " << ec.category().name() << ": " << ec.message() << endl;
            return 1;
        }
        assert(mvlcUSB.isConnected());
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

    static const size_t MaxIterations = 250000;
    static const std::chrono::duration<int, std::milli> WaitInterval(0);
    size_t iteration = 0u;

    try
    {
        for (iteration = 0; iteration < MaxIterations; iteration++)
        {
#if 1
            if (auto ec = mvlc.writeRegister(0x2000 + 512, iteration))
                throw ec;

            u32 regVal = 0u;
            if (auto ec = mvlc.readRegister(0x2000 + 512, regVal))
                throw ec;

            assert(regVal == iteration);
#else
            u32 value = iteration % 0xFFFFu;
            if (value == 0) value = 1;

            if (auto ec = mvlc.vmeSingleWrite(0x0000601A, value,
                                              AddressMode::A32, VMEDataWidth::D16))
            {
                throw ec;
            }

            if (WaitInterval != std::chrono::duration<int>::zero())
            {
                std::this_thread::sleep_for(WaitInterval);
            }

            u32 result = 0u;

            if (auto ec = mvlc.vmeSingleRead(0x0000601A, result,
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

        auto buffer = mvlc.getResponseBuffer();
        log_buffer(std::cout, buffer.data(), buffer.size(), "last response buffer");

        return 1;
    }

    cout << "Did " << MaxIterations << " test iterations without error" << endl;

    return 0;
}
