#include "mvlc/mvlc_impl_usb.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include "mvlc/mvlc_threading.h"
#include "mvlc/mvlc_error.h"

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

#ifdef __WIN32
template<size_t Size>
struct ReadBuffer
{
    std::array<u8, Size> data;
    u8 *first;
    u8 *last;

    size_t used() const { return last - first; }
    size_t free() const { return data.size() - used(); }
    void reset() { first = last = data.data(); }
    void moveToFront()
    {
        const size_t used_ = used();

        if (used_ > 0 && first != data.data())
        {
            memcpy(data.data(), first, used_);
            first = data.data();
            last  = first + used_;

            assert(used_ == used());
        }
    }
};

struct Impl::PipeReader
{
    static const size_t BufferSize = USBSingleTransferMaxBytes;
    static const size_t BufferCount = 8;

    enum BufferState
    {
        Unused,
        InProgress,
        Complete
    };

    void *handle;
    u8 ep;
    std::atomic<bool> keepRunning;
    std::error_code ec;
    std::condition_variable cv;
    std::mutex lock;

    std::array<ReadBuffer<BufferSize>, BufferCount> buffers;
    std::array<BufferState, BufferCount> states;
    std::array<OVERLAPPED, BufferCount> overlapped;
    std::array<ULONG, BufferCount> bytesTransferred;

    PipeReader(void *handle_, Pipe pipe)
        : handle(handle_)
        , ep(get_endpoint(pipe, EndpointDirection::In))
    {
        keepRunning = true;
    }

    void loop();
};

void Impl::PipeReader::loop()
{
    {
        UniqueLock guard(lock);

        if (auto st = FT_SetStreamPipe(handle, false, false, ep, BufferSize))
        {
            ec = make_error_code(st);
            return;
        }

        for (auto &ol: overlapped)
        {
            if (auto st = FT_InitializeOverlapped(handle, &ol))
            {
                ec = make_error_code(st);
                return;
            }
        }

        // queue initial read requests
        for (size_t i=0; i<BufferCount; i++)
        {
            auto st = FT_ReadPipeEx(handle, ep,
                                    buffers[i].data.begin(), BufferSize,
                                    &bytesTransferred[i],
                                    &overlapped[i]);

            if (st != FT_IO_PENDING)
            {
                ec = make_error_code(st);
                return;
            }

            states[i] = InProgress;
        }
    }

    size_t i = 0;

    fprintf(stderr, "%s, ep=0x%x: queued initial requests, entering loop\n",
            __PRETTY_FUNCTION__, ep);

    while (keepRunning)
    {
        UniqueLock guard(lock);

        fprintf(stderr, "%s, ep=0x%x: waiting on cv\n",
                __PRETTY_FUNCTION__, ep);

        cv.wait(guard, [&] { return states[i] != Complete || !keepRunning; });

        fprintf(stderr, "%s, ep=0x%x: post cv wait\n",
                __PRETTY_FUNCTION__, ep);

        if (!keepRunning)
            break;

        assert(states[i] != Complete);

        if (states[i] == InProgress)
        {
            fprintf(stderr, "%s, ep=0x%x: waiting for request %u to complete\n",
                    __PRETTY_FUNCTION__, ep, i);

            auto st = FT_GetOverlappedResult(handle,
                                             &overlapped[i],
                                             &bytesTransferred[i],
                                             true);

            if (st == FT_IO_INCOMPLETE)
            {
                fprintf(stderr, "%s, ep=0x%x: request %u is incomplete, continue\n",
                        __PRETTY_FUNCTION__, ep, i);
                continue;
            }
            else if (st == FT_TIMEOUT)
            {
                fprintf(stderr, "%s, ep=0x%x: request %u timed out\n",
                        __PRETTY_FUNCTION__, ep, i);
            }
            else if (st != FT_OK)
            {
                ec = make_error_code(st);
                fprintf(stderr, "%s, ep=0x%x: request %u is not ok: %s\n",
                        __PRETTY_FUNCTION__, ep, i, ec.message().c_str());
                break;
            }

            fprintf(stderr, "%s, ep=0x%x: setting request %u to complete and waking consumer\n",
                    __PRETTY_FUNCTION__, ep, i);

            states[i] = Complete;
            cv.notify_one();
        }

        if (states[i] == Unused)
        {
            fprintf(stderr, "%s, ep=0x%x: queueing new request %u\n",
                    __PRETTY_FUNCTION__, ep, i);

            auto st = FT_ReadPipeEx(handle, ep,
                                    buffers[i].data.begin(), BufferSize,
                                    &bytesTransferred[i],
                                    &overlapped[i]);

            if (st != FT_IO_PENDING)
            {
                ec = make_error_code(st);
                break;
            }
        }

        if (++i == BufferCount)
            i = 0;
    }

    fprintf(stderr, "%s, ep=0x%x: left loop\n",
            __PRETTY_FUNCTION__, ep, i);

    // cleanup
    UniqueLock guard(lock);
    for (auto &ol: overlapped)
    {
        if (auto st = FT_ReleaseOverlapped(handle, &ol))
        {
            ec = make_error_code(st);
            break;
        }
    }
}
#endif // __WIN32

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

    // Apply the read and write timeouts.
    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
        if (auto ec = set_endpoint_timeout(m_handle, get_endpoint(pipe, EndpointDirection::Out),
                                           getWriteTimeout(pipe)))
            return ec;

        if (auto ec = set_endpoint_timeout(m_handle, get_endpoint(pipe, EndpointDirection::In),
                                           getReadTimeout(pipe)))
            return ec;
    }

    fprintf(stderr, "%s: connected!\n", __PRETTY_FUNCTION__);
