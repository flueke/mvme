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
    if (request.size()  < 1)
    {
        return make_error_code(MVLCProtocolError::MirrorEmptyRequest);
    }

    if (response.size() < 1)
    {
        return make_error_code(MVLCProtocolError::MirrorEmptyResponse);
    }

    if (response.size() < request.size() - 1)
    {
        return make_error_code(MVLCProtocolError::MirrorShortResponse);
    }

    int minIndex = 1; // skip buffer header
    int maxIndex = request.size() - 1;

    for (int i = minIndex; i < maxIndex; i++)
    {
        if (request[i] != response[i])
            return make_error_code(MVLCProtocolError::MirrorNotEqual);
    }

    return {};
}

MVLCDialog::MVLCDialog(MVLCObject *mvlc)
    : m_mvlc(mvlc)
{
    assert(m_mvlc);
}

std::error_code MVLCDialog::doWrite(const QVector<u32> &buffer)
{
    return m_mvlc->write(Pipe::Command, buffer).first;
};

std::error_code MVLCDialog::readResponse(BufferHeaderValidator bhv, QVector<u32> &dest)
{
    assert(bhv);

    dest.resize(0);

    u32 header = 0u;
    size_t bytesTransferred = 0u;

    auto ec = m_mvlc->read(Pipe::Command,
                           reinterpret_cast<u8 *>(&header), sizeof(header),
                           bytesTransferred);
    if (ec)
        return ec;

    if (bytesTransferred != sizeof(header))
        return make_error_code(MVLCProtocolError::ShortRead);

    if (!bhv(header))
        return make_error_code(MVLCProtocolError::InvalidBufferHeader);

    u16 responseLength = (header & BufferSizeMask);

    dest.resize(1 + responseLength);
    dest[0] = header;

    if (responseLength > 0)
    {
        size_t bytesToTransfer = responseLength * sizeof(u32);
        bytesTransferred = 0u;

        ec = m_mvlc->read(Pipe::Command,
                          reinterpret_cast<u8 *>(dest.data() + 1),
                          bytesToTransfer,
                          bytesTransferred);

        dest.resize(1 + bytesTransferred / sizeof(u32));

        if (ec)
            return ec;

        if (bytesTransferred != bytesToTransfer)
            return make_error_code(MVLCProtocolError::ShortRead);
    }

    return {};
}

std::error_code MVLCDialog::readRegister(u32 address, u32 &value)
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
        return make_error_code(MVLCProtocolError::UnexpectedResponseSize);

    value = m_responseBuffer[3];

    return {};
}

std::error_code MVLCDialog::writeRegister(u32 address, u32 value)
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
        return make_error_code(MVLCProtocolError::UnexpectedResponseSize);

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
        return make_error_code(MVLCProtocolError::NoVMEResponse);

    if (m_responseBuffer.size() != 1)
        return make_error_code(MVLCProtocolError::UnexpectedResponseSize);

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
        return make_error_code(MVLCProtocolError::UnexpectedResponseSize);

    value = m_responseBuffer[1];

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
