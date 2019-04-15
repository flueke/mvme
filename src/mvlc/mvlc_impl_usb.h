#ifndef __MVLC_USB_IMPL_H__
#define __MVLC_USB_IMPL_H__

#include <array>
#include <ftd3xx.h>
#include <thread>
#include <memory>
#include <vector>
#include "libmvme_mvlc_export.h"
#include "mvlc/mvlc_impl_abstract.h"
#ifdef __WIN32
#include "mvlc/mvlc_impl_support.h"
#endif

namespace mesytec
{
namespace mvlc
{
namespace usb
{

// Structure of how the MVLC is represented when using the FTDI D3XX driver:
//
//        / Pipe0: FIFO 0 / Endpoint 0x02 OUT/0x82 IN - Command Pipe, bidirectional
// handle
//        \ Pipe1: FIFO 1 / Endpoint          0x83 IN - Data Pipe, read only
//
// Only the FTDI handle (a void *) exists as a variable in the code. The pipes
// are addressed by passing numeric FIFO id or Endpoint numbers to the various
// driver functions.
//
// Provided that the FTDI handle itself is not being modified (e.g. by closing
// the device) multiple threads can access both of the pipes concurrently.
// Synchronization is done within the D3XX driver layer.
// Optionally the fNonThreadSafeTransfer flag can be set per pipe and
// direction. Then the software must ensure that only one thread accesses each
// of the pipes simultaneously. It's still ok for one thread to use pipe0 and
// another to use pipe1.

struct DeviceInfo
{
    struct Flags
    {
        static const u8 Opened = 1;
        static const u8 USB2   = 2;
        static const u8 USB3   = 4;
    };


    int index = -1;             // index value used by the FTDI lib for this device.
    std::string serial;         // usb serial number string
    std::string description;    // usb device description string
    u8 flags = 0;               // Flags bits
    void *handle = nullptr;     // FTDI handle if opened

    operator bool() const { return index >= 0; }
};

using DeviceInfoList = std::vector<DeviceInfo>;

enum class LIBMVME_MVLC_EXPORT ListOptions
{
    MVLCDevices,
    AllDevices,
};

LIBMVME_MVLC_EXPORT std::string format_serial(unsigned serial);

LIBMVME_MVLC_EXPORT DeviceInfoList get_device_info_list(
    const ListOptions opts = ListOptions::MVLCDevices);

LIBMVME_MVLC_EXPORT DeviceInfo get_device_info_by_serial(
    const std::string &serial);

LIBMVME_MVLC_EXPORT DeviceInfo get_device_info_by_serial(
    unsigned serial);

class LIBMVME_MVLC_EXPORT Impl: public AbstractImpl
{
    public:
        // The constructors do not call connect(). They just setup the
        // information needed for the connect() call to do its work.

        // Uses the first device matching the description "MVLC".
        Impl();

        // Absolute index of the USB device to open.
        // This is the index value assigned by the FTDI library and returned in
        // the DeviceInfo structure. This constructor does not check whether
        // the USB description entry contains "MVLC".
        explicit Impl(int index);

        // Open the MVLC with the given serial number
        explicit Impl(const std::string &serial);

        // Disconnects if connected
        ~Impl();

        std::error_code connect() override;
        std::error_code disconnect() override;
        bool isConnected() const override;

        std::error_code setWriteTimeout(Pipe pipe, unsigned ms) override;
        std::error_code setReadTimeout(Pipe pipe, unsigned ms) override;

        unsigned getWriteTimeout(Pipe pipe) const override;
        unsigned getReadTimeout(Pipe pipe) const override;

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override;

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override;

        ConnectionType connectionType() const override { return ConnectionType::USB; }

        std::error_code getReadQueueSize(Pipe pipe, u32 &dest);

    private:
        struct ConnectMode
        {
            enum Mode
            {
                First,
                ByIndex,
                BySerial
            };

            Mode mode = First;
            int index = 0;
            std::string serial;
        };

        void *m_handle = nullptr;
        ConnectMode m_connectMode;

        std::array<unsigned, PipeCount> m_writeTimeouts = {
            DefaultWriteTimeout_ms, DefaultWriteTimeout_ms
        };

        std::array<unsigned, PipeCount> m_readTimeouts = {
            DefaultReadTimeout_ms, DefaultReadTimeout_ms
        };

        std::error_code closeHandle();

#ifdef __WIN32
        std::array<ReadBuffer<USBSingleTransferMaxBytes>, PipeCount> m_readBuffers;
#endif
};

LIBMVME_MVLC_EXPORT std::error_code make_error_code(FT_STATUS st);

} // end namespace usb
} // end namespace mvlc
} // end namespace mesytec

namespace std
{
    template<> struct is_error_code_enum<_FT_STATUS>: true_type {};
} // end namespace std

#endif /* __MVLC_USB_IMPL_H__ */
