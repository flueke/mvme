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
#ifndef __MVLC_DIALOG_H__
#define __MVLC_DIALOG_H__

#include <chrono>
#include <functional>
#include <system_error>
#include "mvlc_basic_interface.h"
#include "mvlc_buffer_validators.h"

// Higher level MVLC dialog (request/response) layer. Builds on top of the
// AbstractImpl abstraction.

namespace mesytec
{
namespace mvlc
{

std::error_code check_mirror(const std::vector<u32> &request, const std::vector<u32> &response);

class MVLCDialog
{
    public:
        constexpr static auto ReadResponseMaxWait = std::chrono::milliseconds(60000);

        MVLCDialog(MVLCBasicInterface *mvlc);

        // MVLC register access
        std::error_code readRegister(u16 address, u32 &value);
        std::error_code writeRegister(u16 address, u32 value);
#if 0 // disabled for now. need to test if this is implemented in the firmware and working.
        std::error_code readRegisterBlock(u16 address, u16 words,
                                          std::vector<u32> &dest);
#endif

        // Higher level VME access
        // Note: Stack0 is used for the VME commands and the stack is written
        // starting from offset 0 into stack memory.

        std::error_code vmeSingleRead(u32 address, u32 &value, u8 amod,
                                      VMEDataWidth dataWidth);

        std::error_code vmeSingleWrite(u32 address, u32 value, u8 amod,
                                       VMEDataWidth dataWidth);

        // Note: The data from the block read is currently returned as is
        // including the stack frame (0xF3) and block frame (0xF5) headers.
        // The flags of either of these headers are not interpreted by this
        // method.
        std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers,
                                     std::vector<u32> &dest);

        // Lower level utilities

        // Read a full response buffer into dest. The buffer header is passed
        // to the BufferHeaderValidator and MVLCErrorCode::InvalidBufferHeader
        // is returned if the validation fails (in this case the data will
        // still be available in the dest buffer for inspection).
        //
        // If no non-error buffer is received within ReadResponseMaxWait the
        // method returns MVLCErrorCode::UnexpectedBufferHeader
        //
        // Note: internally buffers are read from the MVLC until a
        // non-stack_error_notification type buffer is read. All error
        // notifications received up to that point are saved and can be queried
        // using getStackErrorNotifications().
        std::error_code readResponse(BufferHeaderValidator bhv, std::vector<u32> &dest);

        // Sends the given cmdBuffer to the MVLC then reads and verifies the
        // mirror response. The buffer must start with CmdBufferStart and end
        // with CmdBufferEnd, otherwise the MVLC cannot interpret it.
        std::error_code mirrorTransaction(
            const std::vector<u32> &cmdBuffer, std::vector<u32> &responseDest);

        // Sends the given stack data (which must include upload commands),
        // reads and verifies the mirror response, and executes the stack.
        // Notes:
        // - Stack0 is used and offset 0 into stack memory is assumed.
        // - Stack responses consisting of multiple frames (0xF3 followed by
        //   0xF9 frames) are supported. The stack frames will all be copied to
        //   the responseDest vector.
        // - Any stack error notifications read while attempting to read an
        //   actual stack response are available via
        //   getStackErrorNotifications().
        std::error_code stackTransaction(const std::vector<u32> &stackUploadData,
                                         std::vector<u32> &responseDest);

        // Low level read accepting any of the known buffer types (see
        // is_known_buffer_header()). Does not do any special handling for
        // stack error notification buffers as is done in readResponse().
        std::error_code readKnownBuffer(std::vector<u32> &dest);
#ifndef __WIN32
        // Same as readKnownBuffer() above but uses a custom timeout for the read.
        // Currently only available on non-windows systems as under windows the
        // usb pipe timeout cannot be changed while an operation on the other
        // pipe is in progress (A solution to this would be to take both pipe
        // locks under windows, change the timeout and release the data pipe
        // lock but this would negatively effect readout performance.)
        std::error_code readKnownBuffer(std::vector<u32> &dest, unsigned timeout_ms);
#endif

        // Returns the response buffer used internally by readRegister(),
        // readRegisterBlock(), writeRegister(), vmeSingleWrite() and
        // vmeSingleRead().
        // The buffer will contain the last data received from the MVLC.
        std::vector<u32> getResponseBuffer() const { return m_responseBuffer; }

        std::vector<std::vector<u32>> getStackErrorNotifications() const
        {
            return m_stackErrorNotifications;
        }

        void clearStackErrorNotifications()
        {
            m_stackErrorNotifications.clear();
        }

        bool hasStackErrorNotifications() const
        {
            return !m_stackErrorNotifications.empty();
        }

    private:
        std::error_code doWrite(const std::vector<u32> &buffer);
        std::error_code readWords(u32 *dest, size_t count, size_t &wordsTransferred);

        void logBuffer(const std::vector<u32> &buffer, const std::string &info);

        MVLCBasicInterface *m_mvlc = nullptr;
        u32 m_referenceWord = 1;
        std::vector<u32> m_responseBuffer;
        std::vector<std::vector<u32>> m_stackErrorNotifications;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_DIALOG_H__ */
