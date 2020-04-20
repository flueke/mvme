#include <chrono>
#include <iostream>
#include <fmt/format.h>

#include "gtest/gtest.h"
#include <ratio>
#include <limits>

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

MVLC make_testing_mvlc()
{
    //return make_mvlc_eth("mvlc-0007");
    return make_mvlc_usb();
}

TEST(mvlc_stack_executor, MVLCTestTransactions)
{
    using namespace detail;

    //try
    {
        auto mvlc = make_testing_mvlc();

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        if (auto ec = disable_all_triggers(mvlc))
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

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

            auto commands = stack.getCommands();
            auto parts = split_commands(commands, {}, stacks::StackMemoryWords);

            cout << "split_commands returned " << parts.size() << " parts:" << endl;

            for (const auto &part: parts)
                cout << " size=" << part.size() << "words, encodedSize=" << get_encoded_stack_size(part) << endl;

            std::vector<u32> response;

            for (const auto &part: parts)
            {
                if (auto ec = stack_transaction(mvlc, part, response))
                {
                    if (ec)
                    {
                        std::cout << "stack_transactions returned: " << ec.message() << std::endl;
                    }

                    if (ec != ErrorType::VMEError)
                    {
                        throw ec;
                    }

                    ASSERT_TRUE(response.size() >= 1);

                    auto itResponse = std::begin(response);
                    const auto endResponse = std::end(response);
                    u32 frameHeader = *itResponse;
                    auto frameInfo = extract_frame_info(frameHeader);

                    ASSERT_TRUE(is_stack_buffer(frameHeader));
                    ASSERT_TRUE(endResponse - itResponse >= frameInfo.len + 1u);

                    while (frameInfo.flags & frame_flags::Continue)
                    {
                        itResponse += frameInfo.len + 1u;

                        ASSERT_TRUE(itResponse < endResponse);

                        frameHeader = *itResponse;
                        frameInfo = extract_frame_info(frameHeader);

                        ASSERT_TRUE(is_stack_buffer_continuation(frameHeader));
                        ASSERT_TRUE(endResponse - itResponse >= frameInfo.len + 1u);
                    }

                    //log_buffer(cout, response);
                }
            }
        }
    }
    //catch (const std::error_code &ec)
    //{
    //    cout << "std::error_code thrown: " << ec.message() << endl;
    //    throw;
    //}
}

TEST(mvlc_stack_executor, SplitCommandsOptions)
{
    const u32 vmeBase = 0x0;
    StackCommandBuilder stack;
    stack.addVMERead(vmeBase + 0x1000, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1002, vme_amods::A32, VMEDataWidth::D16);
    stack.addSoftwareDelay(std::chrono::milliseconds(100));
    stack.addVMERead(vmeBase + 0x1004, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1008, vme_amods::A32, VMEDataWidth::D16);

    // reserved stack size is too small -> not advancing
    {
        const u16 StackReservedWords = 1;
        auto commands = stack.getCommands();
        ASSERT_THROW(detail::split_commands(commands, {}, StackReservedWords), std::runtime_error);
    }

    {
        const u16 StackReservedWords = stacks::ImmediateStackReservedWords;
        auto commands = stack.getCommands();
        auto parts = detail::split_commands(commands, {}, StackReservedWords);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][1].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::SoftwareDelay);
        ASSERT_EQ(parts[2][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[2][1].type, StackCommand::CommandType::VMERead);
    }

    // ignoreDelays = true
    {
        const u16 StackReservedWords = stacks::ImmediateStackReservedWords;
        auto commands = stack.getCommands();
        Options options { .ignoreDelays = true, .noBatching = false };
        auto parts = detail::split_commands(commands, options, StackReservedWords);
        ASSERT_EQ(parts.size(), 1);
        ASSERT_EQ(parts[0].size(), 5);
        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][1].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][2].type, StackCommand::CommandType::SoftwareDelay);
        ASSERT_EQ(parts[0][3].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][4].type, StackCommand::CommandType::VMERead);
    }

    // noBatching = true
    {
        const u16 StackReservedWords = stacks::ImmediateStackReservedWords;
        auto commands = stack.getCommands();
        Options options { .ignoreDelays = false, .noBatching = true };
        auto parts = detail::split_commands(commands, options, StackReservedWords);
        ASSERT_EQ(parts.size(), 5);

        ASSERT_EQ(parts[0].size(), 1);
        ASSERT_EQ(parts[1].size(), 1);
        ASSERT_EQ(parts[2].size(), 1);
        ASSERT_EQ(parts[3].size(), 1);
        ASSERT_EQ(parts[4].size(), 1);

        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[2][0].type, StackCommand::CommandType::SoftwareDelay);
        ASSERT_EQ(parts[3][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[4][0].type, StackCommand::CommandType::VMERead);
    }

    // ignoreDelays=true, noBatching = true
    {
        const u16 StackReservedWords = stacks::ImmediateStackReservedWords;
        auto commands = stack.getCommands();
        Options options { .ignoreDelays = false, .noBatching = true };
        auto parts = detail::split_commands(commands, options, StackReservedWords);
        ASSERT_EQ(parts.size(), 5);

        ASSERT_EQ(parts[0].size(), 1);
        ASSERT_EQ(parts[1].size(), 1);
        ASSERT_EQ(parts[2].size(), 1);
        ASSERT_EQ(parts[3].size(), 1);
        ASSERT_EQ(parts[4].size(), 1);

        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[2][0].type, StackCommand::CommandType::SoftwareDelay);
        ASSERT_EQ(parts[3][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[4][0].type, StackCommand::CommandType::VMERead);
    }
}

