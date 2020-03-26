/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __MVLC_USB_IMPL_H__
#define __MVLC_USB_IMPL_H__

#include <array>
#include <thread>
#include <memory>
#include <vector>

#include "mesytec-mvlc_export.h"
#include "mvlc_basic_interface.h"
#ifdef __WIN32
#include "mvlc_impl_support.h"
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
// Update (Tue 05/14/2019): FT_SetPipeTimeout is not thread-safe under Windows.
// Using FT_SetPipeTimeout and FT_ReadPipeEx in parallel leads to a deadlock
// even if operating on different pipes. The FT_SetPipeTimeout call never
// returns:
//   ntdll.dll!ZwWaitForSingleObject+0x14
//   KERNELBASE.dll!DeviceIoControl+0x82
//   KERNEL32.DLL!DeviceIoControl+0x80
//   FTD3XX.dll!FT_IoCtl+0x7e
//   FTD3XX.dll!FT_SetPipeTimeout+0x3e

struct DeviceInfo
{
    struct Flags
    {
        // Set if the device is opened by some process at the time the info
        // queried.
        static const u8 Opened = 1;
        static const u8 USB2   = 2;
        static const u8 USB3   = 4;
    };

    int index = -1;             // index value used by the FTDI lib for this device.
    std::string serial;         // usb serial number string
    std::string description;    // usb device description string
    u8 flags = 0;               // Flags bits
    void *handle = nullptr;     // FTDI handle if opened

    inline explicit operator bool() const { return index >= 0; }
};

using DeviceInfoList = std::vector<DeviceInfo>;

enum class ListOptions
{
    MVLCDevices,
    AllDevices,
};

MESYTEC_MVLC_EXPORT DeviceInfoList get_device_info_list(
    const ListOptions opts = ListOptions::MVLCDevices);

MESYTEC_MVLC_EXPORT DeviceInfo get_device_info_by_serial(
    const std::string &serial);

MESYTEC_MVLC_EXPORT DeviceInfo get_device_info_by_serial(
    unsigned serial);

enum class EndpointDirection: u8
{
    In,
    Out
};

class MESYTEC_MVLC_EXPORT Impl: public MVLCBasicInterface
{
    public:
        // The constructors do not call connect(). They just setup the
        // information needed for the connect() call to do its work.

        // Uses the first device matching the description "MVLC".
        Impl();

        // Open the MVLC with the specified index value as used by the FTDI
        // library. Only devices containing "MVLC" in the description are
        // considered.
        explicit Impl(unsigned index);

        // Open the MVLC with the given serial number
        explicit Impl(const std::string &serial);

        // Disconnects if connected
        ~Impl();

        std::error_code connect() override;
        std::error_code disconnect() override;
        bool isConnected() const override;

        std::error_code setWriteTimeout(Pipe pipe, unsigned ms) override;
        std::error_code setReadTimeout(Pipe pipe, unsigned ms) override;

        unsigned writeTimeout(Pipe pipe) const override;
        unsigned readTimeout(Pipe pipe) const override;

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override;

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override;

        std::error_code read_unbuffered(Pipe pipe, u8 *buffer, size_t size,
                                        size_t &bytesTransferred);

        std::error_code abortPipe(Pipe pipe, EndpointDirection dir);

        ConnectionType connectionType() const override { return ConnectionType::USB; }
        std::string connectionInfo() const override;

        DeviceInfo getDeviceInfo() const { return m_deviceInfo; }

        void setDisableTriggersOnConnect(bool b) override
        {
            m_disableTriggersOnConnect = b;
        }

        bool disableTriggersOnConnect() const override
        {
            return m_disableTriggersOnConnect;
        }

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
            unsigned index = 0;
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
        DeviceInfo m_deviceInfo;
        bool m_disableTriggersOnConnect = false;
};

//MESYTEC_MVLC_EXPORT std::error_code make_error_code(FT_STATUS st);

} // end namespace usb
} // end namespace mvlc
} // end namespace mesytec

#if 0
namespace std
{
    template<> struct is_error_code_enum<_FT_STATUS>: true_type {};
} // end namespace std
#endif

#endif /* __MVLC_USB_IMPL_H__ */
