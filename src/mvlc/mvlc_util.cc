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

static QString format_buffer_error_bits(u8 errorBits)
{
    if (!errorBits)
        return "none";

    QStringList buffer;

    if (errorBits & buffer_flags::Continue)
        buffer << "continue";

    if (errorBits & buffer_flags::SyntaxError)
        buffer << "syntax";

    if (errorBits & buffer_flags::BusError)
        buffer << "BERR";

    if (errorBits & buffer_flags::Timeout)
        buffer << "timeout";

    return buffer.join(",");
}

QString decode_response_header(u32 header)
{
    QString result;
    QTextStream ss(&result);
    u8 type = (header >> buffer_headers::TypeShift) & buffer_headers::TypeMask;
    u16 len = header & buffer_headers::LengthMask;

    switch (type)
    {
        case buffer_headers::SuperBuffer:
            ss << "Super Buffer (len=" << len;
            break;

        case buffer_headers::StackBuffer:
            ss << "Stack Result Buffer (len=" << len;
            break;

        case buffer_headers::BlockRead:
            ss << "Block Read Buffer (len=" << len;
            break;

        case buffer_headers::StackError:
            ss << "Stack Error Buffer (len=" << len;
            break;

        default:
            return result;
    }

    switch (type)
    {
        case buffer_headers::StackBuffer:
        case buffer_headers::BlockRead:
        case buffer_headers::StackError:
            {
                u16 stackNum = (header >> buffer_headers::StackNumShift) & buffer_headers::StackNumMask;
                ss << ", stackNum=" << stackNum;
            }
            break;
    }

    u8 errorBits = (header >> buffer_headers::ErrorShift) & buffer_headers::ErrorMask;

    ss << ", errorBits=" << format_buffer_error_bits(errorBits) << ")";

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

} // end namespace mvlc
} // end namespace mesytec
