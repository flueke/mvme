#include "mvlc/mvlc_usb_impl.h"
#include <cassert>

namespace
{

class FTErrorCategory: public std::error_category
{
    const char *name() const noexcept
    {
        return "ftd3xx";
    }

    std::string message(int ev) const
    {
        switch (static_cast<_FT_STATUS>(ev))
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
        }

        return "unknown FT error";
    }
};

const FTErrorCategory theFTErrorCategory {};

constexpr u8 get_fifo_id(mesytec::mvlc::Pipe pipe)
{
    switch (pipe)
    {
        case mesytec::mvlc::Pipe::Command:
            return 0;
        case mesytec::mvlc::Pipe::Data:
            return 1;
    }
    return 0;
}

enum class EndpointDirection: u8
{
    IN,
    OUT
};

constexpr u8 get_endpoint(mesytec::mvlc::Pipe pipe, EndpointDirection dir)
{
    u8 result = 0;

    switch (pipe)
    {
        case mesytec::mvlc::Pipe::Command:
            result = 0x2;
            break;

        case mesytec::mvlc::Pipe::Data:
            result = 0x3;
            break;
    }

    if (dir == EndpointDirection::IN)
        result |= 0x80;

    return result;
}


} // end anon namespace

namespace mesytec
{
namespace mvlc
{
namespace usb
{

std::error_code make_error_code(FT_STATUS st)
{
    return { static_cast<int>(st), theFTErrorCategory };
}

//Impl::Impl()
//    : m_openInfo{OpenInfo::ByIndex}
//{
//}

Impl::Impl(int index)
    : m_openInfo{OpenInfo::ByIndex, index}
{
}

//Impl::Impl(const std::string &serial)
//    : m_openInfo{OpenInfo::BySerial, 0, serial}
//{
//}

Impl::~Impl()
{
    if (is_open())
        close();
}

std::error_code Impl::open()
{
    if (is_open()) return {};

    FT_STATUS st = FT_OK;

    switch (m_openInfo.mode)
    {
        case OpenInfo::First:
            assert(!"not implemented");
            break;

        case OpenInfo::ByIndex:
            st = FT_Create(reinterpret_cast<void *>(m_openInfo.index),
                           FT_OPEN_BY_INDEX, &m_handle);
            break;

        case OpenInfo::BySerial:
            assert(!"not implemented");
            break;
    }

    return make_error_code(st);
}

std::error_code Impl::close()
{
    if (!is_open()) return make_error_code(FT_DEVICE_NOT_OPENED);

    FT_STATUS st = FT_Close(m_handle);
    m_handle = nullptr;
    return make_error_code(st);
}

bool Impl::is_open() const
{
    return m_handle != nullptr;
}

void Impl::set_write_timeout(Pipe pipe, unsigned ms)
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return;
    m_writeTimeouts[static_cast<unsigned>(pipe)] = ms;
}

void Impl::set_read_timeout(Pipe pipe, unsigned ms)
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return;
    m_readTimeouts[static_cast<unsigned>(pipe)] = ms;
}

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    // TODO: do this under windows
    //    FT_SetPipeTimeout(m_handle, get_endpoint(pipe, EndpointDirection::OUT),
    //                      ms);

    u8 fifo = get_fifo_id(pipe);
    ULONG transferred = 0; // FT API needs a ULONG*

    FT_STATUS st = FT_WritePipeEx(m_handle, get_fifo_id(pipe),
                                  const_cast<u8 *>(buffer), size,
                                  &transferred,
                                  m_writeTimeouts[static_cast<unsigned>(pipe)]);

    bytesTransferred = transferred;

    return make_error_code(st);
}

std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    ULONG transferred = 0; // FT API needs a ULONG*

    FT_STATUS st = FT_ReadPipeEx(m_handle, get_fifo_id(pipe),
                                  buffer, size,
                                  &transferred,
                                  m_readTimeouts[static_cast<unsigned>(pipe)]);

    bytesTransferred = transferred;

    return make_error_code(st);
}

} // end namespace usb
} // end namespace mvlc
} // end namespace mesytec
