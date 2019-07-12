#include "mvlc/mvlc_impl_usb.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <iomanip>
#include <numeric>
#include <regex>
#include <QDebug>

#include "mvlc/mvlc_threading.h"
#include "mvlc/mvlc_dialog.h"
#include "mvlc/mvlc_error.h"


#define LOG_LEVEL_OFF     0
#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MVLC_USB_LOG_LEVEL
#define MVLC_USB_LOG_LEVEL LOG_LEVEL_OFF
#endif

#define LOG_LEVEL_SETTING MVLC_USB_LOG_LEVEL

#define DO_LOG(level, prefix, fmt, ...)\
do\
{\
    if (LOG_LEVEL_SETTING >= level)\
    {\
        fprintf(stderr, prefix "%s(): " fmt "\n", __FUNCTION__, ##__VA_ARGS__);\
    }\
} while (0);

#define LOG_WARN(fmt, ...)  DO_LOG(LOG_LEVEL_WARN,  "WARN - mvlc_usb ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DO_LOG(LOG_LEVEL_INFO,  "INFO - mvlc_usb ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - mvlc_usb ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DO_LOG(LOG_LEVEL_TRACE, "TRACE - mvlc_usb ", fmt, ##__VA_ARGS__)

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
            case FT_DEVICE_NOT_CONNECTED:
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

// Returns an unfiltered list of all connected FT60X devices. */
mesytec::mvlc::usb::DeviceInfoList make_device_info_list()
{
    using mesytec::mvlc::usb::DeviceInfoList;
    using mesytec::mvlc::usb::DeviceInfo;

    DeviceInfoList result;

    DWORD numDevs = 0;
    FT_STATUS st = FT_CreateDeviceInfoList(&numDevs);

    if (st == FT_OK && numDevs > 0)
    {
        auto ftInfoNodes = std::make_unique<FT_DEVICE_LIST_INFO_NODE[]>(numDevs);
        st = FT_GetDeviceInfoList(ftInfoNodes.get(), &numDevs);

        if (st == FT_OK)
        {
            result.reserve(numDevs);

            for (DWORD ftIndex = 0; ftIndex < numDevs; ftIndex++)
            {
                const auto &infoNode = ftInfoNodes[ftIndex];
                DeviceInfo di = {};
                di.index = ftIndex;
                di.serial = infoNode.SerialNumber;
                di.description = infoNode.Description;

                if (infoNode.Flags & FT_FLAGS_OPENED)
                    di.flags |= DeviceInfo::Flags::Opened;

                if (infoNode.Flags & FT_FLAGS_HISPEED)
                    di.flags |= DeviceInfo::Flags::USB2;

                if (infoNode.Flags & FT_FLAGS_SUPERSPEED)
                    di.flags |= DeviceInfo::Flags::USB3;

                di.handle = infoNode.ftHandle;

                result.emplace_back(di);
            }
        }
    }

    return result;
}

using namespace mesytec::mvlc;

std::error_code disable_all_triggers(MVLCDialog &dlg)
{
    for (u8 stackId = 0; stackId < stacks::StackCount; stackId++)
    {
        u16 addr = stacks::get_trigger_register(stackId);

        if (auto ec = dlg.writeRegister(addr, stacks::NoTrigger))
            return ec;
    }

    return {};
}

// USB specific post connect routine which tries to disable a potentially
// running DAQ. This is done to make sure the command communication is working
// properly and no readout data is clogging the USB.
// Steps:
// - Disable all triggers by writing 0 to the corresponding registers.
//   Errors are ignored except ErrorType::ConnectionError which indicate that
//   we could not open the USB device.
// - Read from the command pipe until no more data arrives. Again only
//   ConnectionError type errors are considered fatal.
// - Read from the data pipe until no more data arrives. These can be delayed
//   responses from writing to the trigger registers or queued up stack error
//   notifications.
// - Do a register read to check that communication is ok now.
std::error_code post_connect_cleanup(mesytec::mvlc::usb::Impl &impl)
{
    qDebug() << __PRETTY_FUNCTION__ << "begin";

    static const int RetryCount = 3;
    static const int DataBufferSize = 10 * 1024;

    mesytec::mvlc::MVLCDialog dlg(&impl);
    std::error_code ec;
    size_t totalBytesTransferred = 0u;

    // Disable the triggers. There may be timeouts due to the data pipe being
    // full and no command responses arriving on the command pipe. Also
    // notification data can be stuck in the command pipe so that the responses
    // are not parsed correctly.

    for (int try_ = 0; try_ < RetryCount; try_++)
    {
        ec = disable_all_triggers(dlg);

        if (ec == ErrorType::ConnectionError)
            return ec;

        size_t bytesTransferred = 0u;
        std::array<u8, DataBufferSize> buffer;

        do
        {
            ec = impl.read(Pipe::Data, buffer.data(), buffer.size(), bytesTransferred);
            totalBytesTransferred += bytesTransferred;

            if (ec == ErrorType::ConnectionError)
                return ec;

        } while (bytesTransferred > 0);

        u32 regVal = 0u;
        ec = dlg.readRegister(stacks::get_trigger_register(0), regVal);

        if (!ec)
            break; // all good, no need to retry
    }

    qDebug() << __PRETTY_FUNCTION__ << "end, totalBytesTransferred ="
        << totalBytesTransferred << ", ec =" << ec.message().c_str();

    return ec;
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

DeviceInfoList get_device_info_list(const ListOptions opts)
{
    auto result = make_device_info_list();

    if (opts == ListOptions::MVLCDevices)
    {
        // Remove if the description does not contain "MVLC"
        auto it = std::remove_if(result.begin(), result.end(), [] (const DeviceInfo &di) {
            static const std::regex reDescr("MVLC");
            return !(std::regex_search(di.description, reDescr));
        });

        result.erase(it, result.end());
    }

    return result;
}

DeviceInfo get_device_info_by_serial(const std::string &serial)
{
    auto infoList = get_device_info_list();

    auto it = std::find_if(infoList.begin(), infoList.end(),
                           [&serial] (const DeviceInfo &di) {
        return di.serial == serial;
    });

    return it != infoList.end() ? *it : DeviceInfo{};
}

std::string format_serial(unsigned serial)
{
    static const unsigned SerialSize = 12;

    std::stringstream ss;
    ss << std::setw(SerialSize) << std::setfill('0') << serial;
    return ss.str();
}

DeviceInfo get_device_info_by_serial(unsigned serial)
{
    return get_device_info_by_serial(format_serial(serial));
}

//
// Impl
//
Impl::Impl()
    : m_connectMode{ConnectMode::First}
{
}

Impl::Impl(unsigned index)
    : m_connectMode{ConnectMode::ByIndex, index}
{
}

Impl::Impl(const std::string &serial)
    : m_connectMode{ConnectMode::BySerial, 0, serial}
{
}

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


#ifndef __WIN32
    // Initialzing the struct to zero will make the FTD3xx library use default
    // values for all parameters.
    FT_TRANSFER_CONF transferConf = {};
    transferConf.wStructSize = sizeof(FT_TRANSFER_CONF);

    FT_PIPE_TRANSFER_CONF &pipeConf = transferConf.pipe[FT_PIPE_DIR_IN];

    pipeConf.fNonThreadSafeTransfer = false;
    pipeConf.bURBCount = 8;
    pipeConf.wURBBufferCount = 32;
    pipeConf.dwURBBufferSize = 1024 * 1024;
    // Total allocated buffer size is wURBBufferCount * dwURBBufferSize

    st = FT_SetTransferParams(&transferConf, get_fifo_id(Pipe::Data));

    if (auto ec = make_error_code(st))
        return ec;
#endif

    DeviceInfo devInfo = {};

    switch (m_connectMode.mode)
    {
        case ConnectMode::First:
            {
                st = FT_DEVICE_NOT_FOUND;
                auto infoList = get_device_info_list();

                if (!infoList.empty())
                {
                    devInfo = infoList[0];
                    st = FT_Create(reinterpret_cast<void *>(devInfo.index),
                                   FT_OPEN_BY_INDEX, &m_handle);
                }
            }
            break;

        case ConnectMode::ByIndex:
            {
                st = FT_DEVICE_NOT_FOUND;
                auto infoList = get_device_info_list();

                if (m_connectMode.index < infoList.size())
                {
                    devInfo = infoList[0];
                    st = FT_Create(reinterpret_cast<void *>(devInfo.index),
                                   FT_OPEN_BY_INDEX, &m_handle);
                }
            }
            break;

        case ConnectMode::BySerial:
            {
                st = FT_DEVICE_NOT_FOUND;

                if ((devInfo = get_device_info_by_serial(m_connectMode.serial)))
                {

                    st = FT_Create(reinterpret_cast<void *>(devInfo.index),
                                   FT_OPEN_BY_INDEX, &m_handle);
                }
            }
            break;
    }

    if (auto ec = make_error_code(st))
        return ec;

    m_deviceInfo = devInfo;

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

    LOG_INFO("opened USB device");

    if (auto ec = post_connect_cleanup(*this))
    {
        LOG_WARN("error from USB post connect cleanup: %s", ec.message().c_str());
        return ec;
    }

    LOG_INFO("connected to MVLC USB");

    return {};

#ifdef __WIN32
#if 0
    // FT_SetStreamPipe(handle, allWritePipes, allReadPipes, pipeID, streamSize)
    st = FT_SetStreamPipe(m_handle, false, true, 0, USBSingleTransferMaxBytes);

    if (auto ec = make_error_code(st))
    {
        fprintf(stderr, "%s: FT_SetStreamPipe failed: %s",
                __PRETTY_FUNCTION__, ec.message().c_str());
        closeHandle();
        return ec;
    }
#endif
#endif // __WIN32
}

std::error_code Impl::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    auto ec = closeHandle();

    LOG_INFO("disconnected");

    return ec;
}

