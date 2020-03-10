#include <iostream>
#include <getopt.h>
#include "mvlc_constants.h"
#include "mvlc_factory.h"
#include "mvlc_dialog.h"

using std::cerr;
using std::cout;
using std::endl;

using namespace mesytec::mvlc;

// --usb            first device
// --usb 0          index
// --usb foobar     serial
// --eth mvlc-0007  hostname

static struct option long_options[] =
{
    { "eth", required_argument, 0, 0 },
    { "usb", no_argument, 0, 0 },
    { "usb-index", required_argument, 0, 0 },
    { "usb-serial", required_argument, 0, 0 },
    { "help", no_argument, 0, 'h' },
    { 0, 0, 0, 0 },
};

int main(int argc, char *argv[])
{
    std::function<MVLC ()> factory;

    while (true)
    {
        int option_index = 0;
        int c = getopt_long(argc, argv, "h", long_options, &option_index);

        if (c == -1)
            break;

        if (c == 'h')
        {
            cout << "Usage: " << argv[0] << " --eth <hostname> | --usb | --usb-index <index> | --usb-serial <serial>" << endl;
            return 0;
        }

        if (c != 0)
            continue;

        std::string opt_name = long_options[option_index].name;
        std::string opt_arg = optarg ? std::string(optarg) : std::string{};

        if (opt_name == "eth")
        {
            std::string host(opt_arg);
            factory = [host] () -> MVLC
            {
                cerr << "Using host=" << host << endl;
                return make_mvlc_eth(host);
            };
        }
        else if (opt_name == "usb")
        {
            factory = [] () -> MVLC
            {
                cerr << "Using first usb device" << endl;
                return make_mvlc_usb();
            };
        }
        else if (opt_name == "usb-index")
        {
            auto usbIndex = std::stoul(opt_arg);
            factory = [usbIndex] () -> MVLC
            {
                cerr << "Using usbIndex=" << usbIndex << endl;
                return make_mvlc_usb(usbIndex);
            };
        }
        else if (opt_name == "usb-serial")
        {
            auto usbSerial = optarg;
            factory = [usbSerial] () -> MVLC
            {
                cerr << "Using usbSerial=" << usbSerial << endl;
                return make_mvlc_usb(usbSerial);
            };
        }
    }

    if (!factory)
        return 1;

    auto mvlc = factory();

    if (auto ec = mvlc.connect())
    {
        cerr << "Error connecting to MVLC: " << ec.message() << endl;
        return 1;
    }

    cout << "connectionInfo: " << mvlc.connectionInfo() << endl;

    u32 hardware_id = 0u, firmware_revision = 0u;

    cout << std::showbase;

    if (!mvlc.readRegister(registers::hardware_id, hardware_id))
        cout << "mvlc hardware_id: " << std::hex << hardware_id << std::dec << endl;

    if (!mvlc.readRegister(registers::firmware_revision, firmware_revision))
        cout << "mvlc firmware_revision: " << std::hex << firmware_revision << std::dec << endl;

    return 0;
}
