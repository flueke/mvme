#ifndef __MVLC_USB_IMPL_H__
#define __MVLC_USB_IMPL_H__

#include <array>
#include <ftd3xx.h>
#include "mvlc/mvlc_abstract_impl.h"

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

class Impl: public AbstractImpl
{
    public:
        // The constructors do not call open(). They just setup the information
        // needed for the open call to do its work.

        // Uses the first device matching the description "MVLC".
        // TODO: Impl();

        // Absolute index of the USB device to open. Does not check if the
        // description actually is MVLC.
        explicit Impl(int index);

        // Open the MVLC with the given serial number
        // TODO: explicit Impl(const std::string &serial);

        std::error_code open() override;
        std::error_code close() override;
        bool is_open() const override;

        void set_write_timeout(Pipe pipe, unsigned ms) override;
        void set_read_timeout(Pipe pipe, unsigned ms) override;

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override;

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override;

        ~Impl();

    private:
        struct OpenInfo
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
        OpenInfo m_openInfo;

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
