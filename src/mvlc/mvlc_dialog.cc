#include "mvlc/mvlc_dialog.h"

#include <cassert>
#include <cstdio>
#include <iostream>

#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_script.h"
#include "mvlc/mvlc_util.h"
#include "util/debug_timer.h"

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

std::error_code check_mirror(const QVector<u32> &request, const QVector<u32> &response)
{
    if (request.isEmpty())
    {
        return make_error_code(MVLCErrorCode::MirrorEmptyRequest);
    }

    if (response.isEmpty())
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

MVLCDialog::MVLCDialog(AbstractImpl *mvlc)
    : m_mvlc(mvlc)
{
    assert(m_mvlc);
}

std::error_code MVLCDialog::doWrite(const QVector<u32> &buffer)
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

std::error_code MVLCDialog::readKnownBuffer(QVector<u32> &dest)
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

#ifndef __WIN32
std::error_code MVLCDialog::readKnownBuffer(QVector<u32> &dest, unsigned timeout_ms)
{
    auto prevTimeout = m_mvlc->getReadTimeout(Pipe::Command);
    m_mvlc->setReadTimeout(Pipe::Command, timeout_ms);
    auto result = readKnownBuffer(dest);
    m_mvlc->setReadTimeout(Pipe::Command, prevTimeout);
    return result;
}
#endif

std::error_code MVLCDialog::readResponse(BufferHeaderValidator bhv, QVector<u32> &dest)
{
    assert(bhv);

    using Clock = std::chrono::high_resolution_clock;

    auto tStart = Clock::now();

    while (true)
    {
        if (auto ec = readKnownBuffer(dest))
            return ec;

        // readKnownBuffer() should return an error code if its dest buffer is empty
        assert(!dest.isEmpty());

        u32 header = dest[0];

        if (is_stackerror_notification(header))
            m_stackErrorNotifications.push_back(dest);
        else
            break;

        auto elapsed = Clock::now() - tStart;

        if (elapsed >= ReadResponseMaxWait)
            return make_error_code(MVLCErrorCode::NoResponseReceived);
    }

    assert(!dest.isEmpty());
    if (dest.isEmpty())
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
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addReadLocal(address);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
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
                                              QVector<u32> &dest)
{
    if (words > ReadLocalBlockMaxWords)
        return make_error_code(MVLCErrorCode::CommandArgOutOfRange);

    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addReadLocalBlock(address, words);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
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
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addWriteLocal(address, value);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    logBuffer(request, "writeRegister >>>");

    auto ec = mirrorTransaction(request, m_responseBuffer);
    logBuffer(m_responseBuffer, "writeRegister <<<");
    if (ec) return ec;

    if (m_responseBuffer.size() != 4)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    return {};
}

std::error_code MVLCDialog::mirrorTransaction(const QVector<u32> &cmdBuffer,
                                              QVector<u32> &dest)
{
    DebugTimer timer;

    // upload the stack
    if (auto ec = doWrite(cmdBuffer))
    {
        LOG_WARN("write error: %s", ec.message().c_str());
        return ec;
    }

    auto dt_write = timer.restart();

    // read the mirror response
    if (auto ec = readResponse(is_super_buffer, dest))
    {
        LOG_WARN("read error: %s", ec.message().c_str());
        return ec;
    }

    auto dt_read = timer.restart();

#define ms_(x) std::chrono::duration_cast<std::chrono::milliseconds>(x)
    LOG_TRACE("dt_write=%ld, dt_read=%ld\n",
              ms_(dt_write).count(),
              ms_(dt_read).count());
#undef ms_

    // verify the mirror response
    return check_mirror(cmdBuffer, dest);
}

std::error_code MVLCDialog::stackTransaction(const QVector<u32> &stack,
                                             QVector<u32> &dest)
{
    DebugTimer timer;

    // upload, read mirror, verify mirror
    if (auto ec = mirrorTransaction(stack, dest))
        return ec;

    auto dt_mirror = timer.restart();

    // set the stack 0 offset register
    if (auto ec = writeRegister(stacks::Stack0OffsetRegister, 0))
        return ec;

    // exec stack 0
    if (auto ec = writeRegister(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift))
        return ec;

    auto dt_writeStackRegisters = timer.restart();

    // read the stack response into the supplied buffer
    if (auto ec = readResponse(is_stack_buffer, dest))
        return ec;

    assert(!dest.isEmpty()); // guaranteed by readResponse()

    // Test if the Continue bit is set and if so read continuation buffers
    // (0xF9) until the Continue bit is cleared.
    // Note: stack error notification buffers (0xF7) as part of the response are
    // handled in readResponse().

    u32 header = dest[0];
    u8 flags = extract_frame_info(header).flags;

    if (flags & frame_flags::Continue)
    {
        QVector<u32> localBuffer;

        while (flags & frame_flags::Continue)
        {
            if (auto ec = readResponse(is_stack_buffer_continuation, localBuffer))
                return ec;

            std::copy(localBuffer.begin(), localBuffer.end(), std::back_inserter(dest));

            header = localBuffer[0];
            flags = extract_frame_info(header).flags;
        }
    }

    auto dt_readResponse = timer.restart();

#define ms_(x) std::chrono::duration_cast<std::chrono::milliseconds>(x)

    LOG_DEBUG("dt_mirror=%ld, dt_writeStackRegisters=%ld, dt_readResponse=%ld\n",
              ms_(dt_mirror).count(),
              ms_(dt_writeStackRegisters).count(),
              ms_(dt_readResponse).count());

#undef ms_

    // Check the last buffers flag values.

    if (flags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    if (flags & frame_flags::SyntaxError)
        return MVLCErrorCode::StackSyntaxError;

    return {};
}

std::error_code MVLCDialog::vmeSingleWrite(u32 address, u32 value, u8 amod,
                                           VMEDataWidth dataWidth)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMEWrite(address, value, amod, dataWidth);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());

    auto ec = stackTransaction(request, m_responseBuffer);

    logBuffer(m_responseBuffer, "vmeSingleWrite response");

    if (ec)
        return ec;

    if (m_responseBuffer.size() != 1)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    if (extract_frame_info(m_responseBuffer[0]).flags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    return ec;
}

std::error_code MVLCDialog::vmeSingleRead(u32 address, u32 &value, u8 amod,
                                          VMEDataWidth dataWidth)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMERead(address, amod, dataWidth);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());

    auto ec = stackTransaction(request, m_responseBuffer);

    logBuffer(m_responseBuffer, "vmeSingleRead response");

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
                                         QVector<u32> &dest)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMEBlockRead(address, amod, maxTransfers);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());

    auto ec = stackTransaction(request, dest);

    logBuffer(dest, "vmeBlockRead response");

    return ec;
}

void MVLCDialog::logBuffer(const QVector<u32> &buffer, const QString &info)
{
    if (LOG_LEVEL_SETTING >= LOG_LEVEL_TRACE)
    {
        log_buffer(std::cerr, buffer.data(), buffer.size(),
                   ("MVLCDialog::" + info).toStdString().c_str());
    }
}

} // end namespace mvlc
} // end namespace mesytec
