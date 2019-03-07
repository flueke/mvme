#include "mvlc/mvlc_dialog.h"
#include <cassert>
#include <iostream>

#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_script.h"
#include "mvlc/mvlc_util.h"

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
        return make_error_code(MVLCErrorCode::ShortWrite);

    return ec;
};

// Returns MVLCErrorCode::ShortRead in case less than the desired amount of
// words could be read.
std::error_code MVLCDialog::readWords(u32 *dest, size_t count, size_t &wordsTransferred)
{
    size_t bytesToTransfer = count * sizeof(u32);
    size_t bytesTransferred = 0u;

    auto ec = m_mvlc->read(Pipe::Command,
                           reinterpret_cast<u8 *>(dest),
                           bytesToTransfer,
                           bytesTransferred);

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

    if (!is_known_buffer(header))
        return make_error_code(MVLCErrorCode::InvalidBufferHeader);

    u16 responseLength = (header & BufferSizeMask);
    dest.resize(1 + responseLength);
    dest[0] = header;

    auto ec = readWords(dest.data() + 1, responseLength, wordsTransferred);

    if (ec == make_error_code(MVLCErrorCode::ShortRead))
    {
        // Adjust the destination size to the full number of words transfered.
        dest.resize(1 + wordsTransferred);
    }

    return ec;
}

std::error_code MVLCDialog::readResponse(BufferHeaderValidator bhv, QVector<u32> &dest)
{
    assert(bhv);

    while (true)
    {
        if (auto ec = readKnownBuffer(dest))
            return ec;

        assert(!dest.isEmpty());
        if (dest.isEmpty())
            return make_error_code(MVLCErrorCode::ShortRead);

        u32 header = dest[0];

        if (is_stackerror_notification(header))
            m_stackErrorNotifications.push_back(dest);
        else
            break;
    }

    assert(!dest.isEmpty());
    if (dest.isEmpty())
        return make_error_code(MVLCErrorCode::ShortRead);

    u32 header = dest[0];

    if (!bhv(header))
    {
        auto msg = QString("readResponse header validation failed, header=0x%1")
            .arg(header, 8, 16, QLatin1Char('0'));
        std::cerr << msg.toStdString() << std::endl;
        return make_error_code(MVLCErrorCode::InvalidBufferHeader);
    }

    return {};
}

std::error_code MVLCDialog::readRegister(u16 address, u32 &value)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addReadLocal(address);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    logBuffer(request, "read_register >>>");

    if (auto ec = doWrite(request))
        return ec;

    if (auto ec = readResponse(is_super_buffer, m_responseBuffer))
        return ec;

    logBuffer(m_responseBuffer, "read_register <<<");

    if (auto ec = check_mirror(request, m_responseBuffer))
        return ec;

    if (m_responseBuffer.size() < 4)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    value = m_responseBuffer[3];

    return {};
}

std::error_code MVLCDialog::readRegisterBlock(u16 address, u16 words,
                                              QVector<u32> &dest)
{
    if (words > ReadLocalBlockMaxWords)
        return make_error_code(MVLCErrorCode::CommandArgOutOfRange);

    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addReadLocalBlock(address, words);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    logBuffer(request, "read_local_block >>>");

    if (auto ec = doWrite(request))
        return ec;

    if (auto ec = readResponse(is_super_buffer, m_responseBuffer))
        return ec;

    logBuffer(m_responseBuffer, "read_local_block <<<");

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

std::error_code MVLCDialog::writeRegister(u16 address, u32 value)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addWriteLocal(address, value);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());
    logBuffer(request, "write_register >>>");

    if (auto ec = doWrite(request))
        return ec;

    if (auto ec = readResponse(is_super_buffer, m_responseBuffer))
        return ec;

    logBuffer(m_responseBuffer, "write_register <<<");

    if (auto ec = check_mirror(request, m_responseBuffer))
        return ec;

    if (m_responseBuffer.size() != 4)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    return {};
}

std::error_code MVLCDialog::mirrorTransaction(const QVector<u32> &cmdBuffer,
                                              QVector<u32> &dest)
{
    // upload the stack
    if (auto ec = doWrite(cmdBuffer))
        return ec;

    // read the mirror response
    if (auto ec = readResponse(is_super_buffer, dest))
        return ec;

    // verify the mirror response
    return check_mirror(cmdBuffer, dest);
}

std::error_code MVLCDialog::stackTransaction(const QVector<u32> &stack,
                                             QVector<u32> &dest)
{
    // upload, read mirror, verify mirror
    if (auto ec = mirrorTransaction(stack, dest))
        return ec;

    // set the stack offset register
    if (auto ec = writeRegister(stacks::Stack0OffsetRegister, 0))
        return ec;

    // exec the stack
    if (auto ec = writeRegister(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift))
        return ec;

    // read the stack response
    return readResponse(is_stack_buffer, dest);
}

std::error_code MVLCDialog::vmeSingleWrite(u32 address, u32 value, AddressMode amod,
                                           VMEDataWidth dataWidth)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMEWrite(address, value, amod, dataWidth);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());

    auto ec = stackTransaction(request, m_responseBuffer);

    logBuffer(m_responseBuffer, "vme_single_write response");

    if (ec)
        return ec;

    if (m_responseBuffer.size() == 2 && m_responseBuffer[1] == 0xFFFFFFFF)
        return make_error_code(MVLCErrorCode::NoVMEResponse);

    if (m_responseBuffer.size() != 1)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    return ec;
}

std::error_code MVLCDialog::vmeSingleRead(u32 address, u32 &value, AddressMode amod,
                                          VMEDataWidth dataWidth)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMERead(address, amod, dataWidth);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());

    auto ec = stackTransaction(request, m_responseBuffer);

    logBuffer(m_responseBuffer, "vme_single_read response");

    if (ec)
        return ec;

    if (m_responseBuffer.size() != 2)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    const u32 Mask = (dataWidth == VMEDataWidth::D16 ? 0x0000FFFF : 0xFFFFFFFF);

    value = m_responseBuffer[1] & Mask;

    return ec;
}

std::error_code MVLCDialog::vmeBlockRead(u32 address, AddressMode amod, u16 maxTransfers,
                                         QVector<u32> &dest)
{
    script::MVLCCommandListBuilder cmdList;
    cmdList.addReferenceWord(m_referenceWord++);
    cmdList.addVMEBlockRead(address, amod, maxTransfers);

    QVector<u32> request = to_mvlc_command_buffer(cmdList.getCommandList());

    auto ec = stackTransaction(request, dest);

    logBuffer(dest, "vme_block_read response");

    return ec;
}

void MVLCDialog::logBuffer(const QVector<u32> &buffer, const QString &info)
{
    log_buffer(std::cerr, buffer.data(), buffer.size(), info.toStdString().c_str());
}

} // end namespace mvlc
} // end namespace mesytec