bool Impl::isConnected() const
{
    return m_handle != nullptr;
}

std::error_code Impl::setWriteTimeout(Pipe pipe, unsigned ms)
{
    auto p = static_cast<unsigned>(pipe);

    if (p >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    m_writeTimeouts[p] = ms;

    if (isConnected())
        return set_endpoint_timeout(m_handle, get_endpoint(pipe, EndpointDirection::Out), ms);

    return {};
}

std::error_code Impl::setReadTimeout(Pipe pipe, unsigned ms)
{
    auto p = static_cast<unsigned>(pipe);

    if (p >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    m_readTimeouts[p] = ms;

    if (isConnected())
        return set_endpoint_timeout(m_handle, get_endpoint(pipe, EndpointDirection::In), ms);

    return {};
}

unsigned Impl::getWriteTimeout(Pipe pipe) const
{
    auto up = static_cast<unsigned>(pipe);
    assert(up < PipeCount);
    if (up >= PipeCount) return 0u;
    return m_writeTimeouts[up];
}

unsigned Impl::getReadTimeout(Pipe pipe) const
{
    auto up = static_cast<unsigned>(pipe);
    assert(up < PipeCount);
    if (up >= PipeCount) return 0u;
    return m_readTimeouts[up];
}

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

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
        LOG_WARN("pipe=%u, wrote %lu of %lu bytes, result=%s",
                 static_cast<unsigned>(pipe),
                 bytesTransferred, size,
                 ec.message().c_str());
    }

    return ec;
}

