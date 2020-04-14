#ifndef __MVLC_STACK_EXECUTOR_H__
#define __MVLC_STACK_EXECUTOR_H__

#include <cassert>
#include <iostream>
#include <functional>
#include <numeric>
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

inline size_t get_encoded_size(const std::vector<StackCommand> &commands)
{
    size_t encodedPartSize = 2 + std::accumulate(
        std::begin(commands), std::end(commands), static_cast<size_t>(0u),
        [] (const size_t &encodedSize, const StackCommand &cmd)
        {
            return encodedSize + get_encoded_size(cmd);
        });

    return encodedPartSize;
}

template<typename C, typename Predicate>
std::vector<std::vector<typename C::value_type>>
conditional_split(const C &container, Predicate split_predicate)
{
    using PartType = std::vector<typename C::value_type>;

    std::vector<PartType> result;

    auto first = std::begin(container);
    const auto end = std::end(container);

    while (first < end)
    {
        auto next = first;

        while (next < end)
        {
            if (split_predicate(*next))
                break;

            ++next;
        }

        //ViewType part(&(*first), next - first);
        PartType part;
        std::copy(first, next, std::back_inserter(part));
        result.emplace_back(part);
        first = next; // advance
    }

    return result;
}

inline std::vector<std::vector<StackCommand>> split_commands(
    const std::vector<StackCommand> &commands,
    const u16 immediateStackMaxSize = stacks::ImmediateStackReservedWords)
{
    struct SplitPredicateState
    {
        size_t encodedSize = 2;
        bool lastCommandWasSoftwareDelay = false;
    };

    SplitPredicateState predState;

    auto split_predicate = [&predState, &immediateStackMaxSize]
        (const StackCommand &nextCmd) -> bool
    {
        std::cout << to_string(nextCmd) << std::endl;
        // Returning false means that nextCmd should be added to the current
        // part. Returning true means that a new split should be created.
        bool ret = false;

        // Check if adding the next command to the current split would push the
        // encoded stacks size over immediateStackMaxSize.
        if (predState.encodedSize + get_encoded_size(nextCmd) > immediateStackMaxSize)
            ret = true;
        else
            predState.encodedSize += get_encoded_size(nextCmd);

        // If the previous command was a software delay we terminate the split
        // now. This means software delays will be positioned at the end of
        // each split.
        if (predState.lastCommandWasSoftwareDelay)
            ret = true;

        // Update state
        predState.lastCommandWasSoftwareDelay = (
            nextCmd.type == StackCommand::CommandType::SoftwareDelay);

        // Reset state if we tell the algorithm to terminate the current split.
        if (ret)
            predState.encodedSize = 2;

        return ret;
    };

    auto result = conditional_split(commands, split_predicate);

    for (const auto &part: result)
        assert(get_encoded_size(part) <= immediateStackMaxSize);

    return result;
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