TEST(mvlc_stack_executor, SplitCommandsStackSizes)
{
    // Build a stack with a known start sequence, then add more vme read
    // commands. Split the stack using various immediateStackMaxSize values.

    const u32 vmeBase = 0x0;
    StackCommandBuilder stack;
    stack.addVMERead(vmeBase + 0x1000, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1002, vme_amods::A32, VMEDataWidth::D16);
    stack.addSoftwareDelay(std::chrono::milliseconds(100));
    stack.addVMERead(vmeBase + 0x1004, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1008, vme_amods::A32, VMEDataWidth::D16);

    for (int i=0; i<2000; i++)
        stack.addVMERead(vmeBase + 0x100a + 2 * i, vme_amods::A32, VMEDataWidth::D16);

    const std::vector<u16> StackReservedWords =
    {
        stacks::ImmediateStackReservedWords / 2,
        stacks::ImmediateStackReservedWords,
        stacks::ImmediateStackReservedWords * 2,
        stacks::StackMemoryWords / 2,
        stacks::StackMemoryWords,
        // Would overflow MVLCs stack memory.
        stacks::StackMemoryWords * 2,
        // Would overflow MVLCs stack memory. Still results in at least three
        // parts because the 3rd command is a SoftwareDelay.
        stacks::StackMemoryWords * 4,
    };

    const auto commands = stack.getCommands();
    //std::cout << "commandCount=" << commands.size() << std::endl;

    for (auto reservedWords: StackReservedWords)
    {
        auto parts = detail::split_commands(commands, {}, reservedWords);

        //std::cout << "reservedWords=" << reservedWords
        //    << " -> partCount=" << parts.size() << std::endl;

        ASSERT_TRUE(parts.size() > 2);

        ASSERT_EQ(parts[0].size(), 2);
        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][1].type, StackCommand::CommandType::VMERead);

        ASSERT_EQ(parts[1].size(), 1);
        ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::SoftwareDelay);

        ASSERT_TRUE(parts[2][0].type !=  StackCommand::CommandType::SoftwareDelay);

        for (const auto &part: parts)
        {
            ASSERT_TRUE(get_encoded_stack_size(part) <= reservedWords);
            //std::cout << "  part commandCount=" << part.size() << ", encodedSize=" << detail::get_encoded_stack_size(part) << std::endl;
        }
    }
}

TEST(mvlc_stack_executor, SplitCommandsSoftwareDelays)
{
    StackCommandBuilder stack;
    stack.addSoftwareDelay(std::chrono::milliseconds(100));
    stack.addSoftwareDelay(std::chrono::milliseconds(100));

    auto commands = stack.getCommands();
    auto parts = detail::split_commands(commands);

    ASSERT_EQ(parts.size(), 2);
    ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::SoftwareDelay);
    ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::SoftwareDelay);
}

