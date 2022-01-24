#include <gtest/gtest.h>

#include "vmeconfig_to_crateconfig.h"

//using namespace mesytec;

// FIXME: actually add some real vmeconfig_to_crateconfig tests


TEST(vmeconfig_to_crateconfig, ConvertCommand_Types)
{

    //std::map<vme_script::CommandType, mesytec::mvlc::StackCommand::CommandType> typePairs;

    //typePairs[vme_script::MVLC_SetAddressIncMode] = mesytec::mvlc::StackCommand::CommandType::SetAddressIncMode;


    {
        vme_script::Command vCmd = {};
        vCmd.type = vme_script::CommandType::MVLC_SetAddressIncMode;

        auto mCmd = mesytec::mvme::convert_command(vCmd);

        ASSERT_EQ(mCmd.type, mesytec::mvlc::StackCommand::CommandType::SetAddressIncMode);
    }

    {
        vme_script::Command vCmd = {};
        vCmd.type = vme_script::CommandType::MVLC_MaskShiftAccu;

        auto mCmd = mesytec::mvme::convert_command(vCmd);

        ASSERT_EQ(mCmd.type, mesytec::mvlc::StackCommand::CommandType::MaskShiftAccu);
    }
}
