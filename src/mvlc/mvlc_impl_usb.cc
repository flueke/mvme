#include "mvlc/mvlc_impl_usb.h"
#include "mvlc/mvlc_error.h"
#include <cassert>
#include <cstdio>

namespace
{

class FTErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "ftd3xx";
    }

    std::string message(int ev) const override
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

    std::error_condition default_error_condition(int ev) const noexcept override
    {
        using mesytec::mvlc::ErrorType;

        switch (static_cast<_FT_STATUS>(ev))
        {
            case FT_OK:
                return ErrorType::Success;

            case FT_INVALID_HANDLE:
            case FT_DEVICE_NOT_FOUND:
            case FT_DEVICE_NOT_OPENED:
                return ErrorType::ConnectionError;

            case FT_IO_ERROR:
            case FT_INSUFFICIENT_RESOURCES:
            case FT_INVALID_PARAMETER: /* 6 */
            case FT_INVALID_BAUD_RATE:
            case FT_DEVICE_NOT_OPENED_FOR_ERASE:
            case FT_DEVICE_NOT_OPENED_FOR_WRITE:
            case FT_FAILED_TO_WRITE_DEVICE: /* 10 */
            case FT_EEPROM_READ_FAILED:
            case FT_EEPROM_WRITE_FAILED:
            case FT_EEPROM_ERASE_FAILED:
            case FT_EEPROM_NOT_PRESENT:
            case FT_EEPROM_NOT_PROGRAMMED:
            case FT_INVALID_ARGS:
            case FT_NOT_SUPPORTED:
            case FT_NO_MORE_ITEMS:
                return ErrorType::IOError;

            case FT_TIMEOUT: /* 19 */
                return ErrorType::Timeout;

            case FT_OPERATION_ABORTED:
            case FT_RESERVED_PIPE:
            case FT_INVALID_CONTROL_REQUEST_DIRECTION:
            case FT_INVALID_CONTROL_REQUEST_TYPE:
            case FT_IO_PENDING:
            case FT_IO_INCOMPLETE:
            case FT_HANDLE_EOF:
            case FT_BUSY:
            case FT_NO_SYSTEM_RESOURCES:
            case FT_DEVICE_LIST_NOT_READY:
            case FT_DEVICE_NOT_CONNECTED:
            case FT_INCORRECT_DEVICE_PATH:
            case FT_OTHER_ERROR:
                return ErrorType::IOError;
        }

        assert(false);
        return {};
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
    In,
    Out
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

    if (dir == EndpointDirection::In)
        result |= 0x80;

    return result;
}

std::error_code set_endpoint_timeout(void *handle, u8 ep, unsigned ms)
{
    FT_STATUS st = FT_SetPipeTimeout(handle, ep, ms);
    return mesytec::mvlc::usb::make_error_code(st);
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
//    : m_connectMode{ConnectMode::ByIndex}
//{
//}

Impl::Impl(int index)
    : m_connectMode{ConnectMode::ByIndex, index}
{
}

//Impl::Impl(const std::string &serial)
//    : m_connectMode{ConnectMode::BySerial, 0, serial}
//{
//}

Impl::~Impl()
{
    disconnect();
}

std::error_code Impl::connect()
{
    if (isConnected())
        return make_error_code(MVLCErrorCode::IsConnected);

    FT_STATUS st = FT_OK;

    switch (m_connectMode.mode)
    {
        case ConnectMode::First:
            assert(!"not implemented");
            break;

        case ConnectMode::ByIndex:
            st = FT_Create(reinterpret_cast<void *>(m_connectMode.index),
                           FT_OPEN_BY_INDEX, &m_handle);
            break;

        case ConnectMode::BySerial:
            assert(!"not implemented");
            break;
    }

    if (auto ec = make_error_code(st))
        return ec;

    // Apply the read and write timeouts. Errors are ignored for now.
#if 0
    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
        set_endpoint_timeout(m_handle, get_endpoint(pipe, EndpointDirection::Out),
                             getWriteTimeout(pipe));

        set_endpoint_timeout(m_handle, get_endpoint(pipe, EndpointDirection::In),
                             getReadTimeout(pipe));
    }
#endif

#ifdef __WIN32
#if 0
    // Using the streampipe mode under windows results in a minor read
    // performance improvement but makes the read call block until it receives
    // the specified amount of data. This means the readout loop can't be left
    // in case no data arrives. Figure out how to abort the pending read.

    // FT_SetStreamPipe(handle, allWritePipes, allReadPipes, pipeID, streamSize)

    unsigned char pipeArg = get_endpoint(Pipe::Data, EndpointDirection::In);
    st = FT_SetStreamPipe(m_handle, false, false, 
		    pipeArg,
		    1024 * 1024);
    if (auto ec = make_error_code(st))
    {
        fprintf(stderr, "%s: FT_SetStreamPipe failed: %u: %s, pipeArg=%u",
                __PRETTY_FUNCTION__, static_cast<unsigned>(st),
                ec.message().c_str(), static_cast<unsigned>(pipeArg));
        return ec;
    }
#endif
#endif // __WIN32

    fprintf(stderr, "%s: connected!\n", __PRETTY_FUNCTION__);

    return {};
}

