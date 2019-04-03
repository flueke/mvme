#include "mvlc/mvlc_impl_usb.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <numeric>
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
template<size_t Capacity>
struct ReadBuffer
{
    std::array<u8, Capacity> data;
    u8 *first = nullptr;
    u8 *last = nullptr;

    size_t size() const { return last - first; }
    size_t free() const { return Capacity - size(); }
    size_t capacity() const { return Capacity; }
    void clear() { first = last = data.data(); }
};

struct Impl::EndpointReader
{
    static const size_t BufferSize = USBSingleTransferMaxBytes;
    static const size_t BufferCount  = 8;

    enum SlotState
    {
        Unused,
        InProgress,
        Complete
    };

    struct Slot
    {
        ReadBuffer<BufferSize> buffer;
        SlotState state;
        OVERLAPPED overlapped;
        ULONG bytesTransferred;
    };

    void *handle;
    u8 ep;
    std::error_code ec;
    std::array<Slot, BufferCount> slots;
    int firstCompleteSlotIndex;
    int nextPendingSlotIndex;

    EndpointReader(void *handle_, u8 ep);
    ~EndpointReader();
    size_t bytesAvailable() const;
};

Impl::EndpointReader::EndpointReader(void *handle_, u8 ep_)
    : handle(handle_)
    , ep(ep_)
{
    // initialize the OVERLAPPED structures
    for (size_t i=0; i<BufferCount; i++)
    {
        if (auto st = FT_InitializeOverlapped(handle, &slots[i].overlapped))
        {
            ec = make_error_code(st);
            return;
        }
    }

    // queue up the initial set of read requests
    for (size_t i=0; i<BufferCount; i++)
    {
        auto st = FT_ReadPipeEx(handle, ep,
                                slots[i].buffer.data.data(),
                                BufferSize,
                                &slots[i].bytesTransferred,
                                &slots[i].overlapped);

        if (st != FT_IO_PENDING)
        {
            ec = make_error_code(st);
            return;
        }

        slots[i].state = InProgress;
    }

    firstCompleteSlotIndex = -1;
    nextPendingSlotIndex = 0u;
}

Impl::EndpointReader::~EndpointReader()
{
    for (size_t i=0; i<BufferCount; i++)
    {
        FT_AbortPipe(handle, ep);
        FT_ReleaseOverlapped(handle, &slots[i].overlapped);
    }
}

size_t Impl::EndpointReader::bytesAvailable() const
{
    size_t result = std::accumulate(
        slots.begin(), slots.end(), static_cast<size_t>(0u),
        [] (const size_t &a, const Slot &slt) {
            return a + (slt.state == Complete ? slt.buffer.size() : 0u);
    });

    return result;
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

std::error_code Impl::closeHandle()
{
    FT_STATUS st = FT_OK;

    if (m_handle)
    {
        st = FT_Close(m_handle);
        m_handle = nullptr;
    }

    return make_error_code(st);
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
        {
            closeHandle();
            return ec;
        }

        if (auto ec = set_endpoint_timeout(m_handle, get_endpoint(pipe, EndpointDirection::In),
                                           getReadTimeout(pipe)))
        {
            closeHandle();
            return ec;
        }
    }

    fprintf(stderr, "%s: connected!\n", __PRETTY_FUNCTION__);

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
            USBSingleTransferMaxBytes);

    if (auto ec = make_error_code(st))
    {
        fprintf(stderr, "%s: FT_SetStreamPipe failed: %u: %s, pipeArg=%u",
                __PRETTY_FUNCTION__, static_cast<unsigned>(st),
                ec.message().c_str(), static_cast<unsigned>(pipeArg));
        return ec;
    }
#endif
#endif // __WIN32

#ifdef __WIN32
#if 1
    fprintf(stderr, "%s: creating EndpointReaders...\n", __PRETTY_FUNCTION__);

    for (size_t i=0; i<PipeCount; i++)
    {
        m_readers[i] = std::make_unique<EndpointReader>(
            m_handle, get_endpoint(static_cast<Pipe>(i), EndpointDirection::In));

        if (m_readers[i]->ec)
        {
            fprintf(stderr, "%s: error from EndpointReader %u: %s\n",
                    __PRETTY_FUNCTION__, i, m_readers[i]->ec.message().c_str());
            closeHandle();
            break;
        }
    }
#endif
#endif

    fprintf(stderr, "%s: connected!\n", __PRETTY_FUNCTION__);

    return {};
}

std::error_code Impl::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    return closeHandle();

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

    bytesTransferred = 0u;

    if (size == 0u)
    {
        return {};
    }

#if 0
    ULONG transferred = 0; // FT API wants a ULONG* parameter

    FT_STATUS st = FT_ReadPipe(m_handle, get_endpoint(pipe, EndpointDirection::In),
                               buffer, size,
                               &transferred,
                               nullptr);

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    if (ec)
    {
        fprintf(stderr, "%s: pipe=%u, read %lu of %lu bytes, result=%s\n",
                __PRETTY_FUNCTION__, 
                static_cast<unsigned>(pipe),
                bytesTransferred, size, 
                ec.message().c_str());
    }

    return ec;

