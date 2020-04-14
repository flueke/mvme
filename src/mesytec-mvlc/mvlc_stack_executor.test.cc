#include <chrono>
#include <iostream>
#include <fmt/format.h>

#include "gtest/gtest.h"

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
        auto mvlc = make_mvlc_eth("mvlc-0007");

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


            for (int i=0; i<505; i++)
                stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            stack.addVMERead(vmeBase + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            stack.addVMERead(vmeBaseNoModule + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            stack.addVMERead(vmeBaseNoModule + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            stack.addVMERead(vmeBase + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            std::vector<u32> response;

            auto ec = stack_transaction(mvlc, stack, response);

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

            auto parts = partition_commands(stack, stacks::StackMemoryWords / 2);

            cout << "partition_commands returned " << parts.size() << " parts:" << endl;
            for (const auto &part: parts)
                cout << " size=" << part.size() << ", encodedSize=" << get_encoded_size(part) << endl;
        }
    }
    //catch (const std::error_code &ec)
    //{
    //    cout << "std::error_code thrown: " << ec.message() << endl;
    //    throw;
    //}
}