std::error_code Impl::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    FT_STATUS st = FT_Close(m_handle);
    m_handle = nullptr;
    return make_error_code(st);
}

bool Impl::isConnected() const
{
    return m_handle != nullptr;
}

void Impl::setWriteTimeout(Pipe pipe, unsigned ms)
{
    auto up = static_cast<unsigned>(pipe);
    if (up >= PipeCount) return;
    m_writeTimeouts[up] = ms;
    if (isConnected())
        set_endpoint_timeout(m_handle, get_endpoint(pipe, EndpointDirection::Out), ms);
}

void Impl::setReadTimeout(Pipe pipe, unsigned ms)
{
    auto up = static_cast<unsigned>(pipe);
    if (up >= PipeCount) return;
    m_readTimeouts[up] = ms;
    if (isConnected())
        set_endpoint_timeout(m_handle, get_endpoint(pipe, EndpointDirection::In), ms);
}

unsigned Impl::getWriteTimeout(Pipe pipe) const
{
    auto up = static_cast<unsigned>(pipe);
    if (up >= PipeCount) return 0u;
    return m_writeTimeouts[up];
}

unsigned Impl::getReadTimeout(Pipe pipe) const
{
    auto up = static_cast<unsigned>(pipe);
    if (up >= PipeCount) return 0u;
    return m_readTimeouts[up];
}

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    u8 fifo = get_fifo_id(pipe);
    ULONG transferred = 0; // FT API needs a ULONG*

#ifndef __WIN32
    FT_STATUS st = FT_WritePipeEx(m_handle, get_fifo_id(pipe),
                                  const_cast<u8 *>(buffer), size,
                                  &transferred,
                                  m_writeTimeouts[static_cast<unsigned>(pipe)]);
#else // windows
#if 0
    FT_STATUS st = FT_WritePipeEx(m_handle, get_endpoint(pipe, EndpointDirection::Out),
                                  const_cast<u8 *>(buffer), size,
                                  &transferred,
                                  nullptr);
#else
    FT_STATUS st = FT_WritePipe(m_handle, get_endpoint(pipe, EndpointDirection::Out),
                                const_cast<u8 *>(buffer), size,
                                &transferred,
                                nullptr);
#endif
#endif

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    fprintf(stderr, "%s: pipe=%u, wrote %lu of %lu bytes, result=%s\n",
		    __PRETTY_FUNCTION__, 
		    static_cast<unsigned>(pipe),
		    bytesTransferred, size, 
		    ec.message().c_str());

    return ec;
}

std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    ULONG transferred = 0; // FT API needs a ULONG*

#ifndef __WIN32
    FT_STATUS st = FT_ReadPipeEx(m_handle, get_fifo_id(pipe),
                                 buffer, size,
                                 &transferred,
                                 m_readTimeouts[static_cast<unsigned>(pipe)]);
#else // windows
#if 0
    FT_STATUS st = FT_ReadPipeEx(m_handle,  get_endpoint(pipe, EndpointDirection::In),
                                 buffer, size,
                                 &transferred,
                                 nullptr);
#else
    FT_STATUS st = FT_ReadPipe(m_handle, get_endpoint(pipe, EndpointDirection::In),
                               buffer, size,
                               &transferred,
                               nullptr);
#endif
#endif

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    /*
    fprintf(stderr, "%s: pipe=%u, read %lu of %lu bytes, result=%s\n",
		    __PRETTY_FUNCTION__, 
		    static_cast<unsigned>(pipe),
		    bytesTransferred, size, 
		    ec.message().c_str());
    */

    return ec;
}

std::error_code Impl::get_read_queue_size(Pipe pipe, u32 &dest)
{
#ifndef __WIN32
    FT_STATUS st = FT_GetReadQueueStatus(m_handle, get_fifo_id(pipe), &dest);
#else
    dest = 0u;
    FT_STATUS st = FT_NOT_SUPPORTED;
#endif
    return make_error_code(st);
}

} // end namespace usb
} // end namespace mvlc
} // end namespace mesytec
