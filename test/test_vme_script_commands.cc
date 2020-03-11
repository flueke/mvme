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
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A16);
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
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A16);
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
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
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
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
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
