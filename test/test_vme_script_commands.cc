#include "gtest/gtest.h"
#include "vme_script.h"
#include <cstring>
#include <QDebug>

using namespace vme_script;

namespace
{
    u16 get_float_word(float f, unsigned index)
    {
        u32 result = 0u;
        std::memcpy(&result, &f, sizeof(result));

        if (index == 1)
            result >>= 16;

        result &= 0xffff;
        return result;
    }
}

TEST(vme_script_commands, WriteFloatWord)
{
    // lower
    {
        QString input = R"_(write_float_word a16 0x6060 lower 666.666)_";

        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);

        auto &cmd = script.first();
        qDebug("cmd.value=0x%08x", cmd.value);
        ASSERT_EQ(cmd.type, CommandType::Write);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::a16Priv);
        ASSERT_EQ(cmd.dataWidth, DataWidth::D16);
        ASSERT_EQ(cmd.address, 0x6060);
        ASSERT_EQ(cmd.value, get_float_word(666.666, 0));
    }
    {
        QString input = R"_(write_float_word a16 0x6060 0 666.666)_";

        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);

        auto &cmd = script.first();
        qDebug("cmd.value=0x%08x", cmd.value);
        ASSERT_EQ(cmd.type, CommandType::Write);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::a16Priv);
        ASSERT_EQ(cmd.dataWidth, DataWidth::D16);
        ASSERT_EQ(cmd.address, 0x6060);
        ASSERT_EQ(cmd.value, get_float_word(666.666, 0));
    }

    // upper
    {
        QString input = R"_(write_float_word a32 0x6060 upper 666.666)_";

        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);

        auto &cmd = script.first();

        qDebug("cmd.value=0x%08x", cmd.value);
        ASSERT_EQ(cmd.type, CommandType::Write);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::a32PrivData);
        ASSERT_EQ(cmd.dataWidth, DataWidth::D16);
        ASSERT_EQ(cmd.address, 0x6060);
        ASSERT_EQ(cmd.value, get_float_word(666.666, 1));
    }
    {
        QString input = R"_(write_float_word a32 0x6060 1 666.666)_";

        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);

        auto &cmd = script.first();

        qDebug("cmd.value=0x%08x", cmd.value);
        ASSERT_EQ(cmd.type, CommandType::Write);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::a32PrivData);
        ASSERT_EQ(cmd.dataWidth, DataWidth::D16);
        ASSERT_EQ(cmd.address, 0x6060);
        ASSERT_EQ(cmd.value, get_float_word(666.666, 1));
    }

    // invalid part
    {
        QString input = R"_(write_float_word a16 0x6060 foobar 1234.0)_";

        ASSERT_THROW(vme_script::parse(input), ParseError);
    }

    // invalid float
    {
        QString input = R"_(write_float_word a16 0x6060 lower asdf)_";

        ASSERT_THROW(vme_script::parse(input), ParseError);
    }
}

TEST(vme_script_commands, MVLC_SetAddressIncMode)
{
    QStringList inputs =
    {
        R"_(mvlc_set_address_inc_mode fifo)_",
        R"_(mvlc_set_address_inc_mode mem)_",
    };

    for (const auto input: inputs)
    {
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::MVLC_SetAddressIncMode);
        ASSERT_EQ(to_string(cmd), input);
    }

    QString input = R"_(mvlc_set_address_inc_mode foobar)_";
    ASSERT_THROW(vme_script::parse(input), ParseError);
}

TEST(vme_script_commands, MVLC_Wait)
{
    QString input = "mvlc_wait 42";
    auto script = vme_script::parse(input);
    ASSERT_EQ(script.size(), 1);
    auto &cmd = script.first();
    ASSERT_EQ(cmd.type, CommandType::MVLC_Wait);
    ASSERT_EQ(cmd.value, 42);
    ASSERT_EQ(to_string(cmd), input);
}

TEST(vme_script_commands, MVLC_SignalAccu)
{
    QString input = "mvlc_signal_accu";
    auto script = vme_script::parse(input);
    ASSERT_EQ(script.size(), 1);
    auto &cmd = script.first();
    ASSERT_EQ(cmd.type, CommandType::MVLC_SignalAccu);
    ASSERT_EQ(to_string(cmd), input);
}

TEST(vme_script_commands, MVLC_MaskShiftAccu)
{
    QString input = "mvlc_mask_shift_accu 0xFF 13";
    auto script = vme_script::parse(input);
    ASSERT_EQ(script.size(), 1);
    auto &cmd = script.first();
    ASSERT_EQ(cmd.type, CommandType::MVLC_MaskShiftAccu);
    ASSERT_EQ(cmd.address, 0xFF);
    ASSERT_EQ(cmd.value, 13);
}

TEST(vme_script_commands, MVLC_SetAccu)
{
    QString input = "mvlc_set_accu 0x42069";
    auto script = vme_script::parse(input);
    ASSERT_EQ(script.size(), 1);
    auto &cmd = script.first();
    ASSERT_EQ(cmd.type, CommandType::MVLC_SetAccu);
    ASSERT_EQ(cmd.value, 0x42069);
}

TEST(vme_script_commands, MVLC_ReadToAccu)
{
    QString input = "mvlc_read_to_accu a24 d32 0x1337";
    auto script = vme_script::parse(input);
    ASSERT_EQ(script.size(), 1);
    auto &cmd = script.first();
    ASSERT_EQ(cmd.type, CommandType::MVLC_ReadToAccu);
    ASSERT_EQ(cmd.address, 0x1337);
    ASSERT_EQ(cmd.dataWidth, DataWidth::D32);
    ASSERT_EQ(cmd.addressMode, vme_address_modes::a24PrivData);
}

TEST(vme_script_commands, MVLC_CompareLoopAccu)
{
    QStringList inputs =
    {
        R"_(mvlc_compare_loop_accu eq 13)_",
        R"_(mvlc_compare_loop_accu lt 14)_",
        R"_(mvlc_compare_loop_accu gt 15)_",
    };

    for (const auto input: inputs)
    {
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::MVLC_CompareLoopAccu);
        ASSERT_EQ(to_string(cmd), input);
    }

    QString input = R"_(mvlc_compare_loop_accu wrong 13)_";
    ASSERT_THROW(vme_script::parse(input), ParseError);
}