TEST(mvlc_stack_executor, MVLCTestExecParseWrites)
{
    //try
    {
        auto mvlc = make_testing_mvlc();

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        if (auto ec = disable_all_triggers(mvlc))
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        const u32 base = 0x00000000u;
        const u8 mcstByte = 0xbbu;
        const u32 mcst = mcstByte << 24;
        const u8 irq = 1;
        const auto amod = vme_amods::A32;
        const auto dw = VMEDataWidth::D16;

        {
            StackCommandBuilder stack;
            stack.beginGroup("init");

            // module reset
            stack.addVMEWrite(base + 0x6008, 1, amod, dw);
            stack.addSoftwareDelay(std::chrono::milliseconds(100));

            // mtdc pulser, multi event readout and multicast setup
            stack.addVMEWrite(base + 0x6070, 7, amod, dw); // pulser
            stack.addVMEWrite(base + 0x6010, irq, amod, dw); // irq
            stack.addVMEWrite(base + 0x601c, 0, amod, dw);
            stack.addVMEWrite(base + 0x601e, 100, amod, dw); // irq fifo threshold in events
            stack.addVMEWrite(base + 0x6038, 0, amod, dw); // eoe marker (0: eventCounter)
            stack.addVMEWrite(base + 0x6036, 0xb, amod, dw); // multievent mode
            stack.addVMEWrite(base + 0x601a, 100, amod, dw); // max transfer data
            stack.addVMEWrite(base + 0x6020, 0x80, amod, dw); // enable mcst
            stack.addVMEWrite(base + 0x6024, mcstByte, amod, dw); // mcst address

            // mcst daq start sequence
            stack.addVMEWrite(mcst + 0x603a, 0, amod, dw);
            stack.addVMEWrite(mcst + 0x6090, 3, amod, dw);
            stack.addVMEWrite(mcst + 0x603c, 1, amod, dw);
            stack.addVMEWrite(mcst + 0x603a, 1, amod, dw);
            stack.addVMEWrite(mcst + 0x6034, 1, amod, dw);

            struct Options options;
            options.ignoreDelays = false;
            options.noBatching = false;

            //std::function<bool (const std::error_code &ec)> abortPredicate = is_connection_error;

            std::vector<u32> response;
            auto ec = execute_stack(mvlc, stack, stacks::StackMemoryWords, options, /* abortPredicate, */ response);
            //log_buffer(cout, response, "mtdc init response");

            if (ec)
                throw ec;

            auto parsedResults = parse_response(stack, response);

            const auto commands = stack.getCommands();

            ASSERT_EQ(parsedResults.size(), commands.size());

            for (size_t i=0; i<commands.size(); ++i)
            {
                ASSERT_EQ(commands[i], parsedResults[i].cmd);
                ASSERT_TRUE(parsedResults[i].response.empty());
            }

            //cout << "parsedResults.size()=" << parsedResults.size() << endl;
            //for (const auto &result: parsedResults)
            //{
            //    log_buffer(cout, result.response, to_string(result.cmd));
            //}
        }
    }
    //catch (const std::error_code &ec)
    //{
    //    cout << "std::error_code thrown: " << ec.message() << endl;
    //    throw;
    //}
}

TEST(mvlc_stack_executor, MVLCTestExecParseReads)
{
    //try
    {
        auto mvlc = make_testing_mvlc();

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        if (auto ec = disable_all_triggers(mvlc))
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        const u32 base = 0x00000000u;
        const u8 mcstByte = 0xbbu;
        const u32 mcst = mcstByte << 24;
        const u8 irq = 1;
        const auto amod = vme_amods::A32;
        const auto dw = VMEDataWidth::D16;

        {
            StackCommandBuilder stack;
            stack.beginGroup("readout_test");

            stack.addVMERead(base + 0x6092, amod, dw); // event counter low
            stack.addVMERead(base + 0x6094, amod, dw); // event counter high

            // block read from mtdc fifo
            //stack.addVMEBlockRead(base, vme_amods::MBLT64, std::numeric_limits<u16>::max());

            stack.addVMERead(base + 0x6092, amod, dw); // event counter low
            stack.addVMERead(base + 0x6094, amod, dw); // event counter high

            stack.addVMEWrite(mcst + 0x6034, 1, amod, dw); // readout reset

            struct Options options;
            options.ignoreDelays = false;
            options.noBatching = false;

            //std::function<bool (const std::error_code &ec)> abortPredicate = is_connection_error;

            size_t szMin = std::numeric_limits<size_t>::max(), szMax = 0, szSum = 0, iterations = 0;
            std::vector<u32> response;

            for (int i=0; i<1; i++)
            {
                response.clear();
                auto ec = execute_stack(mvlc, stack, stacks::StackMemoryWords, options, /* abortPredicate, */ response);

                if (ec)
                    throw ec;

                size_t size = response.size();
                szMin = std::min(szMin, size);
                szMax = std::max(szMax, size);
                szSum += size;
                ++iterations;
                log_buffer(cout, response, "mtdc readout_test response");
                cout << "response.size() = " << response.size() << std::endl;

                auto parsedResults = parse_response(stack, response);

                const auto commands = stack.getCommands();

                ASSERT_EQ(parsedResults.size(), commands.size());

                for (size_t i=0; i<commands.size(); ++i)
                {
                    ASSERT_EQ(commands[i], parsedResults[i].cmd);

                    if (commands[i].type == StackCommand::CommandType::VMERead)
                        ASSERT_EQ(parsedResults[i].response.size(), 1u);
                    else
                        ASSERT_TRUE(parsedResults[i].response.empty());
                }

#if 0
                std::cout << "parse_response: results.size() = " << parsedResults.size() << std::endl;

                for (size_t i=0; i < parsedResults.size(); ++i)
                {
                    std::cout << "result #" << i << ", cmd=" << to_string(parsedResults[i].cmd) << std::endl;

                    for (const auto &value: parsedResults[i].response)
                        std::cout << fmt::format("  {:#010x}", value) << std::endl;

                    std::cout << std::endl;
                }
#endif
            }

#if 0
            double szAvg = szSum / (iterations * 1.0);

            cout << endl;
            cout << fmt::format("iterations={}, szSum={}, szMin={}, szMax={}, szAvg={}",
                                iterations, szSum, szMin, szMax, szAvg
                               ) << std::endl;
            cout << endl;
#endif
        }
    }
}