#ifdef __WIN32

    fprintf(stderr, "%s: starting PipeReaders...\n", __PRETTY_FUNCTION__);

    for (size_t i=0; i<PipeCount; i++)
    {
        m_readers[i] = std::make_unique<PipeReader>(m_handle, static_cast<Pipe>(i));
        m_readerThreads[i] = std::thread(&PipeReader::loop, m_readers[i].get());
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

#if __WIN32
    fprintf(stderr, "%s: stopping PipeReaders!\n", __PRETTY_FUNCTION__);

    for (size_t i=0; i<PipeCount; i++)
    {
        m_readers[i]->keepRunning = false;
        m_readers[i]->cv.notify_one();
    }

    fprintf(stderr, "%s: joining PipeReaders!\n", __PRETTY_FUNCTION__);

    for (size_t i=0; i<PipeCount; i++)
    {
        if (m_readerThreads[i].joinable())
            m_readerThreads[i].join();
    }
#endif

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

#ifdef __WIN32 // windows
    FT_STATUS st = FT_WritePipeEx(m_handle, get_endpoint(pipe, EndpointDirection::Out),
                                  const_cast<u8 *>(buffer), size,
                                  &transferred,
                                  nullptr);
#else // linux
    FT_STATUS st = FT_WritePipeEx(m_handle, get_fifo_id(pipe),
                                  const_cast<u8 *>(buffer), size,
                                  &transferred,
                                  m_writeTimeouts[static_cast<unsigned>(pipe)]);
#endif

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    if (ec)
    {
        fprintf(stderr, "%s: pipe=%u, wrote %lu of %lu bytes, result=%s\n",
                __PRETTY_FUNCTION__, 
                static_cast<unsigned>(pipe),
                bytesTransferred, size, 
                ec.message().c_str());
    }

    return ec;
}

#ifdef __WIN32 // Impl::read() windows
std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

