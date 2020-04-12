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
    auto stackBuffer = make_stack_buffer(commands);

    if (stackBuffer.size() > stacks::StackMemoryWords)
        return make_error_code(MVLCErrorCode::StackMemoryExceeded);

    // TODO: partition those into chunks that when converted to a command
    // buffer are at most (MirrorTransactionMaxWords) words long.
    auto uploadCommands = make_stack_upload_commands(CommandPipe, 0, stackBuffer);

    auto firstCommand = std::begin(uploadCommands);
    const auto endOfBuffer = std::end(uploadCommands);
    size_t partCount = 0u;

    while (firstCommand < endOfBuffer)
    {
        auto lastCommand = firstCommand;
        size_t encodedSize = 0u;

        while (lastCommand < endOfBuffer)
        {
            if (encodedSize + get_encoded_size(*lastCommand) > MirrorTransactionMaxContentsWords)
                break;

            encodedSize += get_encoded_size(*lastCommand++);
        }

        assert(encodedSize <= MirrorTransactionMaxContentsWords);

        basic_string_view<SuperCommand> part(&(*firstCommand), lastCommand - firstCommand);

        auto request = make_command_buffer(part);

        std::cout << __PRETTY_FUNCTION__ << "part #" << partCount++ << ", request.size() = " << request.size() << std::endl;

        assert(request.size() <= MirrorTransactionMaxWords);

        if (auto ec = mvlc.mirrorTransaction(request, responseDest))
            return ec;

        firstCommand = lastCommand;
    }

    std::cout << __PRETTY_FUNCTION__ << "stack upload done in " << partCount << " parts" << std::endl;

    assert(firstCommand == endOfBuffer);

    // set the stack 0 offset register
    if (auto ec = mvlc.writeRegister(stacks::Stack0OffsetRegister, 0))
        return ec;

    // exec stack 0
    if (auto ec = mvlc.writeRegister(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift))
        return ec;

    // read the stack response into the supplied buffer
    if (auto ec = mvlc.readResponse(is_stack_buffer, responseDest))
    {
        std::cout << "stackTransaction: is_stack_buffer header validation failed" << std::endl;
        return ec;
    }

    assert(!responseDest.empty()); // guaranteed by readResponse()

    // Test if the Continue bit is set and if so read continuation buffers
    // (0xF9) until the Continue bit is cleared.
    // Note: stack error notification buffers (0xF7) as part of the response are
    // handled in readResponse().

    u32 header = responseDest[0];
    u8 flags = extract_frame_info(header).flags;

    if (flags & frame_flags::Continue)
    {
        std::vector<u32> localBuffer;

        while (flags & frame_flags::Continue)
        {
            if (auto ec = mvlc.readResponse(is_stack_buffer_continuation, localBuffer))
            {
                std::cout << "stackTransaction: is_stack_buffer_continuation header validation failed" << std::endl;
                return ec;
            }

            std::copy(localBuffer.begin(), localBuffer.end(), std::back_inserter(responseDest));

            header = !localBuffer.empty() ? localBuffer[0] : 0u;
            flags = extract_frame_info(header).flags;
        }
    }

    if (flags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    if (flags & frame_flags::SyntaxError)
        return MVLCErrorCode::StackSyntaxError;

    return {};
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
