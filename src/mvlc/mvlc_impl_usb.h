#ifndef __MVLC_USB_IMPL_H__
#define __MVLC_USB_IMPL_H__

#include <array>
#include <ftd3xx.h>
#include <thread>
#include <memory>
#include "libmvme_mvlc_export.h"
#include "mvlc/mvlc_impl_abstract.h"

namespace mesytec
{
namespace mvlc
{
namespace usb
{

// TODO: add helpers to get a list of all devices, recreate the list, filter for
// MVLCs, etc. Bascially wrappers around FT_CreateDeviceInfoList() and
// FT_GetDeviceInfoList()

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

class LIBMVME_MVLC_EXPORT Impl: public AbstractImpl
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

        std::error_code closeHandle();

#ifdef __WIN32
        template<size_t Capacity>
        struct ReadBuffer
        {
            std::array<u8, Capacity> data;
            u8 *first;
            u8 *last;

            ReadBuffer() { clear(); }
            size_t size() const { return last - first; }
            size_t free() const { return Capacity - size(); }
            size_t capacity() const { return Capacity; }
            void clear() { first = last = data.data(); }
        };
        std::array<ReadBuffer<USBSingleTransferMaxBytes>, PipeCount> m_readBuffers;
#endif
};

std::error_code LIBMVME_MVLC_EXPORT make_error_code(FT_STATUS st);

} // end namespace usb
} // end namespace mvlc
} // end namespace mesytec

namespace std
{
    template<> struct is_error_code_enum<_FT_STATUS>: true_type {};
} // end namespace std

#endif /* __MVLC_USB_IMPL_H__ */
