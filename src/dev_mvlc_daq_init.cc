#include <cassert>
#include <iostream>
#include <QCoreApplication>

#include "mvlc_daq.h"
#include "mvlc/mvlc_dialog_util.h"
#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_vme_controller.h"
#include "vme_daq.h"

using namespace mesytec::mvlc;
using std::cout;
using std::cerr;
using std::endl;


// Dev tool to test VMEConfig based initialization of the MVLC.
// Input:
//  - hostname/ip-address/"usb" (usb just connects to the first mvlc found)
//  - filename of vmeconfig. the controller type specified in the vme config is
//  ignored and the mvlc is used instead.

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc != 3)
    {
        cerr << "Invalid arguments given." << endl;
        cerr << "Usage: " << argv[0] << " <hostname|ip-address|\"usb\"> <vmeconfig.vme>" << endl;
        return 1;
    }

    std::unique_ptr<AbstractImpl> impl;

    if (argv[1] == std::string("usb"))
        impl = make_mvlc_usb();
    else
        impl = make_mvlc_eth(argv[1]);

    assert(impl);

    std::unique_ptr<VMEConfig> vmeConfig;

    {
        auto readResult = read_vme_config_from_file(argv[2]);

        if (!readResult.first)
        {
            cerr << "Error reading vme config from " << argv[2] << ": "
                << readResult.second.toStdString() << endl;
            return 1;
        }

        vmeConfig = std::move(readResult.first);
    }

    assert(vmeConfig);

    auto logger = [](const QString &msg)
    {
        cout << "> " << msg.toStdString() << endl;
    };

    // takes ownership of the implementation object
    MVLCObject mvlc(std::move(impl));

    if (auto ec = mvlc.connect())
    {
        cout << "Error connecting to MVLC: " << ec.message() << endl;
        return 1;
    }

    try
    {
        cout << "Setting MVLC multicast address" << endl;
        auto ec = setup_mvlc_stage1(mvlc, *vmeConfig, logger);

        if (ec)
        {
            cout << "setup_mvlc_stage1() returned an error code: " << ec.message() << endl;
            return 1;
        }

        {
            cout << "Running DAQ init sequence..." << endl;

            // does not take ownership. just a wrapper implementing the VMEController interface
            MVLC_VMEController mvlcCtrl(&mvlc);

            vme_daq_init(vmeConfig.get(), &mvlcCtrl, logger);

            cout << "DAQ init sequence done." << endl;
            cout << "Setting up MVLC stacks and triggers" << endl;
        }

        ec = setup_mvlc_stage2(mvlc, *vmeConfig, logger);

        if (ec)
        {
            cout << "setup_mvlc_stage2() returned an error code: " << ec.message() << endl;
            return 1;
        }
    }
    catch (const std::runtime_error &e)
    {
        cout << "exception from setup_mvlc(): " << e.what() << endl;
        return 1;
    }
    catch (const vme_script::ParseError &e)
    {
        cout << "vme_script::ParseError from setup_mvlc(): "
            << e.what().toStdString() << endl;
        return 1;
    }

    cout << "setup_mvlc() done, no errors" << endl;

    cout << "Reading stack info..." << endl;

    u8 stackId = stacks::FirstReadoutStackID;

    for (const auto &eventConfig: vmeConfig->getEventConfigs())
    {
        cout << "* Event " << eventConfig->objectName().toStdString() << endl;
        auto readResult = read_stack_info(mvlc, stackId);

        if (readResult.second)
        {
            cout << "  Error reading stack info: " << readResult.second.message() << endl;
            continue;
        }

        const auto &stackInfo = readResult.first;

        printf("  stackId=%u, offset=0x%04x (@0x%04x), triggers=0x%08x (@0x%04x):\n",
               stackInfo.id,
               stackInfo.offset, stackInfo.offsetReg,
               stackInfo.triggers, stackInfo.triggerReg);

        u16 addr = stackInfo.startAddress;
        unsigned line = 0u;

        printf("  +-------+--------+------------+\n");
        printf("  | line  | addr   | value      |\n");
        printf("  +-------+--------+------------+\n");

        for (const u32 &value: stackInfo.contents)
        {
            printf("  |  %4u | 0x%04x | 0x%08x |\n",
                   line, addr, value);

            addr += mesytec::mvlc::AddressIncrement;
            line++;
        }

        printf("  +-------+--------+------------+\n");

        stackId++;
    }

    return 0;
}