#ifdef __WIN32 // Impl::read() windows
/* Explanation:
 * When reading from a pipe under windows any available data that was not
 * retrieved is lost instead of being returned on the next read attempt. This
 * is different than the behaviour under linux where multiple short reads can
 * be done without losing data.
 * Also the windows library does not run into a timeout if less data than
 * requested is available.
 * To work around the above issue the windows implementation uses a single read
 * buffer of size USBSingleTransferMaxBytes and only issues read requests of
 * that size. Client requests are satisfied from buffered data until the buffer
 * is empty at which point another full sized read is performed.
 */
std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    const size_t requestedSize = size;
    bytesTransferred = 0u;

    auto &readBuffer = m_readBuffers[static_cast<unsigned>(pipe)];

    // Copy from readBuffer into the dest buffer while updating local
    // variables.
    auto copy_and_update = [&buffer, &size, &bytesTransferred, &readBuffer] ()
    {
        if (size_t toCopy = std::min(readBuffer.size(), size))
        {
            memcpy(buffer, readBuffer.first, toCopy);
            buffer += toCopy;
            size -= toCopy;
            readBuffer.first += toCopy;
            bytesTransferred += toCopy;
        }
    };

    LOG_TRACE("pipe=%u, size=%u, bufferSize=%u",
              static_cast<unsigned>(pipe), requestedSize, readBuffer.size());

    copy_and_update();

    if (size == 0)
    {
        LOG_TRACE("pipe=%u, size=%u, read request satisfied from buffer, new buffer size=%u",
                  static_cast<unsigned>(pipe), requestedSize, readBuffer.size());
        return {};
    }

    // All data from the read buffer should have been consumed at this point.
    // It's time to issue an actual read request.
    assert(readBuffer.size() == 0);

    LOG_TRACE("pipe=%u, requestedSize=%u, remainingSize=%u, reading from MVLC...",
              static_cast<unsigned>(pipe), requestedSize, size);

    ULONG transferred = 0; // FT API wants a ULONG* parameter

    FT_STATUS st = FT_ReadPipeEx(m_handle, get_endpoint(pipe, EndpointDirection::In),
                                 readBuffer.data.data(),
                                 readBuffer.capacity(),
                                 &transferred,
                                 nullptr);

    auto ec = make_error_code(st);

    LOG_TRACE("pipe=%u, requestedSize=%u, remainingSize=%u, read result: ec=%s, transferred=%u",
              static_cast<unsigned>(pipe), requestedSize, size,
              ec.message().c_str(), transferred);

    readBuffer.first = readBuffer.data.data();
    readBuffer.last  = readBuffer.first + transferred;

    copy_and_update();

    if (ec && ec != ErrorType::Timeout)
        return ec;

    if (size > 0)
    {
        LOG_DEBUG("pipe=%u, requestedSize=%u, remainingSize=%u after read from MVLC,"
                  "returning FT_TIMEOUT (original ec=%s)",
                  static_cast<unsigned>(pipe), requestedSize, size,
                  ec.message().c_str());

        return make_error_code(FT_TIMEOUT);
    }

    LOG_TRACE("pipe=%u, size=%u, read request satisfied after read from MVLC. new buffer size=%u",
              static_cast<unsigned>(pipe), requestedSize, readBuffer.size());

    return {};
}
#else // Impl::read() linux
std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    LOG_TRACE("begin read: pipe=%u, size=%lu bytes",
              static_cast<unsigned>(pipe), size);

    ULONG transferred = 0; // FT API wants a ULONG* parameter

    FT_STATUS st = FT_ReadPipeEx(m_handle, get_fifo_id(pipe),
                                 buffer, size,
                                 &transferred,
                                 m_readTimeouts[static_cast<unsigned>(pipe)]);

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    if (ec && ec != ErrorType::Timeout)
    {
        LOG_WARN("pipe=%u, read %lu of %lu bytes, result=%s",
                 static_cast<unsigned>(pipe),
                 bytesTransferred, size,
                 ec.message().c_str());
    }

    return ec;
}
#endif // Impl::read

