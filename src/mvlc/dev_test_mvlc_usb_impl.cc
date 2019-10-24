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

#if 0
    // find by serial
    {
        for (unsigned serial = 1; serial <=2; serial++)
        {
            auto di = get_device_info_by_serial(std::to_string(serial));

            if (di)
                cout << "  Found device for serial " << serial << ", index=" << di.index << endl;
            else
                cout << "  Did not find a device for serial " << serial << endl;
        }
    }
#endif

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

    mvlc.setReadTimeout(Pipe::Command, 250);
    mvlc.setWriteTimeout(Pipe::Command, 250);

    if (auto ec = mvlc.connect())
    {
        assert(!mvlc.isConnected());
        cerr << "connect: " << ec.category().name() << ": " << ec.message() << endl;
        return 1;
    }
    assert(mvlc.isConnected());

    static const size_t MaxIterations = 25000;
    static const std::chrono::duration<int, std::milli> WaitInterval(0);
    size_t iteration = 0u;

    struct ErrorWithMessage
    {
        std::error_code ec;
        std::string msg;
    };

    try
    {
        for (iteration = 0; iteration < MaxIterations; iteration++)
        {
#if 1
            if (auto ec = mvlc.writeRegister(0x2000 + 512, iteration))
                throw ErrorWithMessage{ec, "writeRegister"};

            u32 regVal = 0u;
            if (auto ec = mvlc.readRegister(0x2000 + 512, regVal))
                throw ErrorWithMessage{ec, "readRegister"};

            assert(regVal == iteration);
#else
            u32 value = iteration % 0xFFFFu;
            if (value == 0) value = 1;

            if (auto ec = mvlc.vmeSingleWrite(0x0000601A, value,
                                              vme_address_modes::A32, VMEDataWidth::D16))
            {
                throw ec;
            }

            if (WaitInterval != std::chrono::duration<int>::zero())
            {
                std::this_thread::sleep_for(WaitInterval);
            }

            u32 result = 0u;

            if (auto ec = mvlc.vmeSingleRead(0x0000601A, result,
                                             vme_address_modes::A32, VMEDataWidth::D16))
            {
                throw ec;
            }

            assert(result == value);
#endif
        }
    }
    catch (const ErrorWithMessage &em)
    {
        const auto &ec = em.ec;
        const auto &msg = em.msg;

        cout << "Error from iteration " << iteration
            << ": " << ec.message()
            << " (" << ec.category().name() << ")"
            << ", extra_msg=" << msg
            << endl;

        auto buffer = mvlc.getResponseBuffer();
        log_buffer(std::cout, buffer.data(), buffer.size(), "last response buffer");

        u32 readQueueSize = 0u;
        auto impl = reinterpret_cast<usb::Impl *>(mvlc.getImpl());
        impl->getReadQueueSize(Pipe::Command, readQueueSize);
        cout << "Cmd Pipe Read Queue Size: " << readQueueSize << endl;

        if (readQueueSize > 0)
        {
            cout << "Attempting to read from Cmd Pipe...";

            size_t bytesTransferred = 0u;
            std::vector<u8> buffer(readQueueSize);
            auto ec = impl->read(Pipe::Command, buffer.data(), buffer.size(),
                                 bytesTransferred);

            cout << "Cmd read result: " << ec.message()
                << ", bytesTransferred=" << bytesTransferred
                << endl;

            for (size_t i=0; i < bytesTransferred / sizeof(u32); i++)
            {
                u32 value = reinterpret_cast<u32 *>(buffer.data())[i];

                printf("  0x%08x\n", value);
            }

            cout << "End of data from manual read attempt" << endl;
        }

        return 1;
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