#if 0
    //fprintf(stderr, "%s: begin read: pipe=%u, size=%lu bytes\n",
    //        __PRETTY_FUNCTION__, 
    //        static_cast<unsigned>(pipe),
    //        size);

    bytesTransferred = 0u;

    if (size == 0)
    {
        fprintf(stderr, "%s: zero size read from pipe %u, nothing to do",
                __PRETTY_FUNCTION__,
                static_cast<unsigned>(pipe));

        return {};
    }
    else if (pipe == Pipe::Command && m_cmdBuffer.used() >= size)
    // Have enough data to complete the request.
    {
        memcpy(buffer, m_cmdBuffer.begin, size);
        m_cmdBuffer.begin += size;

        if (m_cmdBuffer.used() == 0)
            m_cmdBuffer.reset();

        bytesTransferred = size;

        fprintf(stderr, "%s: cmd read completed using buffered data, new buffer size=%lu\n",
                __PRETTY_FUNCTION__, 
                m_cmdBuffer.used());

        return {};
    }
    else if (pipe == Pipe::Command)
    // Need to fetch more data
    {
        memcpy(buffer, m_cmdBuffer.begin, m_cmdBuffer.used());
        bytesTransferred = m_cmdBuffer.used();
        size -= m_cmdBuffer.used();
        m_cmdBuffer.reset();

        ULONG transferred = 0; // FT API wants a ULONG* parameter

        FT_STATUS st = FT_ReadPipeEx(
            m_handle, get_endpoint(pipe, EndpointDirection::In),
            m_cmdBuffer.data.data(), m_cmdBuffer.data.size(),
            &transferred,
            nullptr);

        fprintf(stderr, "%s: pipe=%u, transferred %lu bytes from device, result=%s\n",
                __PRETTY_FUNCTION__,
                static_cast<unsigned>(pipe),
                transferred,
                make_error_code(st).message().c_str());

        m_cmdBuffer.end = m_cmdBuffer.begin + transferred;

        if (auto ec = make_error_code(st))
            return ec;

        if (m_cmdBuffer.used() >= size)
        {
            memcpy(buffer + bytesTransferred, m_cmdBuffer.begin, size);
            m_cmdBuffer.begin += size;
            bytesTransferred += size;

            if (m_cmdBuffer.used() == 0)
                m_cmdBuffer.reset();
        }
        else
        {
            // FIXME: This part can be reached. Figure out when and why and fix this.
            assert(false);
        }

        return {};
    }
    else
    {
        ULONG transferred = 0; // FT API wants a ULONG* parameter

        FT_STATUS st = FT_ReadPipeEx(m_handle,  get_endpoint(pipe, EndpointDirection::In),
                                     buffer, size,
                                     &transferred,
                                     nullptr);

        bytesTransferred = transferred;

        auto ec = make_error_code(st);
        if (ec && ec != ErrorType::Timeout)
        {
            fprintf(stderr, "%s: pipe=%u, read %lu of %lu bytes, result=%s\n",
                    __PRETTY_FUNCTION__, 
                    static_cast<unsigned>(pipe),
                    bytesTransferred, size, 
                    ec.message().c_str());
        }
        return ec;
    }

    return {};
#elif 1
    const u8 ep = get_endpoint(pipe, EndpointDirection::In);
    bytesTransferred = 0u;
    OVERLAPPED overlapped;

    if (auto st = FT_InitializeOverlapped(m_handle, &overlapped))
        return make_error_code(st);

    ULONG transferred = 0; // FT API wants a ULONG* parameter

    auto st = FT_ReadPipeEx(m_handle, ep, buffer, size, &transferred, &overlapped);

    if (st == FT_IO_PENDING)
    {
        do
        {
            st = FT_GetOverlappedResult(m_handle, &overlapped, &transferred, false);

            if (st == FT_OK || st != FT_IO_INCOMPLETE)
                break;
        }
        while(true);
    }

    bytesTransferred = transferred;
    FT_ReleaseOverlapped(m_handle, &overlapped);
    return make_error_code(st);
#else
#endif
}
#else // Impl::read() linux
std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    fprintf(stderr, "%s: begin read: pipe=%u, size=%lu bytes\n",
            __PRETTY_FUNCTION__, 
            static_cast<unsigned>(pipe),
            size);

    ULONG transferred = 0; // FT API wants a ULONG* parameter

    FT_STATUS st = FT_ReadPipeEx(m_handle, get_fifo_id(pipe),
                                 buffer, size,
                                 &transferred,
                                 m_readTimeouts[static_cast<unsigned>(pipe)]);

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    if (true||ec)
    {
        fprintf(stderr, "%s: pipe=%u, read %lu of %lu bytes, result=%s\n",
                __PRETTY_FUNCTION__, 
                static_cast<unsigned>(pipe),
                bytesTransferred, size, 
                ec.message().c_str());
    }

    return ec;
}
#endif // Impl::read

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
