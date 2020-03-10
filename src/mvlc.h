/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020 mesytec GmbH & Co. KG
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __MESYTEC_MVLC_MVLC_H__
#define __MESYTEC_MVLC_MVLC_H__

#include <memory>
#include <vector>

#include "mesytec-mvlc_export.h"
#include "mvlc_basic_interface.h"
#include "mvlc_buffer_validators.h"
#include "mvlc_constants.h"
#include "mvlc_threading.h"

namespace mesytec
{
namespace mvlc
{

class MESYTEC_MVLC_EXPORT MVLC: public MVLCBasicInterface
{
    public:
        MVLC(std::unique_ptr<MVLCBasicInterface> &&impl);
        ~MVLC() override;

        MVLC(const MVLC &) = default;
        MVLC &operator=(const MVLC &) = default;

        MVLC(MVLC &&) = default;
        MVLC &operator=(MVLC &&) = default;

        //
        // MVLCBasicInterface
        //
        std::error_code connect() override;
        std::error_code disconnect() override;
        bool isConnected() const override;
        ConnectionType connectionType() const override;
        std::string connectionInfo() const override;

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override;

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override;

        std::error_code setWriteTimeout(Pipe pipe, unsigned ms) override;
        std::error_code setReadTimeout(Pipe pipe, unsigned ms) override;

        unsigned getWriteTimeout(Pipe pipe) const override;
        unsigned getReadTimeout(Pipe pipe) const override;

        //
        // Dialog layer
        //
        std::error_code readRegister(u16 address, u32 &value);
        std::error_code writeRegister(u16 address, u32 value);

        std::error_code vmeSingleRead(
            u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth);

        std::error_code vmeSingleWrite(
            u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);

        std::error_code vmeBlockRead(
            u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest);

        std::error_code readResponse(BufferHeaderValidator bhv, std::vector<u32> &dest);

        std::error_code mirrorTransaction(
            const std::vector<u32> &cmdBuffer, std::vector<u32> &responseDest);

        std::error_code stackTransaction(const std::vector<u32> &stackUploadData,
                                         std::vector<u32> &responseDest);

        std::error_code readKnownBuffer(std::vector<u32> &dest);

        std::vector<u32> getResponseBuffer() const;

        //
        // Stack Error Notifications (Command Pipe)
        //
        std::vector<std::vector<u32>> getStackErrorNotifications() const;
        void clearStackErrorNotifications();
        bool hasStackErrorNotifications() const;

        //
        // Access to the low-level implementation and the mutexes.
        //

        MVLCBasicInterface *getImpl();
        Locks &getLocks();

    private:
        struct Private;
        std::shared_ptr<Private> d;
};

}
}

#endif /* __MESYTEC_MVLC_MVLC_H__ */