#else
    // Client requests 'size' bytes to be stored in 'buffer'
    // Cases:
    // - we have >= size bytes available across all slots:
    //   copy from slots into the dest buffer
    //   update slots to account for the consumed data.
    //   which is the first slot to copy from? -> firstCompleteSlotIndex, the
    //   oldest slot for which a read request completed

    // - we do not have <= size bytes available:
    //   while bytesAvailable < size:
    //     FT_GetOverlappedResult() on the next slot
    //   if (no more slot left to read from and still not enough data):
    //      return an error;
    //   if (timeout)
    //      copy available data to user buffer
    //      return timeout

    FT_STATUS st = FT_OK;
    auto &reader = m_readers[static_cast<unsigned>(pipe)];

    fprintf(stderr, "%s: pipe=%u, size=%u, bytesAvailable=%u, nextPendingSlotIndex=%d, slots:\n",
            __PRETTY_FUNCTION__, static_cast<unsigned>(pipe),
            size, reader->bytesAvailable(), reader->nextPendingSlotIndex);

    for (size_t i=0; i<reader->BufferCount; i++)
    {
        fprintf(stderr, "  slot=%u, state=%d\n", i, reader->slots[i].state);
    }

    while (reader->bytesAvailable() < size)
    {
        const int si = reader->nextPendingSlotIndex;
        auto &slt = reader->slots[si];

        if (slt.state == EndpointReader::Unused)
        {
            st = FT_ReadPipeEx(
                m_handle, reader->ep,
                slt.buffer.data.data(),
                EndpointReader::BufferSize,
                &slt.bytesTransferred,
                &slt.overlapped);

            if (st != FT_IO_PENDING)
                return make_error_code(st);

            slt.state = EndpointReader::InProgress;
        }
        else if (slt.state == EndpointReader::Complete)
        {
            // Not enough data and no more unused slots left to queue up more
            // requests.
            //return make_error_code(FT_NO_MORE_ITEMS);
            st = FT_TIMEOUT;
            break;
        }

        st = FT_GetOverlappedResult(m_handle, &slt.overlapped, &slt.bytesTransferred, true);

        fprintf(stderr, "%s: pipe=%u, size=%u, si=%d, FT_GetOverlappedResult: %s\n",
                __PRETTY_FUNCTION__, static_cast<unsigned>(pipe), size, si,
                make_error_code(st).message().c_str());

        if (++reader->nextPendingSlotIndex == EndpointReader::BufferCount)
            reader->nextPendingSlotIndex = 0u;

        if (st == FT_OK || st == FT_TIMEOUT)
        {
            slt.buffer.first = slt.buffer.data.data();
            slt.buffer.last  = slt.buffer.first + slt.bytesTransferred;
            slt.state = EndpointReader::Complete;

            fprintf(stderr, "%s: pipe=%u, size=%u, si=%d, transfered %u bytes\n",
                    __PRETTY_FUNCTION__, static_cast<unsigned>(pipe), size, si,
                    slt.bytesTransferred);

            if (reader->firstCompleteSlotIndex < 0)
                reader->firstCompleteSlotIndex = si;

            if (st == FT_TIMEOUT)
                break;
        }
        else
        {
            slt.buffer.clear();
            slt.state = EndpointReader::Unused;
            return make_error_code(st);
        }
    }

    assert(reader->bytesAvailable() >= size || st == FT_TIMEOUT);

    // starting from firstCompleteSlotIndex copy data into buffer 
    while (size > 0)
    {
        auto &slt = reader->slots[reader->firstCompleteSlotIndex];

        if (slt.state != EndpointReader::Complete)
            break;

        const size_t toCopy = std::min(size, slt.buffer.size());
        memcpy(buffer, slt.buffer.first, toCopy);
        slt.buffer.first += toCopy;
        bytesTransferred += toCopy;
        buffer += toCopy;
        size -= toCopy;

        if (slt.buffer.size() == 0)
        {
            slt.buffer.clear();
            slt.state = EndpointReader::Unused;

            if (++reader->firstCompleteSlotIndex == EndpointReader::BufferCount)
                reader->firstCompleteSlotIndex = 0u;

            st = FT_ReadPipe(m_handle, reader->ep,
                               slt.buffer.data.data(),
                               EndpointReader::BufferSize,
                               &slt.bytesTransferred,
                               &slt.overlapped);

            if (st != FT_IO_PENDING)
                return make_error_code(st);

            slt.state = EndpointReader::InProgress;
        }
    }

    if (st == FT_IO_PENDING)
        return make_error_code(FT_TIMEOUT);
    return make_error_code(st);
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

    if (ec)
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
