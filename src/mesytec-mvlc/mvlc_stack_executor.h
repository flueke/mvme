#ifndef __MVLC_STACK_EXECUTOR_H__
#define __MVLC_STACK_EXECUTOR_H__

#include <cassert>
#include <iostream>
#include <functional>
#include <system_error>

#include "mvlc_buffer_validators.h"
#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "mvlc_error.h"
#include "mvlc_util.h"
#include "util/string_view.hpp"

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

    explicit Result(const StackCommand &cmd_)
        : cmd(cmd_)
    {}

    explicit Result(const std::error_code &ec_)
        : ec(ec_)
    {}
};

struct Options
{
    using opt = unsigned;

    static constexpr opt IgnoreDelays = 1u << 0;
    static constexpr opt NoBatching = 1u << 1;
};

using AbortPredicate = std::function<bool (const std::error_code &ec)>;

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

template<typename DIALOG_API>
    std::error_code stack_transaction(
        DIALOG_API &mvlc, const StackCommandBuilder &stack,
        std::vector<u32> &responseDest)
{
    return stack_transaction(mvlc, stack.getCommands(), responseDest);
}

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

} // end namespace detail

#if 0
template<typename DIALOG_API>
std::vector<Result>
execute_stack(DIALOG_API &mvlc, const StackCommandBuilder &stack, const Options &options, AbortPredicate &pred)
{
}
#endif

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_STACK_EXECUTOR_H__ */