std::error_code Impl::read_unbuffered(Pipe pipe, u8 *buffer, size_t size,
                                      size_t &bytesTransferred)
{
    assert(buffer);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    LOG_TRACE("begin unbuffered read: pipe=%u, size=%lu bytes",
              static_cast<unsigned>(pipe), size);

    ULONG transferred = 0; // FT API wants a ULONG* parameter

#ifdef __WIN32
    FT_STATUS st = FT_ReadPipeEx(m_handle, get_endpoint(pipe, EndpointDirection::In),
                                 buffer, size,
                                 &transferred,
                                 nullptr);
#else
    FT_STATUS st = FT_ReadPipeEx(m_handle, get_fifo_id(pipe),
                                 buffer, size,
                                 &transferred,
                                 m_readTimeouts[static_cast<unsigned>(pipe)]);
#endif

    bytesTransferred = transferred;
    auto ec = make_error_code(st);

    LOG_TRACE("end unbuffered read: pipe=%u, size=%lu bytes, transferred=%lu bytes, ec=%s",
              static_cast<unsigned>(pipe), size, bytesTransferred, ec.message().c_str());

    return ec;
}

std::error_code Impl::getReadQueueSize(Pipe pipe, u32 &dest)
{
    assert(static_cast<unsigned>(pipe) < PipeCount);

#ifndef __WIN32
    FT_STATUS st = FT_GetReadQueueStatus(m_handle, get_fifo_id(pipe), &dest);
#else
    FT_STATUS st = FT_OK;
    dest = m_readBuffers[static_cast<unsigned>(pipe)].size();
#endif

    return make_error_code(st);
}

std::string Impl::connectionInfoString() const
{
    std::string result = "speed=";

    auto devInfo = getDeviceInfo();

    if (devInfo.flags & DeviceInfo::Flags::USB2)
        result += "USB2";
    else if (devInfo.flags & DeviceInfo::Flags::USB3)
        result += "USB3";
    else
        result += "unknown";

    result += ", serial=" + devInfo.serial;

    return result;
}

} // end namespace usb
} // end namespace mvlc
} // end namespace mesytec
