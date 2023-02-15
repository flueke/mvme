/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <gtest/gtest.h>
#include <iostream>

#include "mvlc/mvlc_script.h"

using namespace mesytec::mvme_mvlc::script;

TEST(MVLCScriptTest, Empty)
{
    const QString input = R"(


    )";

    auto script = parse(input);

    ASSERT_EQ(script.size(), 0);
}

TEST(MVLCScriptTest, ReferenceWord)
{
    const QString input = R"(
        ref_word 0x43218765
    )";

    auto script = parse(input);

    ASSERT_EQ(script.size(), 1);

    const auto &cmd = script.at(0);
    ASSERT_EQ(cmd.type, CommandType::ReferenceWord);
    ASSERT_EQ(cmd.value, 0x43218765);
}

TEST(MVLCScriptTest, ReadLocal)
{
    {
        const QString input = R"(
            read_local 0x2000
        )";

        auto script = parse(input);

        ASSERT_EQ(script.size(), 1);

        const auto &cmd = script.at(0);
        ASSERT_EQ(cmd.type, CommandType::ReadLocal);
        ASSERT_EQ(cmd.address, 0x2000);
    }

    // parsed value > maxValue (register addresses are uint16_t)
    {
        const QString input = R"(
            read_local 0x1ffff
        )";

        ASSERT_THROW(parse(input), mesytec::mvme_mvlc::script::ParseError);

    }
}

TEST(MVLCScriptTest, WriteLocal)
{
    // basic write local
    {
        const QString input = R"(
            write_local 0x2000 0x87654321
        )";

        auto script = parse(input);

        ASSERT_EQ(script.size(), 1);

        const auto &cmd = script.at(0);
        ASSERT_EQ(cmd.type, CommandType::WriteLocal);
        ASSERT_EQ(cmd.address, 0x2000);
        ASSERT_EQ(cmd.value, 0x87654321);
    }

    // using binary literals
    {
        const QString input = R"(
            write_local 1234 0b101'110'011
        )";

        auto script = parse(input);

        ASSERT_EQ(script.size(), 1);

        const auto &cmd = script.at(0);
        ASSERT_EQ(cmd.type, CommandType::WriteLocal);
        ASSERT_EQ(cmd.address, 1234);
        ASSERT_EQ(cmd.value, 0b101'110'011);
    }
}

TEST(MVLCScriptTest, StackEmpty)
{
    {
        const QString input = R"(
            stack_start
            stack_end
        )";

        auto script = parse(input);

        ASSERT_EQ(script.size(), 1);

        const auto &cmd = script.at(0);
        ASSERT_EQ(cmd.type, CommandType::Stack);
        ASSERT_EQ(cmd.stack.contents.size(), 0);
    }

    //try
    {
        const QString input = R"(
            stack_start output=data offset=0x100
            stack_end
        )";

        auto script = parse(input);

        ASSERT_EQ(script.size(), 1);

        const auto &cmd = script.at(0);
        ASSERT_EQ(cmd.type, CommandType::Stack);
        ASSERT_EQ(cmd.stack.contents.size(), 0);
        ASSERT_EQ(cmd.stack.outputPipe, 1);
        ASSERT_EQ(cmd.stack.offset, 0x100);
    }

    //catch (const mesytec::mvme_mvlc::script::ParseError &e)
    //{
    //    std::cout << e.what() << std::endl;
    //}
}

TEST(MVLCScriptTest, StackErrors)
{
    // output out of range
    {
        const QString input = R"(
            stack_start output=2 offset=0x100
            stack_end
        )";

        ASSERT_THROW(parse(input), mesytec::mvme_mvlc::script::ParseError);
    }

    // unknown output name
    {
        const QString input = R"(
            stack_start output=foobar offset=0x100
            stack_end
        )";

        ASSERT_THROW(parse(input), mesytec::mvme_mvlc::script::ParseError);
    }

    // invalid output
    {
        const QString input = R"(
            stack_start output=foobar offset=0x100
            stack_end
        )";

        ASSERT_THROW(parse(input), mesytec::mvme_mvlc::script::ParseError);
    }

    // offset out of range
    {
        const QString input = R"(
            stack_start output=data offset=0x10000
            stack_end
        )";

        ASSERT_THROW(parse(input), mesytec::mvme_mvlc::script::ParseError);
    }

    // offset % 4 != 0
    {
        const QString input = R"(
            stack_start output=data offset=0x0001
            stack_end
        )";

        ASSERT_THROW(parse(input), mesytec::mvme_mvlc::script::ParseError);
    }
}

