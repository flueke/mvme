#include <iostream>

#include "gtest/gtest.h"
#include "mvlc_constants.h"
#include "mvlc_readout.h"
#include "vme_constants.h"

using std::cout;
using std::endl;

using namespace mesytec::mvlc;

TEST(mvlc_readout_config, CrateConfigToYaml)
{
    CrateConfig cc;
    cc.connectionType = ConnectionType::USB;
    cc.usbIndex = 42;
    cc.usbSerial = "1234";
    cc.ethHost = "example.com";

    {
        StackCommandBuilder sb;
        sb.beginGroup("module0");
        sb.addVMEBlockRead(0x00000000u, vme_amods::MBLT64, (1u << 16)-1);
        sb.beginGroup("module1");
        sb.addVMEBlockRead(0x10000000u, vme_amods::MBLT64, (1u << 16)-1);
        sb.beginGroup("reset");
        sb.addVMEWrite(0xbb006070u, 1, vme_amods::A32, VMEDataWidth::D32);

        cc.stacks.emplace_back(sb);
    }

    {
        u8 irqLevel = 1;
        u32 triggerVal = stacks::TriggerType::IRQNoIACK << stacks::TriggerTypeShift;
        triggerVal |= (irqLevel - 1) & stacks::TriggerBitsMask;

        cc.triggers.push_back(triggerVal);
    }

    assert(cc.stacks.size() == cc.triggers.size());

    cout << to_yaml(cc) << endl;
}
