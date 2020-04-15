#include <chrono>
#include <iostream>
#include <fmt/format.h>

#include "gtest/gtest.h"

#include "mesytec-mvlc/mvlc_constants.h"
#include "mvlc_dialog_util.h"
#include "mvlc_factory.h"
#include "mvlc_stack_executor.h"
#include "vme_constants.h"

using std::cout;
using std::endl;

using namespace mesytec::mvlc;
using SuperCT = SuperCommandType;
using StackCT = StackCommand::CommandType;

template<typename Out>
Out &log_buffer(Out &out, const std::vector<u32> &buffer, const std::string &header = {})
{
    out << "begin buffer '" << header << "' (size=" << buffer.size() << ")" << endl;

    for (const auto &value: buffer)
        out << fmt::format("  {:#010x}", value) << endl;

    out << "end buffer " << header << "' (size=" << buffer.size() << ")" << endl;

    return out;
}

TEST(mvlc_stack_executor, TestTransactions)
{
    using namespace detail;

    //try
    {
        auto mvlc = make_mvlc_usb();
        //auto mvlc = make_mvlc_eth("mvlc-0007");

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
            throw ec;

        if (auto ec = disable_all_triggers(mvlc))
            throw ec;

        const u32 vmeBase = 0x0;
        const u32 vmeBaseNoModule = 0x10000000u;

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            StackCommandBuilder stack;

            //stack.addVMERead(vmeBase + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            //stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            //stack.addVMERead(vmeBaseNoModule + 0x6008, vme_amods::A32, VMEDataWidth::D16);


            for (int i=0; i<5505; i++)
                stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            stack.addVMERead(vmeBase + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            stack.addVMERead(vmeBaseNoModule + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            stack.addVMERead(vmeBaseNoModule + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            stack.addVMERead(vmeBase + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            std::vector<u32> response;

            std::error_code ec= {};
            //auto ec = stack_transaction(mvlc, stack, response);

            if (ec && ec != ErrorType::VMEError)
            {
                cout << ec.message() << endl;
                throw ec;
            }

            log_buffer(cout, response, to_string(stack.getCommands()[0]));

            if (ec == ErrorType::VMEError)
                cout << "Received a VMEError: " << ec.message() << endl;

            size_t notificationIndex = 0;
            for (const auto &notification: mvlc.getStackErrorNotifications())
            {
                log_buffer(cout, notification, "notification #" + std::to_string(notificationIndex++));
            }

            auto commands = stack.getCommands();
            auto parts = split_commands(commands, {}, stacks::StackMemoryWords);

            cout << "split_commands returned " << parts.size() << " parts:" << endl;
            for (const auto &part: parts)
                cout << " size=" << part.size() << ", encodedSize=" << get_encoded_stack_size(part) << endl;

            for (const auto &part: parts)
            {
                if (auto ec = stack_transaction(mvlc, part, response))
                    throw ec;
            }
        }
    }
    //catch (const std::error_code &ec)
    //{
    //    cout << "std::error_code thrown: " << ec.message() << endl;
    //    throw;
    //}
}

TEST(mvlc_stack_executor, SplitCommands1)
{
    const u32 vmeBase = 0x0;
    StackCommandBuilder stack;
    stack.addVMERead(vmeBase + 0x1000, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1002, vme_amods::A32, VMEDataWidth::D16);
    stack.addSoftwareDelay(std::chrono::milliseconds(100));
    stack.addVMERead(vmeBase + 0x1004, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1008, vme_amods::A32, VMEDataWidth::D16);


    {
        const u16 StackReservedWords = 1;
        auto commands = stack.getCommands();
        ASSERT_THROW(detail::split_commands(commands, {}, StackReservedWords), std::runtime_error);
    }

    {
        const u16 StackReservedWords = stacks::ImmediateStackReservedWords;
        auto commands = stack.getCommands();
        auto parts = detail::split_commands(commands, {}, StackReservedWords);
        ASSERT_EQ(parts.size(), 2);
        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][1].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][2].type, StackCommand::CommandType::SoftwareDelay);
        ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[1][1].type, StackCommand::CommandType::VMERead);
    }

    for (int i=0; i<2000; i++)
    {
        stack.addVMERead(vmeBase + 0x100a + 2 * i, vme_amods::A32, VMEDataWidth::D16);
    }

    {
        const std::vector<u16> StackReservedWords =
        {
            stacks::ImmediateStackReservedWords / 2,
            stacks::ImmediateStackReservedWords,
            stacks::ImmediateStackReservedWords * 2,
            stacks::StackMemoryWords / 2,
            stacks::StackMemoryWords,
            // Would overflow MVLCs stack memory.
            stacks::StackMemoryWords * 2,
            // Would overflow MVLCs stack memory. Still results in at least two
            // parts because the 3rd command is a SoftwareDelay.
            stacks::StackMemoryWords * 4,
        };

        const auto commands = stack.getCommands();
        std::cout << "--- commandCount=" << commands.size() << std::endl;

        for (auto reservedWords: StackReservedWords)
        {
            auto parts = detail::split_commands(commands, {}, reservedWords);

            std::cout << "--- reservedWords=" << reservedWords
                << ", partCount=" << parts.size() << std::endl;

            ASSERT_TRUE(parts.size() > 1);
            ASSERT_EQ(parts[0].size(), 3);
            ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
            ASSERT_EQ(parts[0][1].type, StackCommand::CommandType::VMERead);
            ASSERT_EQ(parts[0][2].type, StackCommand::CommandType::SoftwareDelay);
            ASSERT_TRUE(parts[1][0].type !=  StackCommand::CommandType::SoftwareDelay);

            for (const auto &part: parts)
            {
                ASSERT_TRUE(detail::get_encoded_stack_size(part) <= reservedWords);
                //std::cout << part.size() << ", " << detail::get_encoded_stack_size(part) << std::endl;
            }
        }
    }
}

TEST(mvlc_stack_executor, SplitCommands2)
{
    const u32 vmeBase = 0x0;
    StackCommandBuilder stack;
    //stack.addVMERead(vmeBase + 0x1000, vme_amods::A32, VMEDataWidth::D16);
    //stack.addVMERead(vmeBase + 0x1002, vme_amods::A32, VMEDataWidth::D16);
    stack.addSoftwareDelay(std::chrono::milliseconds(100));
    stack.addSoftwareDelay(std::chrono::milliseconds(100));
    //stack.addVMERead(vmeBase + 0x1004, vme_amods::A32, VMEDataWidth::D16);
    //stack.addVMERead(vmeBase + 0x1008, vme_amods::A32, VMEDataWidth::D16);

    auto commands = stack.getCommands();
    auto parts = detail::split_commands(commands);
    ASSERT_EQ(parts.size(), 2);
    ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::SoftwareDelay);
    ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::SoftwareDelay);

    //ASSERT_EQ(parts.size(), 3);
}