TEST(MVLCScriptTest, StackSingleCommand)
{
    {
        const QString input = R"(
            stack_start output=data offset=0x100
            setbase 0x01000000
            stack_end
        )";

        auto script = parse(input);

        ASSERT_EQ(script.size(), 1);

        const auto &cmd = script.at(0);
        ASSERT_EQ(cmd.type, CommandType::Stack);
        ASSERT_EQ(cmd.stack.outputPipe, 1);
        ASSERT_EQ(cmd.stack.offset, 0x100);

        const auto &vme = cmd.stack.contents;

        ASSERT_EQ(vme.size(), 1);

        ASSERT_EQ(vme.at(0).type, vme_script::CommandType::SetBase);
        ASSERT_EQ(vme.at(0).address, 0x01000000);
    }
}

TEST(MVLCScriptTest, StackMultiCommands)
{
    {
        const QString input = R"(
            stack_start output=data offset=0x100
                setbase 0x01000000
                write a24 d32 0x6070 3
                mbltfifo a32 0x1234 100 # <amod> <address> <transfer_count>
            stack_end
        )";

        auto script = parse(input);

        ASSERT_EQ(script.size(), 1);

        const auto &cmd = script.at(0);
        ASSERT_EQ(cmd.type, CommandType::Stack);
        ASSERT_EQ(cmd.stack.outputPipe, 1);
        ASSERT_EQ(cmd.stack.offset, 0x100);

        const auto &vme = cmd.stack.contents;

        ASSERT_EQ(vme.size(), 3);

        ASSERT_EQ(vme.at(0).type, vme_script::CommandType::SetBase);
        ASSERT_EQ(vme.at(0).address, 0x01000000);

        // Note: base address is added directly
        ASSERT_EQ(vme.at(1).type, vme_script::CommandType::Write);
        ASSERT_EQ(vme.at(1).address, 0x01000000 + 0x6070);
        ASSERT_EQ(vme.at(1).value, 3);

        ASSERT_EQ(vme.at(2).type, vme_script::CommandType::MBLTFifo);
        ASSERT_EQ(vme.at(2).address, 0x01000000 + 0x1234);
        ASSERT_EQ(vme.at(2).transfers, 100);
    }
}

TEST(MVLCScriptTest, StackNotFirstCommand)
{
    {
        const QString input = R"(
            write_local 0x2000 0x87654321
            stack_start output=data offset=0x100
                setbase 0x01000000
                write a24 d32 0x6070 3
                mbltfifo a32 0x1234 100 # <amod> <address> <transfer_count>
            stack_end
        )";

        auto script = parse(input);

        ASSERT_EQ(script.size(), 2);

        {
            const auto &cmd = script.at(0);
            ASSERT_EQ(cmd.type, CommandType::WriteLocal);
            ASSERT_EQ(cmd.address, 0x2000);
            ASSERT_EQ(cmd.value, 0x87654321);
        }

        {
            const auto &cmd = script.at(1);
            ASSERT_EQ(cmd.type, CommandType::Stack);
            ASSERT_EQ(cmd.stack.outputPipe, 1);
            ASSERT_EQ(cmd.stack.offset, 0x100);

            const auto &vme = cmd.stack.contents;

            ASSERT_EQ(vme.size(), 3);

            ASSERT_EQ(vme.at(0).type, vme_script::CommandType::SetBase);
            ASSERT_EQ(vme.at(0).address, 0x01000000);

            // Note: base address is added directly
            ASSERT_EQ(vme.at(1).type, vme_script::CommandType::Write);
            ASSERT_EQ(vme.at(1).address, 0x01000000 + 0x6070);
            ASSERT_EQ(vme.at(1).value, 3);

            ASSERT_EQ(vme.at(2).type, vme_script::CommandType::MBLTFifo);
            ASSERT_EQ(vme.at(2).address, 0x01000000 + 0x1234);
            ASSERT_EQ(vme.at(2).transfers, 100);
        }
    }
}

#if 0
TEST(MVLCScriptTest, Basic)
{
    const QString input = R"(
    write_local 0x2000 0x1234
    read_local 0x2000
    ref_word 0x87654321
    0xAFFE1234          # custom value to be written
    stack_start id=0 address=0x2100 pipe=command
        # vme_script commands until the matching "stack_end"
        setbase 0x01000000
        0x6070 3
    stack_end
    write_reset
    )";

    auto script = parse(input);
    ASSERT_EQ(script.size(), 0);
}
#endif
