#ifndef __MVLC_USB_IMPL_H__
#define __MVLC_USB_IMPL_H__

#include <array>
#include <ftd3xx.h>
#include "mvlc/mvlc_impl_abstract.h"

namespace std
{
    template<> struct is_error_code_enum<_FT_STATUS>: true_type {};
} // end namespace std

namespace mesytec
{
namespace mvlc
{
namespace usb
{

std::error_code make_error_code(FT_STATUS st);

// TODO: add helpers to get a list of all device, recreate the list, filter for
// MVLCs, etc. Bascially wrappers around FT_CreateDeviceInfoList() and
// FT_GetDeviceInfoList()

// While the FTDI handle is not being modified multiple threads can access all
// pipes concurrently.
// Optionally the fNonThreadSafeTransfer flag can be set per pipe and direction. Then
// the software must ensure that only one thread accesses each of the pipes
// simultaneously. It's still ok for one thread to use pipe0 and another to use pipe1.
//
//        / Pipe0 / Endpoint 0x02 OUT/0x82 IN - Command Pipe, bidirectional
// handle
//        \ Pipe1 / Endpoint 0x83 IN - Data Pipe, read only

class Impl: public AbstractImpl
{
    public:
        // The constructors do not call open(). They just setup the information
        // needed for the open call to do its work.

        // Uses the first device matching the description "MVLC".
        // TODO: Impl();

        // Absolute index of the USB device to open. Does not check if the
        // description actually matches the MVLC.
        explicit Impl(int index);

        // Open the MVLC with the given serial number
        // TODO: explicit Impl(const std::string &serial);

        // Disconnects if connected
        ~Impl();

        std::error_code connect() override;
        std::error_code disconnect() override;
        bool isConnected() const override;

        void setWriteTimeout(Pipe pipe, unsigned ms) override;
        void setReadTimeout(Pipe pipe, unsigned ms) override;

        unsigned getWriteTimeout(Pipe pipe) const override;
        unsigned getReadTimeout(Pipe pipe) const override;

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override;

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override;

        std::error_code get_read_queue_size(Pipe pipe, u32 &dest);

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
};

} // end namespace usb
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_USB_IMPL_H__ */
