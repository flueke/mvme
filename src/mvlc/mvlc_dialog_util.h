#ifndef __MVME_MVLC_DIALOG_UTIL_H__
#define __MVME_MVLC_DIALOG_UTIL_H__

#include <utility>
#include <vector>

#include "mvlc/mvlc_constants.h"
#include "mvlc/mvlc_error.h"

namespace mesytec
{
namespace mvlc
{

template<typename DIALOG_API>
std::pair<std::vector<u32>, std::error_code>
read_stack_contents(DIALOG_API &mvlc, u16 startAddress)
{
    u32 stackHeader = 0u;

    if (auto ec = mvlc.readRegister(startAddress, stackHeader))
        return std::make_pair(std::vector<u32>{}, ec);

    std::vector<u32> contents;
    contents.reserve(64);
    contents.push_back(stackHeader);

    u8 headerType = (stackHeader >> CmdShift) & CmdMask; // 0xF3

    if (headerType != commands::StackStart)
        return { contents, make_error_code(MVLCErrorCode::InvalidStackHeader) };

    u32 addr  = startAddress + AddressIncrement;
    u32 value = 0u;

    do
    {
        if (addr >= stacks::StackMemoryEnd)
            return { contents, make_error_code(MVLCErrorCode::StackMemoryExceeded) };

        if (auto ec = mvlc.readRegister(addr, value))
            return { contents, ec };

        contents.push_back(value);
        addr += AddressIncrement;

    } while (((value >> CmdShift) & CmdMask) != commands::StackEnd); // 0xF4

    return { contents, {} };
}

struct StackInfo
{
    u8 id;
    u32 triggers;
    u32 offset;
    u16 triggerReg;
    u16 offsetReg;
    u16 startAddress;
    std::vector<u32> contents;
};

template<typename DIALOG_API>
std::pair<StackInfo, std::error_code>
read_stack_info(DIALOG_API &mvlc, u8 id)
{
    StackInfo result = {};
    result.id = id;

    if (id >= stacks::StackCount)
        return { result, make_error_code(MVLCErrorCode::StackCountExceeded) };

    result.triggerReg = stacks::get_trigger_register(id);
    result.offsetReg  = stacks::get_offset_register(id);

    if (auto ec = mvlc.readRegister(result.triggerReg, result.triggers))
        return { result, ec };

    if (auto ec = mvlc.readRegister(result.offsetReg, result.offset))
        return { result, ec };

    result.startAddress = stacks::StackMemoryBegin + result.offset;

    auto sc = read_stack_contents(mvlc, result.startAddress);

    result.contents = sc.first;

    return { result, sc.second };
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_DIALOG_UTIL_H__ */
