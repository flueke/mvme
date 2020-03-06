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

#include "mvlc.h"
#include "mvlc_dialog.h"

namespace mesytec
{
namespace mvlc
{

struct MVLC::Private
{
    Private(std::unique_ptr<MVLCBasicInterface> &&impl_)
        : impl(std::move(impl_))
        , dialog(impl.get())
    {
    }

    std::unique_ptr<MVLCBasicInterface> impl;
    MVLCDialog dialog;
    mutable Locks locks;
};

MVLC::MVLC(std::unique_ptr<MVLCBasicInterface> &&impl)
    : d(std::make_shared<Private>(std::move(impl)))
{
}

MVLC::~MVLC()
{
}

MVLCBasicInterface *MVLC::getImpl()
{
    return d->impl.get();
}

Locks &MVLC::getLocks()
{
    return d->locks;
}

std::error_code MVLC::connect()
{
    auto guards = d->locks.lockBoth();
    return d->impl->connect();
}

std::error_code MVLC::disconnect()
{
    auto guards = d->locks.lockBoth();
    return d->impl->disconnect();
}

bool MVLC::isConnected() const
{
    auto guards = d->locks.lockBoth();
    return d->impl->isConnected();
}

ConnectionType MVLC::connectionType() const
{
    auto guards = d->locks.lockBoth();
    return d->impl->connectionType();
}

std::string MVLC::connectionInfo() const
{
    auto guards = d->locks.lockBoth();
    return d->impl->connectionInfo();
}

std::error_code MVLC::write(Pipe pipe, const u8 *buffer, size_t size,
                      size_t &bytesTransferred)
{
    auto guard = d->locks.lock(pipe);
    return d->impl->write(pipe, buffer, size, bytesTransferred);
}

std::error_code MVLC::read(Pipe pipe, u8 *buffer, size_t size,
                     size_t &bytesTransferred)
{
    auto guard = d->locks.lock(pipe);
    return d->impl->read(pipe, buffer, size, bytesTransferred);
}

std::error_code MVLC::setWriteTimeout(Pipe pipe, unsigned ms)
{
    auto guard = d->locks.lock(pipe);
    return d->impl->setWriteTimeout(pipe, ms);
}

std::error_code MVLC::setReadTimeout(Pipe pipe, unsigned ms)
{
    auto guard = d->locks.lock(pipe);
    return d->impl->setReadTimeout(pipe, ms);
}

unsigned MVLC::getWriteTimeout(Pipe pipe) const
{
    auto guard = d->locks.lock(pipe);
    return d->impl->getWriteTimeout(pipe);
}

unsigned MVLC::getReadTimeout(Pipe pipe) const
{
    auto guard = d->locks.lock(pipe);
    return d->impl->getReadTimeout(pipe);
}

std::error_code MVLC::readRegister(u16 address, u32 &value)
{
    auto guard = d->locks.lockCmd();
    return d->dialog.readRegister(address, value);
}

std::error_code MVLC::writeRegister(u16 address, u32 value)
{
    auto guard = d->locks.lockCmd();
    return d->dialog.writeRegister(address, value);
}

std::error_code MVLC::vmeSingleRead(u32 address, u32 &value, u8 amod,
                              VMEDataWidth dataWidth)
{
    auto guard = d->locks.lockCmd();
    return d->dialog.vmeSingleRead(address, value, amod, dataWidth);
}

std::error_code MVLC::vmeSingleWrite(u32 address, u32 value, u8 amod,
                               VMEDataWidth dataWidth)
{
    auto guard = d->locks.lockCmd();
    return d->dialog.vmeSingleWrite(address, value, amod, dataWidth);
}

std::error_code MVLC::vmeBlockRead(u32 address, u8 amod, u16 maxTransfers,
                             std::vector<u32> &dest)
{
    auto guard = d->locks.lockCmd();
    return d->dialog.vmeBlockRead(address, amod, maxTransfers, dest);
}

std::error_code MVLC::readResponse(BufferHeaderValidator bhv, std::vector<u32> &dest)
{
    auto guard = d->locks.lockCmd();
    return d->dialog.readResponse(bhv, dest);
}

std::error_code MVLC::mirrorTransaction(
    const std::vector<u32> &cmdBuffer, std::vector<u32> &responseDest)
{
    auto guard = d->locks.lockCmd();
    return d->dialog.mirrorTransaction(cmdBuffer, responseDest);
}

std::error_code MVLC::stackTransaction(const std::vector<u32> &stackUploadData,
                                 std::vector<u32> &responseDest)
{
    auto guard = d->locks.lockCmd();
    return d->dialog.stackTransaction(stackUploadData, responseDest);
}

std::error_code MVLC::readKnownBuffer(std::vector<u32> &dest)
{
    auto guard = d->locks.lockCmd();
    return d->dialog.readKnownBuffer(dest);
}

std::vector<u32> MVLC::getResponseBuffer() const
{
    auto guard = d->locks.lockCmd();
    return d->dialog.getResponseBuffer();
}

std::vector<std::vector<u32>> MVLC::getStackErrorNotifications() const
{
    auto guard = d->locks.lockCmd();
    return d->dialog.getStackErrorNotifications();
}

void MVLC::clearStackErrorNotifications()
{
    auto guard = d->locks.lockCmd();
    d->dialog.clearStackErrorNotifications();
}

bool MVLC::hasStackErrorNotifications() const
{
    auto guard = d->locks.lockCmd();
    return d->dialog.hasStackErrorNotifications();
}

}
}
