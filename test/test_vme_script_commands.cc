#include "gtest/gtest.h"
#include "vme_script.h"
#include "vme_script_exec.h"
#include <cstring>
#include <QDebug>

using namespace vme_script;

namespace
{
    // Returns the lower (index=0) or upper (index=1) raw 16 bits of a 32 bit
    // float value.
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
        //qDebug("cmd.value=0x%08x", cmd.value);
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
        //qDebug("cmd.value=0x%08x", cmd.value);
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

        //qDebug("cmd.value=0x%08x", cmd.value);
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

        //qDebug("cmd.value=0x%08x", cmd.value);
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
    ASSERT_EQ(cmd.addressMode, vme_address_modes::A24);
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

TEST(vme_script_commands, Reads)
{
    {
        auto input = QSL("read a32 d32 0x1111");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::Read);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D32);
        ASSERT_EQ(cmd.mvlcSlowRead, false);
        ASSERT_EQ(cmd.mvlcFifoMode, true);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }

    {
        auto input = QSL("read a32 d16 0x1111 late");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::Read);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D16);
        ASSERT_EQ(cmd.mvlcSlowRead, true);
        ASSERT_EQ(cmd.mvlcFifoMode, true);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }

    {
        auto input = QSL("read a32 d16 0x1111 fifo");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::Read);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D16);
        ASSERT_EQ(cmd.mvlcSlowRead, false);
        ASSERT_EQ(cmd.mvlcFifoMode, true);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }

    {
        auto input = QSL("read a32 d16 0x1111 fifo late");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::Read);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D16);
        ASSERT_EQ(cmd.mvlcSlowRead, true);
        ASSERT_EQ(cmd.mvlcFifoMode, true);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }

    {
        auto input = QSL("read a32 d16 0x1111 slow");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::Read);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D16);
        ASSERT_EQ(cmd.mvlcSlowRead, true);
        ASSERT_EQ(cmd.mvlcFifoMode, true);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }

    {
        auto input = QSL("read a32 d16 0x1111 mem");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::Read);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D16);
        ASSERT_EQ(cmd.mvlcSlowRead, false);
        ASSERT_EQ(cmd.mvlcFifoMode, false);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }

    {
        auto input = QSL("read a32 d16 0x1111 mem late");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::Read);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D16);
        ASSERT_EQ(cmd.mvlcSlowRead, true);
        ASSERT_EQ(cmd.mvlcFifoMode, false);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }
}

