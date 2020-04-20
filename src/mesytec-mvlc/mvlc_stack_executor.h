#ifndef __MVLC_STACK_EXECUTOR_H__
#define __MVLC_STACK_EXECUTOR_H__

#include <cassert>
#include <chrono>
#include <iostream>
#include <functional>
#include <iterator>
#include <numeric>
#include <system_error>
#include <thread>

#include "mvlc_buffer_validators.h"
#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "mvlc_error.h"
#include "mvlc_util.h"
#include "util/string_view.hpp"
#include "vme_constants.h"

namespace mesytec
{
namespace mvlc
{

using namespace nonstd;

struct Result
{
    StackCommand cmd;
    size_t groupIndex = 0;
    std::error_code ec;
    std::vector<u32> response;
    bool isValid = false;

    explicit Result() {};

    explicit Result(const StackCommand &cmd_)
        : cmd(cmd_)
        , isValid(true)
    {}

    explicit Result(const std::error_code &ec_)
        : ec(ec_)
        , isValid(true)
    {}

    void clear() { isValid = false; }
    bool valid() const { return isValid; }
};

struct Options
{
    bool ignoreDelays = false;
    bool noBatching  = false;
    bool contineOnVMEError = false;
};

//using AbortPredicate = std::function<bool (const std::error_code &ec)>;

namespace detail
{

template<typename DIALOG_API>
    std::error_code stack_transaction(
        DIALOG_API &mvlc, const std::vector<StackCommand> &commands,
        std::vector<u32> &responseDest)
{
    if (auto ec = mvlc.uploadStack(CommandPipe, 0, commands, responseDest))
        return ec;

    return mvlc.execImmediateStack(0, responseDest);
}

//template<typename DIALOG_API>
//    std::error_code stack_transaction(
//        DIALOG_API &mvlc, const StackCommandBuilder &stack,
//        std::vector<u32> &responseDest)
//{
//    return stack_transaction(mvlc, stack.getCommands(), responseDest);
//}

inline bool is_sw_delay (const StackCommand &cmd)
{
    return cmd.type == StackCommand::CommandType::SoftwareDelay;
};

MESYTEC_MVLC_EXPORT std::vector<std::vector<StackCommand>> split_commands(
    const std::vector<StackCommand> &commands,
    const Options &options = {},
    const u16 immediateStackMaxSize = stacks::ImmediateStackReservedWords);

template<typename DIALOG_API>
std::error_code run_part(
    DIALOG_API &mvlc,
    const std::vector<StackCommand> &part,
    const Options &options,
    std::vector<u32> &responseDest)
{
    if (part.empty())
        throw std::runtime_error("empty command stack part");

    if (is_sw_delay(part[0]))
    {
        if (!options.ignoreDelays)
        {
            std::cout << "run_part: delaying for " << part[0].value << " ms" << std::endl;
            std::chrono::milliseconds delay(part[0].value);
            std::this_thread::sleep_for(delay);
        }

        responseDest.resize(0);

        return {};
    }

    return stack_transaction(mvlc, part, responseDest);
}

template<typename DIALOG_API>
std::error_code run_parts(
    DIALOG_API &mvlc,
    const std::vector<std::vector<StackCommand>> &parts,
    const Options &options,
    //AbortPredicate abortPredicate,
    std::vector<u32> &combinedResponses)
{
    std::error_code ret;
    std::vector<u32> responseBuffer;

    for (const auto &part: parts)
    {
        auto ec = run_part(mvlc, part, options, responseBuffer);

        std::copy(std::begin(responseBuffer), std::end(responseBuffer),
                  std::back_inserter(combinedResponses));

        if (ec && !ret)
            ret = ec;

        //if (abortPredicate && abortPredicate(ec))
        //    break;
    }

    return ret;
}

#if 0
struct ParsedResponse
{
    std::error_code ec;
    std::vector<Result> results;
};

template<typename DIALOG_API>
    ParsedResponse execute_commands(
        DIALOG_API &mvlc, const std::vector<StackCommand> &commands)
{
    ParsedResponse pr;
    std::vector<u32> responseBuffer;

    pr.ec = stack_transaction(mvlc, commands, responseBuffer);

    if (pr.ec && pr.ec != ErrorType::VMEError)
        return pr;

    basic_string_view<u32> response(responseBuffer.data(), responseBuffer.size());

    for (const auto &cmd: commands)
    {
        using CT = StackCommand::CommandType;

        Result result(cmd);

        switch (cmd.type)
        {
            case CT::StackStart:
            case CT::StackEnd:
            case CT::SoftwareDelay:
                break;

            case CT::VMERead:
            case CT::VMEWrite:
            case CT::WriteSpecial:
            case CT::WriteMarker:
                break;
        }
    }

    return pr;
}
#endif

} // end namespace detail

template<typename DIALOG_API>
std::error_code execute_stack(
    DIALOG_API &mvlc,
    const StackCommandBuilder &stack,
    u16 immediateStackMaxSize,
    const Options &options,
    //AbortPredicate &abortPredicate,
    std::vector<u32> &responseBuffer)
{
    if (stack.getGroupCount() == 0)
        return {};

    auto commands = stack.getCommands();

    auto parts = detail::split_commands(commands, options, immediateStackMaxSize);

    if (parts.empty())
        return {};

    responseBuffer.clear();

    auto ec = detail::run_parts(mvlc, parts, options, /* abortPredicate, */ responseBuffer);

    return ec;
}

std::vector<Result> MESYTEC_MVLC_EXPORT parse_response(
    const StackCommandBuilder &stack, const std::vector<u32> &responseBuffer);


#if 0
    while (!response.empty())
    {
        const auto groupEnd = std::end(group.get().commands);

        if (cmdIter == groupEnd)
        {
            if (++groupIndex >= stack.getGroupCount())
                throw std::runtime_error("execute_stack: groupIndex out of range (response too long?)");

            group = std::ref(stack.getGroup(groupIndex));
            cmdIter = std::begin(group.get().commands);
        }

        assert(cmdIter != groupEnd);

        using CT = StackCommand::CommandType;

        Result result(*cmdIter);

        switch (cmdIter->type)
        {
            case CT::StackStart:
            case CT::StackEnd:
            case CT::SoftwareDelay:
                break;

            case CT::VMERead:
                if (!vme_amods::is_block_mode(cmdIter->amod))
                {
                    result.response = { response[0] };
                    response.remove_prefix(1);
                }
                else
                {
                    u32 header = response[0];
                    if (!is_stack_buffer(header))
                        throw std::runtime_error("execute_stack: expected stack buffer response");

                    do
                    {
                    } while (is_stack_buffer_continuation(header));

                }
            case CT::VMEWrite:
            case CT::WriteSpecial:
            case CT::WriteMarker:
                break;
        }

    }
#endif

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_STACK_EXECUTOR_H__ */