TEST(mvlc_stack_executor, MVLCTestExecParseBlockRead)
{
    //try
    {
        auto mvlc = make_testing_mvlc();

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        if (auto ec = disable_all_triggers(mvlc))
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        const u32 base = 0x00000000u;
        const u8 mcstByte = 0xbbu;
        const u32 mcst = mcstByte << 24;
        const u8 irq = 1;
        const auto amod = vme_amods::A32;
        const auto dw = VMEDataWidth::D16;

        {
            StackCommandBuilder stack;
            stack.beginGroup("readout_test");

            stack.addVMERead(base + 0x6092, amod, dw); // event counter low
            stack.addVMERead(base + 0x6094, amod, dw); // event counter high

            // block read from mtdc fifo
            stack.addVMEBlockRead(base, vme_amods::MBLT64, std::numeric_limits<u16>::max()); // should yield data
            stack.addVMEBlockRead(base, vme_amods::MBLT64, std::numeric_limits<u16>::max()); // should be empty

            stack.addVMERead(base + 0x6092, amod, dw); // event counter low
            stack.addVMERead(base + 0x6094, amod, dw); // event counter high

            stack.addVMEWrite(mcst + 0x6034, 1, amod, dw); // readout reset

            struct Options options;
            options.ignoreDelays = false;
            options.noBatching = false;

            //std::function<bool (const std::error_code &ec)> abortPredicate = is_connection_error;

            size_t szMin = std::numeric_limits<size_t>::max(), szMax = 0, szSum = 0, iterations = 0;
            std::vector<u32> response;

            for (int i=0; i<1; i++)
            {
                response.clear();
                auto ec = execute_stack(mvlc, stack, stacks::StackMemoryWords, options, /* abortPredicate, */ response);

                if (ec)
                    throw ec;

                size_t size = response.size();
                szMin = std::min(szMin, size);
                szMax = std::max(szMax, size);
                szSum += size;
                ++iterations;
                //log_buffer(cout, response, "mtdc readout_test response");
                cout << "response.size() = " << response.size() << std::endl;

                auto parsedResults = parse_response(stack, response);

                const auto commands = stack.getCommands();

                ASSERT_EQ(parsedResults.size(), commands.size());

                for (size_t i=0; i<commands.size(); ++i)
                {
                    ASSERT_EQ(commands[i], parsedResults[i].cmd);

                    if (commands[i].type == StackCommand::CommandType::VMERead)
                    {
                        //ASSERT_TRUE(parsedResults[i].response.size() >= 1);

                        //log_buffer(cout, parsedResults[i].response,
                        //           "response of '" + to_string(parsedResults[i].cmd) + "'");
                    }
                    else
                    {
                        ASSERT_TRUE(parsedResults[i].response.empty());
                    }
                }

#if 1
                std::cout << "parse_response: results.size() = " << parsedResults.size() << std::endl;

                for (size_t i=0; i < parsedResults.size(); ++i)
                {
                    std::cout << "result #" << i << ", cmd=" << to_string(parsedResults[i].cmd) << std::endl;

                    if (!vme_amods::is_block_mode(parsedResults[i].cmd.amod))
                    {
                        for (const auto &value: parsedResults[i].response)
                            std::cout << fmt::format("  {:#010x}", value) << std::endl;
                    }
                    else
                        std::cout << "size=" << parsedResults[i].response.size() << " words" << endl;

                    std::cout << std::endl;
                }
#endif
            }

#if 1
            double szAvg = szSum / (iterations * 1.0);

            cout << endl;
            cout << fmt::format("iterations={}, szSum={}, szMin={}, szMax={}, szAvg={}",
                                iterations, szSum, szMin, szMax, szAvg
                               ) << std::endl;
            cout << endl;
#endif
        }
    }
}