TEST(vme_script_commands, BlockReads)
{
    // blt a24
    {
        auto input = QSL("blt a24 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::BLT);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::a24UserBlock);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // bltfifo a24
    {
        auto input = QSL("bltfifo a24 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::BLTFifo);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::a24UserBlock);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // blt a32
    {
        auto input = QSL("blt a32 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::BLT);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::BLT32);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // bltfifo a32
    {
        auto input = QSL("bltfifo a32 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::BLTFifo);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::BLT32);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // blt with a numeric amod value (0x3F == a24PrivBlock)
    {
        auto input = QSL("blt 0x3F 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::BLT);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, 0x3F);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // blt with a numeric amod value (0x0F == a32PrivBlock)
    {
        auto input = QSL("blt 0x0F 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::BLT);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, 0x0F);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // blt but with a custom, non-sensical address mode
    {
        auto input = QSL("blt 0x42 0x1234 1000");
        EXPECT_THROW(vme_script::parse(input), vme_script::ParseError);
    }

    // mblt a32
    {
        auto input = QSL("mblt a32 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::MBLT);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::MBLT64);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // mblt with a numeric amod value (0x0c == a32PrivBlock64)
    {
        auto input = QSL("mblt 0x0c 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::MBLT);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, 0x0c);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // mblt with an invalid numeric amod value
    {
        auto input = QSL("mblt 0x42 0x1234 1000");
        EXPECT_THROW(vme_script::parse(input), vme_script::ParseError);
    }

    // mbltfifo a32
    {
        auto input = QSL("mbltfifo a32 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::MBLTFifo);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::MBLT64);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // mblts a32 - swapped mblt
    {
        auto input = QSL("mblts a32 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::MBLTSwapped);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::MBLT64);
        ASSERT_EQ(cmd.transfers, 1000);
    }

    // mbltsfifo a32 - swapped mblt
    {
        auto input = QSL("mbltsfifo a32 0x1234 1000");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::MBLTSwappedFifo);
        ASSERT_EQ(cmd.address, 0x1234);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::MBLT64);
        ASSERT_EQ(cmd.transfers, 1000);
    }
}

TEST(vme_script_commands, BlockRead2eSstFifo)
{
    const auto commands = { "2esst", "2esstfifo" };

    {
        const auto rates = { "0", "160", "160mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64Fifo);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate160MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }

    {
        const auto rates = { "1", "276", "276mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64Fifo);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate276MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }

    {
        const auto rates = { "2", "320", "320mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64Fifo);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate320MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }
}

TEST(vme_script_commands, BlockRead2eSstSwappedFifo)
{
    const auto commands = { "2essts", "2esstsfifo" };

    {
        const auto rates = { "0", "160", "160mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64SwappedFifo);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate160MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }

    {
        const auto rates = { "1", "276", "276mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64SwappedFifo);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate276MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }

    {
        const auto rates = { "2", "320", "320mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64SwappedFifo);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate320MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }
}

TEST(vme_script_commands, BlockRead2eSstMem)
{
    const auto commands = { "2esstmem" };

    {
        const auto rates = { "0", "160", "160mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate160MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }

    {
        const auto rates = { "1", "276", "276mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate276MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }

    {
        const auto rates = { "2", "320", "320mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate320MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }
}

TEST(vme_script_commands, BlockRead2eSstSwappedMem)
{
    const auto commands = { "2esstsmem" };

    {
        const auto rates = { "0", "160", "160mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64Swapped);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate160MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }

    {
        const auto rates = { "1", "276", "276mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64Swapped);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate276MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }

    {
        const auto rates = { "2", "320", "320mb" };

        for (const auto &cmd: commands)
        {
            for (const auto &rate: rates)
            {
                auto input = QSL("%1 0x1234 %2 54321").arg(cmd).arg(rate);
                auto script = vme_script::parse(input);
                ASSERT_EQ(script.size(), 1);
                auto &cmd = script.first();
                ASSERT_EQ(cmd.type, CommandType::Blk2eSST64Swapped);
                ASSERT_EQ(cmd.address, 0x1234);
                ASSERT_EQ(cmd.blk2eSSTRate, Blk2eSSTRate::Rate320MB);
                ASSERT_EQ(cmd.transfers, 54321);
            }
        }
    }
}

TEST(vme_script_commands, Writes)
{
    {
        auto input = QSL("write a32 d16 0x1111 0x2222");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::Write);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.value, 0x2222);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D16);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }

    {
        auto input = QSL("write a32 d32 0x1111 0x2222");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::Write);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.value, 0x2222);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D32);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }

    {
        auto input = QSL("writeabs a32 d16 0x1111 0x2222");
        auto script = vme_script::parse(input);
        ASSERT_EQ(script.size(), 1);
        auto &cmd = script.first();
        ASSERT_EQ(cmd.type, CommandType::WriteAbs);
        ASSERT_EQ(cmd.address, 0x1111);
        ASSERT_EQ(cmd.value, 0x2222);
        ASSERT_EQ(cmd.addressMode, vme_address_modes::A32);
        ASSERT_EQ(cmd.dataWidth, vme_script::DataWidth::D16);
        ASSERT_EQ(vme_script::parse(to_string(cmd)), script);
    }
}

TEST(vme_script_commands, SingleLineCommandInVariable)
{
    SymbolTables symtabs =
    {
        { "first", {{ QSL("myCmd"), Variable("mbltfifo a32 0x4321 12345") }}},
        { "second", {{ QSL("myvar"), Variable("a32") }}},
        { "third", {{ QSL("mycmd2"), Variable("mbltfifo ${myvar} 0x1234 54321") }}},
    };

    //qDebug() << symtabs[0].value("myCmd").value;

    {
        QString input = "${myCmd}";

        try
        {
            auto script = vme_script::parse(input, symtabs);
            ASSERT_EQ(script.size(), 1);
            auto &cmd = script.first();
            ASSERT_EQ(cmd.type, CommandType::MBLTFifo);
            ASSERT_EQ(cmd.address, 0x4321);
            ASSERT_EQ(cmd.transfers, 12345);
        } catch (const vme_script::ParseError &e)
        {
            qDebug() << e.toString();
            throw;
        }
    }

    {
        QString input = "${mycmd2}";

        try
        {
            auto script = vme_script::parse(input, symtabs);
            ASSERT_EQ(script.size(), 1);
            auto &cmd = script.first();
            ASSERT_EQ(cmd.type, CommandType::MBLTFifo);
            ASSERT_EQ(cmd.address, 0x1234);
            ASSERT_EQ(cmd.transfers, 54321);
        } catch (const vme_script::ParseError &e)
        {
            qDebug() << e.toString();
            throw;
        }
    }
}

TEST(vme_script_commands, ParseVMEAddressModes)
{
    namespace vme_amods = vme_address_modes;

    ASSERT_EQ(vme_script::parseAddressMode("a16"), vme_amods::A16);
    ASSERT_EQ(vme_script::parseAddressMode("A16"), vme_amods::A16);

    ASSERT_EQ(vme_script::parseAddressMode("a24"), vme_amods::A24);
    ASSERT_EQ(vme_script::parseAddressMode("A24"), vme_amods::A24);

    ASSERT_EQ(vme_script::parseAddressMode("a32"), vme_amods::A32);
    ASSERT_EQ(vme_script::parseAddressMode("A32"), vme_amods::A32);

    ASSERT_EQ(vme_script::parseAddressMode("42"), 42);
    ASSERT_EQ(vme_script::parseAddressMode("0x2a"), 42);

    ASSERT_THROW(vme_script::parseAddressMode("foobar"), const char *);
}

TEST(vme_script_commands, AccuSet)
{
    auto input = QSL("accu_set 0xDEADCAFE\naccu_add 1234");
    auto script = vme_script::parse(input);

    ASSERT_EQ(script.size(), 2);

    {
        auto &cmd = script[0];
        ASSERT_EQ(cmd.type, CommandType::Accu_Set);
        ASSERT_EQ(cmd.value, 0xDEADCAFE);
    }

    {
        auto &cmd = script[1];
        ASSERT_EQ(cmd.type, CommandType::Accu_Add);
        ASSERT_EQ(cmd.value, 1234);
    }

    auto results = vme_script::run_script(
        nullptr,
        script,
        [] (const QString &msg) { qDebug() << "run_script:" << msg; }
        //,vme_script::run_script_options::LogEachResult
    );

    ASSERT_EQ(results.size(), 2);
    ASSERT_EQ(results[0].state.accu, 0xDEADCAFEu);
    ASSERT_EQ(results[1].state.accu, 0xDEADCAFEu + 1234u);

}
