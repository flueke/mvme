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
#include "mvlc_dialog.h"

#include <cassert>
#include <cstdio>
#include <iostream>

#include "mvlc_command_builders.h"
#include "mvlc_error.h"
#include "mvlc_util.h"
//#include "util/debug_timer.h"

#define LOG_LEVEL_OFF     0
#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MVLC_DIALOG_LOG_LEVEL
#define MVLC_DIALOG_LOG_LEVEL LOG_LEVEL_WARN
#endif

#define LOG_LEVEL_SETTING MVLC_DIALOG_LOG_LEVEL

#define DO_LOG(level, prefix, fmt, ...)\
do\
{\
    if (LOG_LEVEL_SETTING >= level)\
    {\
        fprintf(stderr, prefix "%s(): " fmt "\n", __FUNCTION__, ##__VA_ARGS__);\
    }\
} while (0);


#define LOG_WARN(fmt, ...)  DO_LOG(LOG_LEVEL_WARN,  "WARN - mvlc_dialog ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DO_LOG(LOG_LEVEL_INFO,  "INFO - mvlc_dialog ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - mvlc_dialog ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DO_LOG(LOG_LEVEL_TRACE, "TRACE - mvlc_dialog ", fmt, ##__VA_ARGS__)

namespace mesytec
{
namespace mvlc
{

std::error_code check_mirror(const std::vector<u32> &request, const std::vector<u32> &response)
{
    if (request.empty())
    {
        return make_error_code(MVLCErrorCode::MirrorEmptyRequest);
    }

    if (response.empty())
    {
        return make_error_code(MVLCErrorCode::MirrorEmptyResponse);
    }

    if (response.size() < request.size() - 1)
    {
        return make_error_code(MVLCErrorCode::MirrorShortResponse);
    }

    const int minIndex = 1; // skip buffer header
    const int endIndex = request.size() - 1;

    for (int i = minIndex; i < endIndex; i++)
    {
        if (request[i] != response[i])
            return make_error_code(MVLCErrorCode::MirrorNotEqual);
    }

    return {};
}

constexpr std::chrono::milliseconds MVLCDialog::ReadResponseMaxWait;

MVLCDialog::MVLCDialog(MVLCBasicInterface *mvlc)
    : m_mvlc(mvlc)
{
    assert(m_mvlc);
}

std::error_code MVLCDialog::doWrite(const std::vector<u32> &buffer)
{
    size_t bytesTransferred = 0u;
    const size_t bytesToTransfer = buffer.size() * sizeof(u32);
    auto ec = m_mvlc->write(Pipe::Command, reinterpret_cast<const u8 *>(buffer.data()),
                            bytesToTransfer, bytesTransferred);

    if (!ec && bytesToTransfer != bytesTransferred)
    {
        LOG_WARN("tried to write %lu bytes, wrote %lu bytes",
                 bytesToTransfer, bytesTransferred);
        return make_error_code(MVLCErrorCode::ShortWrite);
    }

    return ec;
};

// Returns MVLCErrorCode::ShortRead in case less than the desired amount of
// words could be read.
std::error_code MVLCDialog::readWords(u32 *dest, size_t count, size_t &wordsTransferred)
{
    if (count == 0)
    {
        wordsTransferred = 0u;
        return {};
    }

    std::error_code ec;
    size_t bytesToTransfer = count * sizeof(u32);
    size_t bytesTransferred = 0u;

    // Note: the loop is a workaround for an issue happening only when
    // connected via USB2: the read call fails with a timeout despite data
    // being available at the endpoint. This can be verified by using
    // getReadQueueSize() directly after the read that timed out.
    // A 2nd read issued right after the timeout will succeed and yield the
    // correct data.
    // I have not encountered this issue when connected via USB3.  This
    // workaround has the side effect of multiplying the potential maximum time
    // spent waiting for a timeout by MaxReadAttempts.
    static const u16 MaxReadAttempts = 2;
    u16 attempts = 0;

    do
    {
        ec = m_mvlc->read(Pipe::Command,
                          reinterpret_cast<u8 *>(dest),
                          bytesToTransfer,
                          bytesTransferred);

        //std::cout << __PRETTY_FUNCTION__
        //    << " attempt=" << attempts + 1
        //    << ", ec=" << ec.message()
        //    << ", bytesTransferred=" << bytesTransferred
        //    << std::endl;

    } while (ec == ErrorType::Timeout
             && bytesTransferred == 0
             && ++attempts < MaxReadAttempts);

    if (bytesTransferred > 0 && attempts > 0)
    {
        LOG_DEBUG("Needed %u reads to receive incoming data.", attempts+1);
    }

    wordsTransferred = bytesTransferred / sizeof(u32);

    if (ec)
        return ec;

    if (bytesTransferred != bytesToTransfer)
        return make_error_code(MVLCErrorCode::ShortRead);

    return ec;
}

std::error_code MVLCDialog::readKnownBuffer(std::vector<u32> &dest)
{
    dest.resize(0);

    u32 header = 0u;
    size_t wordsTransferred = 0u;

    if (auto ec = readWords(&header, 1, wordsTransferred))
        return ec;

    if (!is_known_frame_header(header))
    {
        dest.resize(1);
        dest[0] = header;
        return make_error_code(MVLCErrorCode::InvalidBufferHeader);
    }

    u16 responseLength = (header & FrameSizeMask);
    dest.resize(1 + responseLength);
    dest[0] = header;

    auto ec = readWords(dest.data() + 1, responseLength, wordsTransferred);

    if (ec == make_error_code(MVLCErrorCode::ShortRead))
    {
        // Got less words than requested. Adjust the destination size to the
        // number of words actually received.
        dest.resize(1 + wordsTransferred);
    }

    return ec;
}

std::error_code MVLCDialog::readResponse(BufferHeaderValidator bhv, std::vector<u32> &dest)
{
    assert(bhv);

    using Clock = std::chrono::steady_clock;

    auto tStart = Clock::now();

    while (true)
    {
        if (auto ec = readKnownBuffer(dest))
            return ec;

        // readKnownBuffer() should return an error code if its dest buffer is empty
        assert(!dest.empty());

        u32 header = dest[0];

        if (is_stackerror_notification(header))
            m_stackErrorNotifications.push_back(dest);
        else
            break;

        auto elapsed = Clock::now() - tStart;

        if (elapsed >= ReadResponseMaxWait)
            return make_error_code(MVLCErrorCode::ReadResponseMaxWaitExceeded);
    }

    assert(!dest.empty());
    if (dest.empty())
        return make_error_code(MVLCErrorCode::ShortRead);

    u32 header = dest[0];

    if (!bhv(header))
    {
        LOG_WARN("readResponse header validation failed, header=0x%08x", header);
        return make_error_code(MVLCErrorCode::UnexpectedBufferHeader);
    }

    return {};
}

std::error_code MVLCDialog::readRegister(u16 address, u32 &value)
{
    SuperCommandBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addReadLocal(address);

    auto request = make_command_buffer(cmdList);
    logBuffer(request, "readRegister >>>");

    auto ec = mirrorTransaction(request, m_responseBuffer);
    logBuffer(m_responseBuffer, "readRegister <<<");
    if (ec) return ec;

    if (m_responseBuffer.size() < 4)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    value = m_responseBuffer[3];

    return {};
}

#if 0
std::error_code MVLCDialog::readRegisterBlock(u16 address, u16 words,
                                              std::vector<u32> &dest)
{
    if (words > ReadLocalBlockMaxWords)
        return make_error_code(MVLCErrorCode::CommandArgOutOfRange);

    SuperCommandBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addReadLocalBlock(address, words);

    std::vector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    logBuffer(request, "readRegisterBlock >>>");

    if (auto ec = doWrite(request))
        return ec;

    if (auto ec = readResponse(is_super_buffer, m_responseBuffer))
        return ec;

    logBuffer(m_responseBuffer, "readRegisterBlock <<<");

    if (auto ec = check_mirror(request, m_responseBuffer))
        return ec;

    // copy resulting words into dest
    auto mirrorLen = request.size() - 1;

    dest.reserve(m_responseBuffer.size() - mirrorLen);
    dest.clear();

    std::copy(std::begin(m_responseBuffer) + mirrorLen,
              std::end(m_responseBuffer),
              std::back_inserter(dest));

    return {};
}
#endif

std::error_code MVLCDialog::writeRegister(u16 address, u32 value)
{
    SuperCommandBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addWriteLocal(address, value);

    auto request = make_command_buffer(cmdList);
    logBuffer(request, "writeRegister >>>");

    auto ec = mirrorTransaction(request, m_responseBuffer);
    logBuffer(m_responseBuffer, "writeRegister <<<");

    if (ec)
        return ec;

    if (m_responseBuffer.size() != 4)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    return {};
}

std::error_code MVLCDialog::mirrorTransaction(const std::vector<u32> &cmdBuffer,
                                              std::vector<u32> &dest)
{
    for (unsigned tries = 0u; tries < MirrorMaxRetries; tries++)
    {
        // upload the stack
        if (auto ec = doWrite(cmdBuffer))
        {
            LOG_WARN("write error: %s (attempt %u of %u)",
                     ec.message().c_str(),
                     tries+1, MirrorMaxRetries);

            if (ec == ErrorType::Timeout)
                continue;

            return ec;
        }

        // read the mirror response
        if (auto ec = readResponse(is_super_buffer, dest))
        {
            LOG_WARN("read error: %s (attempt %u of %u)",
                     ec.message().c_str(),
                     tries+1, MirrorMaxRetries);

            if (ec == ErrorType::Timeout)
                continue;

            return ec;
        }

        // verify the mirror response
        return check_mirror(cmdBuffer, dest);
    }

    return make_error_code(MVLCErrorCode::MirrorMaxTriesExceeded);
}

std::error_code MVLCDialog::stackTransaction(const std::vector<u32> &stack,
                                             std::vector<u32> &dest)
{
    //DebugTimer timer;

    // upload, read mirror, verify mirror
    if (auto ec = mirrorTransaction(stack, dest))
        return ec;

    //auto dt_mirror = timer.restart();

    // set the stack 0 offset register
    if (auto ec = writeRegister(stacks::Stack0OffsetRegister, 0))
        return ec;

    // exec stack 0
    if (auto ec = writeRegister(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift))
        return ec;

    //auto dt_writeStackRegisters = timer.restart();

    // read the stack response into the supplied buffer
    if (auto ec = readResponse(is_stack_buffer, dest))
        return ec;

    assert(!dest.empty()); // guaranteed by readResponse()

    // Test if the Continue bit is set and if so read continuation buffers
    // (0xF9) until the Continue bit is cleared.
    // Note: stack error notification buffers (0xF7) as part of the response are
    // handled in readResponse().

    u32 header = dest[0];
    u8 flags = extract_frame_info(header).flags;

    if (flags & frame_flags::Continue)
    {
        std::vector<u32> localBuffer;

        while (flags & frame_flags::Continue)
        {
            if (auto ec = readResponse(is_stack_buffer_continuation, localBuffer))
                return ec;

            std::copy(localBuffer.begin(), localBuffer.end(), std::back_inserter(dest));

            header = !localBuffer.empty() ? localBuffer[0] : 0u;
            flags = extract_frame_info(header).flags;
        }
    }

#if 0
    auto dt_readResponse = timer.restart();

#define ms_(x) std::chrono::duration_cast<std::chrono::milliseconds>(x)

    LOG_DEBUG("dt_mirror=%ld, dt_writeStackRegisters=%ld, dt_readResponse=%ld\n",
              ms_(dt_mirror).count(),
              ms_(dt_writeStackRegisters).count(),
              ms_(dt_readResponse).count());

#undef ms_
#endif

    // Check the last buffers flag values.

    if (flags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    if (flags & frame_flags::SyntaxError)
        return MVLCErrorCode::StackSyntaxError;

    return {};
}

std::error_code MVLCDialog::vmeWrite(u32 address, u32 value, u8 amod,
                                           VMEDataWidth dataWidth)
{
    SuperCommandBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMEWrite(address, value, amod, dataWidth);

    auto request = make_command_buffer(cmdList);

    auto ec = stackTransaction(request, m_responseBuffer);

    logBuffer(m_responseBuffer, "vmeWrite response");

    if (ec)
        return ec;

    if (m_responseBuffer.size() != 1)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    if (extract_frame_info(m_responseBuffer[0]).flags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    return ec;
}

std::error_code MVLCDialog::vmeRead(u32 address, u32 &value, u8 amod,
                                          VMEDataWidth dataWidth)
{
    SuperCommandBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMERead(address, amod, dataWidth);

    auto request = make_command_buffer(cmdList);

    auto ec = stackTransaction(request, m_responseBuffer);

    logBuffer(m_responseBuffer, "vmeRead response");

    if (ec)
        return ec;

    if (m_responseBuffer.size() != 2)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    if (extract_frame_info(m_responseBuffer[0]).flags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    const u32 Mask = (dataWidth == VMEDataWidth::D16 ? 0x0000FFFF : 0xFFFFFFFF);

    value = m_responseBuffer[1] & Mask;

    return ec;
}

std::error_code MVLCDialog::vmeBlockRead(u32 address, u8 amod, u16 maxTransfers,
                                         std::vector<u32> &dest)
{
    SuperCommandBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMEBlockRead(address, amod, maxTransfers);

    auto request = make_command_buffer(cmdList);

    auto ec = stackTransaction(request, dest);

    logBuffer(dest, "vmeBlockRead response");

    return ec;
}

void MVLCDialog::logBuffer(const std::vector<u32> &buffer, const std::string &info)
{
    if (LOG_LEVEL_SETTING >= LOG_LEVEL_TRACE)
    {
        log_buffer(std::cerr, buffer.data(), buffer.size(),
                   ("MVLCDialog::" + info).c_str());
    }
}

} // end namespace mvlc
} // end namespace mesytec
