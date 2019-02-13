#include "mvlc_usb.h"

#include <cassert>
#include <ftd3xx.h>
#include <iostream>
#include <memory>

#include "mvlc_script.h"
#include "mvlc_util.h"

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec;


// m_trace
#if 1

#define m_trace(fmt, ...)\
do\
{\
    fprintf(stderr, "m_trace: %s() " fmt, __FUNCTION__, ##__VA_ARGS__);\
} while (0);

#else

#define m_trace(...)

#endif

namespace
{

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

using mesytec::mvlc::usb::err_t;

const char *ft_error_str(err_t error)
{
    switch (static_cast<_FT_STATUS>(error))
    {
        case FT_OK:                                 return "FT_OK";
        case FT_INVALID_HANDLE:                     return "FT_INVALID_HANDLE";
        case FT_DEVICE_NOT_FOUND:                   return "FT_DEVICE_NOT_FOUND";
        case FT_DEVICE_NOT_OPENED:                  return "FT_DEVICE_NOT_OPENED";
        case FT_IO_ERROR:                           return "FT_IO_ERROR";
        case FT_INSUFFICIENT_RESOURCES:             return "FT_INSUFFICIENT_RESOURCES";
        case FT_INVALID_PARAMETER: /* 6 */          return "FT_INVALID_PARAMETER";
        case FT_INVALID_BAUD_RATE:                  return "FT_INVALID_BAUD_RATE";
        case FT_DEVICE_NOT_OPENED_FOR_ERASE:        return "FT_DEVICE_NOT_OPENED_FOR_ERASE";
        case FT_DEVICE_NOT_OPENED_FOR_WRITE:        return "FT_DEVICE_NOT_OPENED_FOR_WRITE";
        case FT_FAILED_TO_WRITE_DEVICE: /* 10 */    return "FT_FAILED_TO_WRITE_DEVICE";
        case FT_EEPROM_READ_FAILED:                 return "FT_EEPROM_READ_FAILED";
        case FT_EEPROM_WRITE_FAILED:                return "FT_EEPROM_WRITE_FAILED";
        case FT_EEPROM_ERASE_FAILED:                return "FT_EEPROM_ERASE_FAILED";
        case FT_EEPROM_NOT_PRESENT:                 return "FT_EEPROM_NOT_PRESENT";
        case FT_EEPROM_NOT_PROGRAMMED:              return "FT_EEPROM_NOT_PROGRAMMED";
        case FT_INVALID_ARGS:                       return "FT_INVALID_ARGS";
        case FT_NOT_SUPPORTED:                      return "FT_NOT_SUPPORTED";

        case FT_NO_MORE_ITEMS:                      return "FT_NO_MORE_ITEMS";
        case FT_TIMEOUT: /* 19 */                   return "FT_TIMEOUT";
        case FT_OPERATION_ABORTED:                  return "FT_OPERATION_ABORTED";
        case FT_RESERVED_PIPE:                      return "FT_RESERVED_PIPE";
        case FT_INVALID_CONTROL_REQUEST_DIRECTION:  return "FT_INVALID_CONTROL_REQUEST_DIRECTION";
        case FT_INVALID_CONTROL_REQUEST_TYPE:       return "FT_INVALID_CONTROL_REQUEST_TYPE";
        case FT_IO_PENDING:                         return "FT_IO_PENDING";
        case FT_IO_INCOMPLETE:                      return "FT_IO_INCOMPLETE";
        case FT_HANDLE_EOF:                         return "FT_HANDLE_EOF";
        case FT_BUSY:                               return "FT_BUSY";
        case FT_NO_SYSTEM_RESOURCES:                return "FT_NO_SYSTEM_RESOURCES";
        case FT_DEVICE_LIST_NOT_READY:              return "FT_DEVICE_LIST_NOT_READY";
        case FT_DEVICE_NOT_CONNECTED:               return "FT_DEVICE_NOT_CONNECTED";
        case FT_INCORRECT_DEVICE_PATH:              return "FT_INCORRECT_DEVICE_PATH";

        case FT_OTHER_ERROR:                        return "FT_OTHER_ERROR";

#if 0
        case MVLC_SHORT_WRITE:                      return "MVLC_SHORT_WRITE";
        case MVLC_SHORT_READ:                       return "MVLC_SHORT_READ";
        case MVLC_WRONG_CMD_HEADER:                 return "MVLC_WRONG_CMD_HEADER";
        case MVLC_WRONG_MIRROR_LENGTH:              return "MVLC_WRONG_MIRROR_LENGTH";
        case MVLC_CMD_MIRROR_TOO_SHORT:              return "MVLC_CMD_MIRROR_TOO_SHORT";
        case MVLC_CMD_MIRROR_MISMATCH:              return "MVLC_CMD_MIRROR_MISMATCH";
#endif
    }

    return nullptr;
}

enum MVLC_STATUS: err_t
{
    MVLC_OK = FT_OK,
    MVLC_SHORT_WRITE = FT_OTHER_ERROR + 1,
    MVLC_SHORT_READ,
    MVLC_WRONG_CMD_HEADER,
    MVLC_WRONG_MIRROR_LENGTH,
    MVLC_CMD_MIRROR_TOO_SHORT,
    MVLC_CMD_MIRROR_MISMATCH,
};

const char *mvlc_error_str(err_t error)
{
    switch (error)
    {
        case MVLC_OK:                               return "MVLC_OK";
        case MVLC_SHORT_WRITE:                      return "MVLC_SHORT_WRITE";
        case MVLC_SHORT_READ:                       return "MVLC_SHORT_READ";
        case MVLC_WRONG_CMD_HEADER:                 return "MVLC_WRONG_CMD_HEADER";
        case MVLC_WRONG_MIRROR_LENGTH:              return "MVLC_WRONG_MIRROR_LENGTH";
        case MVLC_CMD_MIRROR_TOO_SHORT:             return "MVLC_CMD_MIRROR_TOO_SHORT";
        case MVLC_CMD_MIRROR_MISMATCH:              return "MVLC_CMD_MIRROR_MISMATCH";
    }

    return nullptr;
}

} // end anon namespace

namespace mesytec
{
namespace mvlc
{
namespace usb
{

const char *error_str(err_t error)
{
    if (auto ret = ft_error_str(error))
        return ret;

    if (auto ret = mvlc_error_str(error))
        return ret;

    return "UNKNOWN_ERROR";
}

bool is_timeout(err_t error)
{
    return error == FT_TIMEOUT;
}

USB_Impl open_by_index(unsigned index, err_t *errp)
{
    err_t local_error = FT_OK;
    err_t &err(errp ? *errp : local_error);

    USB_Impl result = {};

    err = FT_Create(reinterpret_cast<void *>(index),
                    FT_OPEN_BY_INDEX, &result.handle);

    return result;
}

USB_Impl open_by_serial(const char *serial, err_t *errp)
{
    err_t local_error = FT_OK;
    err_t &err(errp ? *errp : local_error);

    USB_Impl result = {};

    err = FT_Create(const_cast<char *>(serial),
                    FT_OPEN_BY_SERIAL_NUMBER, &result.handle);

    return result;
}

USB_Impl open_by_description(const char *description, err_t *errp)
{
    err_t local_error = FT_OK;
    err_t &err(errp ? *errp : local_error);

    USB_Impl result = {};

    err = FT_Create(const_cast<char *>(description),
                    FT_OPEN_BY_DESCRIPTION, &result.handle);

    return result;
}

err_t close(USB_Impl &mvlc)
{
    err_t result = FT_Close(mvlc.handle);
    mvlc.handle = nullptr;
    return result;
}

err_t write_bytes(USB_Impl *mvlc, u8 writePipe,
                  const u8 *buffer, size_t size,
                  size_t *bytesTransferred)
{
    assert(mvlc);
    assert(size <= USBSingleTransferMaxBytes);

    ULONG transferred = 0;

    err_t result = FT_WritePipeEx(mvlc->handle, writePipe,
                                  const_cast<u8 *>(buffer), size,
                                  &transferred,
                                  mvlc->writeTimeout_ms);

    if (bytesTransferred)
        *bytesTransferred = transferred;

    return result;
}

err_t write_words(USB_Impl *mvlc, u8 pipe,
                  const u32 *buffer, size_t wordCount,
                  size_t *wordsTransferred)
{
    size_t bytesTransferred = 0u;

    err_t result = write_bytes(mvlc, pipe,
                               reinterpret_cast<const u8 *>(buffer),
                               wordCount * sizeof(u32),
                               &bytesTransferred);

    if (wordsTransferred)
        *wordsTransferred = bytesTransferred / sizeof(u32);

    return result;
}

err_t read_bytes(USB_Impl *mvlc, u8 readPipe,
                 u8 *dest, size_t size,
                 size_t *bytesTransferred)
{
    assert(mvlc);
    assert(size <= USBSingleTransferMaxBytes);

    ULONG transferred = 0;

    err_t result = FT_ReadPipeEx(mvlc->handle, readPipe,
                                 dest, size,
                                 &transferred,
                                 mvlc->readTimeout_ms);

    if (bytesTransferred)
        *bytesTransferred = transferred;

    return result;
}

err_t read_words(USB_Impl *mvlc, u8 pipe,
                 u32 *dest, size_t wordCount,
                 size_t *wordsTransferred)
{
    size_t bytesTransferred = 0u;

    err_t result = read_bytes(mvlc, pipe,
                              reinterpret_cast<u8 *>(dest),
                              wordCount * sizeof(u32),
                              &bytesTransferred);

    if (wordsTransferred)
        *wordsTransferred = bytesTransferred / sizeof(u32);

    return result;
}

err_t write_words(USB_Impl *mvlc, u8 pipe, const std::vector<u32> &buffer,
                  size_t *wordsTransferred)
{
    err_t result = write_words(
        mvlc, pipe,
        buffer.data(), buffer.size(),
        wordsTransferred);

    return result;
}

err_t read_words(USB_Impl *mvlc, u8 pipe, std::vector<u32> &dest)
{
    size_t wordsTransferred = 0u;
    err_t result = read_words(
        mvlc, pipe,
        dest.data(), dest.size(),
        &wordsTransferred);

    return result;
}

err_t write_words(USB_Impl *mvlc, u8 pipe, const QVector<u32> &buffer,
                  size_t *wordsTransferred)
{
    err_t result = write_words(
        mvlc, pipe,
        buffer.data(), buffer.size(),
        wordsTransferred);

    return result;
}

err_t read_words(USB_Impl *mvlc, u8 pipe, QVector<u32> &dest)
{
    size_t wordsTransferred = 0u;
    err_t result = read_words(
        mvlc, pipe,
        dest.data(), dest.size(),
        &wordsTransferred);

    return result;
}


//
// MVLCError
//
QString MVLCError::toString() const
{
    switch (type)
    {
        case NoError:
            return "No Error";
        case USBError:
            return error_str(ft_error);
        case IsOpen:
            return "Device is open";
        case IsClosed:
            return "Device is closed";
        //case NotExclusiveOwner:
        //    return "Not the exclusive owner of the underlying device";
        case ShortWrite:
            return "Short write";
        case ShortRead:
            return "Short read";
        case MirrorShortRequest:  // size < 1
            return "mirror check - short request";
        case MirrorShortResponse: // size < 1
            return "mirror check - short response";
        case MirrorResponseTooShort:
            return "mirror check - response too short";
        case MirrorNotEqual:
            return "mirror check - unequal data words";
        case ParseResponseUnexpectedSize:
            return "parsing - unexpected response size";
        case ParseUnexpectedBufferType:
            return "parsing - unexpected response buffer type";
    }

    return "Unknonw Error";
}

//
// Dialog API
//

MVLCError write_buffer(USB_Impl *mvlc, const QVector<u32> &buffer)
{
    size_t wordsTransferred = 0u;
    auto error = write_words(mvlc, CommandPipe, buffer, &wordsTransferred);

    if (error != 0)
        return make_usb_error(error);

    if (wordsTransferred < static_cast<size_t>(buffer.size()))
        return { MVLCError::ShortWrite, 0 };

    return make_success();
};

MVLCError read_response(USB_Impl *mvlc, u8 requiredBufferType, QVector<u32> &dest)
{
    u32 header = 0u;
    size_t wordsTransferred = 0u;

    auto error = read_words(mvlc, CommandPipe, &header, 1, &wordsTransferred);

    if (error != 0) return make_usb_error(error);
    if (wordsTransferred < 1) return { MVLCError::ShortRead };

    if (((header >> 24) & 0xFF) != requiredBufferType)
        return { MVLCError::ParseUnexpectedBufferType };

    u16 responseLength = (header & SuperCmdArgMask);

    dest.resize(1 + responseLength);
    dest[0] = header;

    if (responseLength > 0)
    {
        error = read_words(mvlc, CommandPipe, dest.data() + 1, responseLength,
                           &wordsTransferred);

        if (error != 0) return make_usb_error(error);
        if (wordsTransferred < responseLength) return { MVLCError::ShortRead };
    }

    return make_success();
};

MVLCError check_mirror(const QVector<u32> &request, const QVector<u32> &response)
{
    if (request.size()  < 1)
    {
        qDebug("request size < 1");
        return { MVLCError::MirrorShortRequest };
    }

    if (response.size() < 1)
    {
        qDebug("response_size < 1");
        return { MVLCError::MirrorShortResponse };
    }

    if (response.size() < request.size() - 1)
    {
        qDebug("response too short");
        return { MVLCError::MirrorResponseTooShort };
    }

    int minIndex = 1; // skip F1 header
    int maxIndex = request.size() - 1;

    for (int i = minIndex; i < maxIndex; i++)
    {
        if (request[i] != response[i])
            return { MVLCError::MirrorNotEqual };
    }

    return make_success();
}

MVLCDialog::MVLCDialog(const usb::USB_Impl &impl)
    : m_impl(impl)
{}

MVLCError MVLCDialog::doWrite(const QVector<u32> &buffer)
{
    return write_buffer(&m_impl, buffer);
};

MVLCError MVLCDialog::readResponse(u8 requiredBufferType, QVector<u32> &dest)
{
    return read_response(&m_impl, requiredBufferType, dest);
}

MVLCError MVLCDialog::readRegister(u32 address, u32 &value)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addReadLocal(address);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    log_buffer(request, "read_register >>>");

    MVLCError result = doWrite(request);
    if (!result) return result;
    result = readResponse(SuperResponseHeaderType, m_responseBuffer);
    if (!result) return result;

    log_buffer(m_responseBuffer, "read_register <<<");

    result = check_mirror(request, m_responseBuffer);
    if (!result) return result;

    if (m_responseBuffer.size() < 4)
        return { MVLCError::ParseResponseUnexpectedSize };

    value = m_responseBuffer[3];

    return make_success();
}

MVLCError MVLCDialog::writeRegister(u32 address, u32 value)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addWriteLocal(address, value);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    log_buffer(request, "write_register >>>");

    MVLCError result = doWrite(request);
    if (!result) return result;
    result = readResponse(SuperResponseHeaderType, m_responseBuffer);
    if (!result) return result;

    log_buffer(m_responseBuffer, "write_register <<<");

    result = check_mirror(request, m_responseBuffer);
    if (!result) return result;

    if (m_responseBuffer.size() != 4)
        return { MVLCError::ParseResponseUnexpectedSize };

    return make_success();
}

MVLCError MVLCDialog::vmeSingleRead(u32 address, u32 &value, AddressMode amod,
                                    VMEDataWidth dataWidth)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMERead(address, amod, dataWidth);

    // upload the stack
    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    log_buffer(request, "vme_single_read upload >>>");

    MVLCError result = doWrite(request);
    if (!result) return result;
    result = readResponse(SuperResponseHeaderType, m_responseBuffer);
    if (!result) return result;

    log_buffer(m_responseBuffer, "vme_single_read upload response <<<");

    result = check_mirror(request, m_responseBuffer);
    if (!result) return result;

    // exec the stack
    writeRegister(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift);

    result = readResponse(StackResponseHeaderType, m_responseBuffer);

    log_buffer(m_responseBuffer, "vme_single_read response");

    if (m_responseBuffer.size() != 2)
        return { MVLCError::ParseResponseUnexpectedSize };

    value = m_responseBuffer[1];

    return make_success();
}

MVLCError MVLCDialog::vmeSingleWrite(u32 address, u32 value, AddressMode amod,
                                     VMEDataWidth dataWidth)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMEWrite(address, value, amod, dataWidth);

    // upload the stack
    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    log_buffer(request, "vme_single_write upload >>>");

    MVLCError result = doWrite(request);
    if (!result) return result;
    result = readResponse(SuperResponseHeaderType, m_responseBuffer);
    if (!result) return result;

    log_buffer(m_responseBuffer, "vme_single_write upload response <<<");

    result = check_mirror(request, m_responseBuffer);
    if (!result) return result;

    // exec the stack
    writeRegister(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift);

    result = readResponse(StackResponseHeaderType, m_responseBuffer);

    log_buffer(m_responseBuffer, "vme_single_write response");

    if (m_responseBuffer.size() != 1)
        return { MVLCError::ParseResponseUnexpectedSize };

    return make_success();
}

MVLCError MVLCDialog::vmeBlockRead(u32 address, AddressMode amod, u16 maxTransfers,
                                   QVector<u32> &dest)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMEBlockRead(address, amod, maxTransfers);

    // upload the stack
    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    log_buffer(request, "vme_block_read upload >>>");

    MVLCError result = doWrite(request);
    if (!result) return result;
    result = readResponse(SuperResponseHeaderType, m_responseBuffer);
    if (!result) return result;

    log_buffer(m_responseBuffer, "vme_block_read upload response <<<");

    result = check_mirror(request, m_responseBuffer);
    if (!result) return result;

    // exec the stack
    writeRegister(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift);

    result = readResponse(StackResponseHeaderType, m_responseBuffer);

    log_buffer(m_responseBuffer, "vme_block_read response");

    return make_success();
}

} // end namespace usb
} // end namespace mvlc
} // end namespace mesytec
