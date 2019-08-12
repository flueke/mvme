#include "mvlc_util.h"

#include <QDebug>
#include <iostream>
#include <cassert>

using namespace mesytec::mvlc;

namespace mesytec
{
namespace mvlc
{

//
// vme_script -> mvlc
//
VMEDataWidth convert_data_width(vme_script::DataWidth width)
{
    switch (width)
    {
        case vme_script::DataWidth::D16: return VMEDataWidth::D16;
        case vme_script::DataWidth::D32: return VMEDataWidth::D32;
    }

    return VMEDataWidth::D16;
}

vme_script::DataWidth convert_data_width(VMEDataWidth dataWidth)
{
    switch (dataWidth)
    {
        case D16: return vme_script::DataWidth::D16;
        case D32: return vme_script::DataWidth::D32;
    }

    throw std::runtime_error("invalid mvlc::VMEDataWidth given");
}

QVector<u32> build_stack(const vme_script::VMEScript &script, u8 outPipe)
{
    QVector<u32> result;

    u32 firstWord = commands::StackStart << CmdShift | outPipe << CmdArg0Shift;
    result.push_back(firstWord);

    for (auto &cmd: script)
    {
        using vme_script::CommandType;

        switch (cmd.type)
        {
            case CommandType::Invalid:
            case CommandType::SetBase:
            case CommandType::ResetBase:
                break;

            case CommandType::Write:
            case CommandType::WriteAbs:
                {
                    firstWord = commands::VMEWrite << CmdShift;
                    firstWord |= cmd.addressMode << CmdArg0Shift;
                    firstWord |= convert_data_width(cmd.dataWidth) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                    result.push_back(cmd.value);
                } break;

            case CommandType::Read:
                {
                    firstWord = commands::VMERead << CmdShift;
                    firstWord |= cmd.addressMode << CmdArg0Shift;
                    firstWord |= convert_data_width(cmd.dataWidth) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                } break;

            case CommandType::BLT:
            case CommandType::BLTFifo:
                {
                    firstWord = commands::VMERead << CmdShift;
                    firstWord |= vme_address_modes::BLT32 << CmdArg0Shift;
                    firstWord |= (cmd.transfers & CmdArg1Mask) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                } break;

            case CommandType::MBLT:
            case CommandType::MBLTFifo:
                {
                    firstWord = commands::VMERead << CmdShift;
                    firstWord |= vme_address_modes::MBLT64 << CmdArg0Shift;
                    firstWord |= (cmd.transfers & CmdArg1Mask) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                } break;

            case CommandType::Blk2eSST64:
                {
                    firstWord = commands::VMERead << CmdShift;
                    firstWord |= (vme_address_modes::Blk2eSST64 | (cmd.blk2eSSTRate << Blk2eSSTRateShift))
                        << CmdArg0Shift;
                    firstWord |= (cmd.transfers & CmdArg1Mask) << CmdArg1Shift;
                    result.push_back(firstWord);
                    result.push_back(cmd.address);
                } break;

            case CommandType::Marker:
                {
                    firstWord = commands::WriteMarker << CmdShift;
                    result.push_back(firstWord);
                    result.push_back(cmd.value);
                } break;

            case CommandType::MVLC_WriteSpecial:
                {
                    firstWord = commands::WriteSpecial << CmdShift;
                    firstWord |= cmd.value & 0x00FFFFFFu;
                    result.push_back(firstWord);
                } break;

            case CommandType::Wait:
            case CommandType::BLTCount:
            case CommandType::BLTFifoCount:
            case CommandType::MBLTCount:
            case CommandType::MBLTFifoCount:
            case CommandType::VMUSB_ReadRegister:
            case CommandType::VMUSB_WriteRegister:
                qDebug() << __FUNCTION__ << " unsupported VME Script command:"
                    << to_string(cmd.type);
                break;
        }
    }

    firstWord = commands::StackEnd << CmdShift;
    result.push_back(firstWord);

    return result;
}

QVector<u32> build_upload_commands(const vme_script::VMEScript &script, u8 outPipe,
                                   u16 startAddress)
{
    auto stack = build_stack(script, outPipe);
    return build_upload_commands(stack, startAddress);
}

QVector<u32> build_upload_commands(const QVector<u32> &stack, u16 startAddress)
{
    QVector<u32> result;
    result.reserve(stack.size() * 2);

    u16 address = startAddress;

    for (u32 stackValue: stack)
    {
        u32 cmdValue = super_commands::WriteLocal << SuperCmdShift;
        cmdValue |= address;
        address += AddressIncrement;
        result.push_back(cmdValue);
        result.push_back(stackValue);
    }

    return result;
}

QVector<u32> build_upload_command_buffer(const vme_script::VMEScript &script, u8 outPipe,
                                         u16 startAddress)
{
    auto stack = build_stack(script, outPipe);
    return build_upload_command_buffer(stack, startAddress);
}

QVector<u32> build_upload_command_buffer(const QVector<u32> &stack, u16 startAddress)
{
    QVector<u32> result;
    auto uploadData = build_upload_commands(stack, startAddress);
    result.reserve(uploadData.size() + 2);
    result.push_back(super_commands::CmdBufferStart << SuperCmdShift);
    std::copy(uploadData.begin(), uploadData.end(), std::back_inserter(result));
    result.push_back(super_commands::CmdBufferEnd << SuperCmdShift);

    return result;
}

QString format_frame_flags(u8 frameFlags)
{
    if (!frameFlags)
        return "none";

    QStringList buffer;

    if (frameFlags & frame_flags::Continue)
        buffer << "continue";

    if (frameFlags & frame_flags::SyntaxError)
        buffer << "syntax";

    if (frameFlags & frame_flags::BusError)
        buffer << "BERR";

    if (frameFlags & frame_flags::Timeout)
        buffer << "timeout";

    return buffer.join(",");
}

QString decode_frame_header(u32 header)
{
    QString result;
    QTextStream ss(&result);

    auto headerInfo = extract_frame_info(header);

    switch (static_cast<frame_headers::FrameTypes>(headerInfo.type))
    {
        case frame_headers::SuperFrame:
            ss << "Super Frame (len=" << headerInfo.len;
            break;

        case frame_headers::StackFrame:
            ss << "Stack Result Frame (len=" << headerInfo.len;
            break;

        case frame_headers::BlockRead:
            ss << "Block Read Frame (len=" << headerInfo.len;
            break;

        case frame_headers::StackError:
            ss << "Stack Error Frame (len=" << headerInfo.len;
            break;

        case frame_headers::StackContinuation:
            ss << "Stack Result Continuation Frame (len=" << headerInfo.len;
            break;

        case frame_headers::SystemEvent:
            ss << "System Event (len=" << headerInfo.len;
            break;
    }

    switch (static_cast<frame_headers::FrameTypes>(headerInfo.type))
    {
        case frame_headers::StackFrame:
        case frame_headers::BlockRead:
        case frame_headers::StackError:
        case frame_headers::StackContinuation:
            {
                u16 stackNum = (header >> frame_headers::StackNumShift) & frame_headers::StackNumMask;
                ss << ", stackNum=" << stackNum;
            }
            break;

        case frame_headers::SuperFrame:
        case frame_headers::SystemEvent:
            break;
    }

    u8 frameFlags = (header >> frame_headers::FrameFlagsShift) & frame_headers::FrameFlagsMask;

    ss << ", frameFlags=" << format_frame_flags(frameFlags) << ")";

    return result;
}

void log_buffer(const u32 *buffer, size_t size, const std::string &info)
{
    using std::cout;
    using std::endl;

    cout << "begin " << info << " (size=" << size << ")" << endl;

    for (size_t i = 0; i < size; i++)
    {
        //printf("  %3lu: 0x%08x\n", i, buffer[i]);
        printf("  0x%08X\n", buffer[i]);
    }

    cout << "end " << info << endl;
}

void log_buffer(const std::vector<u32> &buffer, const std::string &info)
{
    log_buffer(buffer.data(), buffer.size(), info);
}

void log_buffer(const QVector<u32> &buffer, const QString &info)
{
    std::vector<u32> vec;
    std::copy(buffer.begin(), buffer.end(), std::back_inserter(vec));
    log_buffer(vec, info.toStdString());
}

const char *get_system_event_subtype_name(u8 subtype_)
{
    switch (subtype_)
    {
        using namespace system_event::subtype;

        case EndianMarker:
            return "EndianMarker";

        case VMEConfig:
            return "VMEConfig";

        case UnixTimestamp:
            return "UnixTimestamp";

        case Pause:
            return "Pause";

        case Resume:
            return "Resume";

        case EndOfFile:
            return "EndOfFile";
    }

    return "unknown system event subtype";
}

const char *get_frame_flag_shift_name(u8 flag_shift)
{
    if (flag_shift == frame_flags::shifts::Timeout)
        return "Timeout";

    if (flag_shift == frame_flags::shifts::BusError)
        return "BusError";

    if (flag_shift == frame_flags::shifts::SyntaxError)
        return "SyntaxError";

    if (flag_shift == frame_flags::shifts::Continue)
        return "Continue";

    return "Unknown";
}

stacks::TimerBaseUnit timer_base_unit_from_string(const QString &str_)
{
    auto str = str_.toLower();

    if (str == "ns")
        return stacks::TimerBaseUnit::ns;

    if (str == "us" || str == "µs")
        return stacks::TimerBaseUnit::us;

    if (str == "ms")
        return stacks::TimerBaseUnit::ms;

    if (str == "s")
        return stacks::TimerBaseUnit::s;

    return {};
}

} // end namespace mvlc
} // end namespace mesytec
