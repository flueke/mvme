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
    bool ignoreDelays = false;
    bool noBatching  = false;
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

inline size_t get_encoded_stack_size(const std::vector<StackCommand> &commands)
{
    size_t encodedPartSize = 2 + std::accumulate(
        std::begin(commands), std::end(commands), static_cast<size_t>(0u),
        [] (const size_t &encodedSize, const StackCommand &cmd)
        {
            return encodedSize + get_encoded_size(cmd);
        });

    return encodedPartSize;
}

// Split the elements of an input container into parts using a predicate to
// decide when to terminate each part.
//
// Each element of the input container is passed to the predicate.  If the
// predicate returns true the current part is terminated and a new one is
// started. Otherwise the current input container element is added to the
// current part.
template<typename Container, typename Predicate>
std::vector<Container> conditional_split(
    const Container &container, Predicate terminate_part_predicate)
{
    std::vector<Container> result;

    auto first = std::begin(container);
    const auto end = std::end(container);

    while (first < end)
    {
        auto next = first;

        while (next < end)
        {
            if (terminate_part_predicate(*next))
                break;

            ++next;
        }

        if (first == next)
            throw std::runtime_error("conditional_split produced an empty part");

        Container part;
        std::copy(first, next, std::back_inserter(part));
        result.emplace_back(part);
        first = next; // advance
    }

    return result;
}

inline std::vector<std::vector<StackCommand>> split_commands(
    const std::vector<StackCommand> &commands,
    const Options &options = {},
    const u16 immediateStackMaxSize = stacks::ImmediateStackReservedWords)
{
#if 0
    struct SplitPredicateState
    {
        // Start with two encoded words for StackStart and StackEnd
        size_t encodedSize = 2;

        // We want SoftwareDelay commands to be added to the current part and
        // then terminate the part. This way all SoftwareDelay commands will be
        // at the very end of each part. Multiple successive SoftwareDelays
        // will result in parts of size 1 for each but the first delay command.

        StackCommand lastAddedCommand;
    };

    SplitPredicateState predState;

    auto split_predicate = [&predState, &immediateStackMaxSize]
        (const StackCommand &nextCmd) -> bool
    {
        auto is_sw_delay = [] (const StackCommand &cmd) -> bool
        {
            return cmd.type == StackCommand::CommandType::SoftwareDelay;
        };
#if 0
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
        if (predState.lastAddedCommand.type == StackCommand::CommandType::SoftwareDelay)
            ret = true;

        // Update state
        predState.lastAddedCommand = nextCmd;

        if (ret && predState.lastAddedCommand.type == StackCommand::CommandType::SoftwareDelay)
            ret = false;

        // Reset state if we tell the algorithm to terminate the current split.
        if (ret)
            predState.encodedSize = 2;

        return ret;
#else
        // Returning false means that nextCmd should be added to the current
        // part. Returning true means that a new split should be created.

        if (is_sw_delay(predState.lastAddedCommand))
        {
            if (is_sw_delay(nextCmd))
            {
                predState.encodedSize += get_encoded_size(nextCmd);
                return false;
            }
            else
            {
                predState.encodedSize = 2;
                return true;
            }
        }

        bool ret = true;

        if (predState.encodedSize + get_encoded_size(nextCmd) <= immediateStackMaxSize)
        {
            predState.encodedSize += get_encoded_size(nextCmd);
            predState.lastAddedCommand = nextCmd;
            ret = false;
        }


        if (ret)
            predState.encodedSize = 2;

        return ret;
#endif
    };

    auto result = conditional_split(commands, split_predicate);

    for (const auto &part: result)
        assert(get_encoded_stack_size(part) <= immediateStackMaxSize);

    return result;
#else
    auto is_sw_delay = [] (const StackCommand &cmd) -> bool
    {
        return cmd.type == StackCommand::CommandType::SoftwareDelay;
    };

    std::vector<std::vector<StackCommand>> result;

    auto first = std::begin(commands);
    const auto end = std::end(commands);

    while (first < end)
    {
        auto next = first;
        size_t encodedSize = 2u;

        while (next < end)
        {
            if (encodedSize + get_encoded_size(*next) > immediateStackMaxSize)
                break;

            encodedSize += get_encoded_size(*next);

            auto prev = next++;

            if (options.noBatching)
                break;

            if (!options.ignoreDelays && is_sw_delay(*prev))
                break;
        }

        if (first == next)
            throw std::runtime_error("split_commands produced an empty part");

        std::vector<StackCommand> part;
        std::copy(first, next, std::back_inserter(part));

        assert(get_encoded_stack_size(part) <= immediateStackMaxSize);

        if (options.noBatching)
            assert(part.size() == 1);

        result.emplace_back(part);

        first = next; // advance
    }

    return result;
#endif
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
