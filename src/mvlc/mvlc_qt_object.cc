#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_impl_abstract.h"
#include <cassert>
#include <QDebug>

namespace mesytec
{
namespace mvlc
{

//
// MVLCObject
//
MVLCObject::MVLCObject(std::unique_ptr<AbstractImpl> mvlc, QObject *parent)
    : QObject(parent)
    , m_impl(std::move(mvlc))
    , m_dialog(m_impl.get())
    , m_state(Disconnected)
{
    assert(m_impl);

    if (m_impl->isConnected())
        setState(Connected);
}

MVLCObject::~MVLCObject()
{
    auto guards = getLocks().lockBoth();
    disconnect();
}

bool MVLCObject::isConnected() const
{
    auto guards = getLocks().lockBoth();
    return m_impl->isConnected();
}

std::error_code MVLCObject::connect()
{
    if (isConnected())
        return make_error_code(MVLCErrorCode::IsConnected);

    auto guards = getLocks().lockBoth();
    setState(Connecting);
    auto ec = m_impl->connect();

    setState(ec ? Disconnected : Connected);

    return ec;
}

std::error_code MVLCObject::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    auto guards = getLocks().lockBoth();
    auto ec = m_impl->disconnect();
    setState(Disconnected);
    return ec;
}

void MVLCObject::setState(const State &newState)
{
    if (m_state != newState)
    {
        auto prevState = m_state;
        m_state = newState;
        emit stateChanged(prevState, newState);
    }
};

std::error_code MVLCObject::read(Pipe pipe, u8 *buffer, size_t size,
                                 size_t &bytesTransferred)
{
    auto guard = getLocks().lock(pipe);
    auto ec = m_impl->read(pipe, buffer, size, bytesTransferred);
    return ec;
}

std::error_code MVLCObject::write(Pipe pipe, const u8 *buffer, size_t size,
                                  size_t &bytesTransferred)
{
    auto guard = getLocks().lock(pipe);
    auto ec = m_impl->write(pipe, buffer, size, bytesTransferred);
    return ec;
}

#if 0
std::pair<std::error_code, size_t> MVLCObject::write(Pipe pipe, const QVector<u32> &buffer)
{
    size_t bytesTransferred = 0u;
    const size_t bytesToTransfer = buffer.size() * sizeof(u32);
    auto ec = write(pipe, reinterpret_cast<const u8 *>(buffer.data()),
                    bytesToTransfer, bytesTransferred);

    if (!ec && bytesToTransfer != bytesTransferred)
    {
        return std::make_pair(
            make_error_code(MVLCErrorCode::ShortWrite),
            bytesTransferred);
    }

    return std::make_pair(ec, bytesTransferred);
}
#endif

AbstractImpl *MVLCObject::getImpl()
{
    return m_impl.get();
}

void MVLCObject::setReadTimeout(Pipe pipe, unsigned ms)
{
    auto guard = getLocks().lock(pipe);
    m_impl->setReadTimeout(pipe, ms);
}

void MVLCObject::setWriteTimeout(Pipe pipe, unsigned ms)
{
    auto guard = getLocks().lock(pipe);
    m_impl->setWriteTimeout(pipe, ms);
}

unsigned MVLCObject::getReadTimeout(Pipe pipe) const
{
    auto guard = getLocks().lock(pipe);
    return m_impl->getReadTimeout(pipe);
}

unsigned MVLCObject::getWriteTimeout(Pipe pipe) const
{
    auto guard = getLocks().lock(pipe);
    return m_impl->getWriteTimeout(pipe);
}

std::error_code MVLCObject::vmeSingleRead(u32 address, u32 &value, AddressMode amod,
                              VMEDataWidth dataWidth)
{
    auto guard = getLocks().lockCmd();
    return m_dialog.vmeSingleRead(address, value, amod, dataWidth);
}

std::error_code MVLCObject::vmeSingleWrite(u32 address, u32 value, AddressMode amod,
                               VMEDataWidth dataWidth)
{
    auto guard = getLocks().lockCmd();
    return m_dialog.vmeSingleWrite(address, value, amod, dataWidth);
}

std::error_code MVLCObject::vmeBlockRead(u32 address, AddressMode amod, u16 maxTransfers,
                             QVector<u32> &dest)
{
    auto guard = getLocks().lockCmd();
    return m_dialog.vmeBlockRead(address, amod, maxTransfers, dest);
}

std::error_code MVLCObject::readRegister(u32 address, u32 &value)
{
    auto guard = getLocks().lockCmd();
    return m_dialog.readRegister(address, value);
}

std::error_code MVLCObject::writeRegister(u32 address, u32 value)
{
    auto guard = getLocks().lockCmd();
    return m_dialog.writeRegister(address, value);
}

std::error_code MVLCObject::readResponse(BufferHeaderValidator bhv, QVector<u32> &dest)
{
    auto guard = getLocks().lockCmd();
    return m_dialog.readResponse(bhv, dest);
}

std::error_code MVLCObject::mirrorTransaction(const QVector<u32> &cmdBuffer,
                                  QVector<u32> &responseDest)
{
    auto guard = getLocks().lockCmd();
    return m_dialog.mirrorTransaction(cmdBuffer, responseDest);
}

std::error_code MVLCObject::stackTransaction(const QVector<u32> &stackUploadData,
                                 QVector<u32> &responseDest)
{
    auto guard = getLocks().lockCmd();
    return m_dialog.stackTransaction(stackUploadData, responseDest);
}

QVector<u32> MVLCObject::getResponseBuffer() const
{
    auto guard = getLocks().lockCmd();
    return m_dialog.getResponseBuffer();
}

QVector<QVector<u32>> MVLCObject::getStackErrorNotifications() const
{
    auto guard = getLocks().lockCmd();
    return m_dialog.getStackErrorNotifications();
}

void MVLCObject::clearStackErrorNotifications()
{
    auto guard = getLocks().lockCmd();
    m_dialog.clearStackErrorNotifications();
}

QVector<QVector<u32>> MVLCObject::getAndClearStackErrorNotifications()
{
    auto guard = getLocks().lockCmd();
    // Implementation used MVLCDialog directly to avoid deadlocking.
    auto result = m_dialog.getStackErrorNotifications();
    m_dialog.clearStackErrorNotifications();
    return result;
}

} // end namespace mvlc
} // end namespace mesytec
