#include "mvlc/mvlc_qt_object.h"

#include <cassert>
#include <QDebug>

#include "mvlc/mvlc_error.h"

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
    disconnect();
}

AbstractImpl *MVLCObject::getImpl()
{
    return m_impl.get();
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

std::error_code MVLCObject::write(Pipe pipe, const u8 *buffer, size_t size,
                                  size_t &bytesTransferred)
{
    auto guard = getLocks().lock(pipe);
    auto ec = m_impl->write(pipe, buffer, size, bytesTransferred);
    return ec;
}

std::error_code MVLCObject::read(Pipe pipe, u8 *buffer, size_t size,
                                 size_t &bytesTransferred)
{
    auto guard = getLocks().lock(pipe);
    auto ec = m_impl->read(pipe, buffer, size, bytesTransferred);
    return ec;
}

std::error_code MVLCObject::getReadQueueSize(Pipe pipe, u32 &dest)
{
    auto guard = getLocks().lock(pipe);
    return m_impl->getReadQueueSize(pipe, dest);
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

// Called before stack operations. Clears the internal stack error notification
// buffer.
// The command mutex must be locked when this method is called.
void MVLCObject::preDialogOperation()
{
    m_dialog.clearStackErrorNotifications();
}

// Called after stack operations. Checks if there are pending stack error
// notifications and emits the stackErrorNotification() signal for each of
// them.
// The command mutex must be locked when this method is called.
void MVLCObject::postDialogOperation()
{
    // The Command mutex should be locked at this point which means to avoid
    // deadlock we have to call the dialog methods directly instead of using
    // our own methods which do take the lock.

    for (const auto &n: m_dialog.getStackErrorNotifications())
            emit stackErrorNotification(n);
}

std::error_code MVLCObject::vmeSingleRead(u32 address, u32 &value, u8 amod,
                                          VMEDataWidth dataWidth)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.vmeSingleRead(address, value, amod, dataWidth);
    postDialogOperation();
    return result;
}

std::error_code MVLCObject::vmeSingleWrite(u32 address, u32 value, u8 amod,
                                           VMEDataWidth dataWidth)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.vmeSingleWrite(address, value, amod, dataWidth);
    postDialogOperation();
    return result;
}

std::error_code MVLCObject::vmeBlockRead(u32 address, u8 amod, u16 maxTransfers,
                                         QVector<u32> &dest)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.vmeBlockRead(address, amod, maxTransfers, dest);
    postDialogOperation();
    return result;
}

std::error_code MVLCObject::readRegister(u16 address, u32 &value)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.readRegister(address, value);
    postDialogOperation();
    return result;
}

std::error_code MVLCObject::readRegisterBlock(u16 address, u16 words,
                                              QVector<u32> &dest)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.readRegisterBlock(address, words, dest);
    postDialogOperation();
    return result;
}

std::error_code MVLCObject::writeRegister(u16 address, u32 value)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.writeRegister(address, value);
    postDialogOperation();
    return result;
}

std::error_code MVLCObject::readResponse(BufferHeaderValidator bhv, QVector<u32> &dest)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.readResponse(bhv, dest);
    postDialogOperation();
    return result;
}

std::error_code MVLCObject::mirrorTransaction(const QVector<u32> &cmdBuffer,
                                              QVector<u32> &responseDest)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.mirrorTransaction(cmdBuffer, responseDest);
    postDialogOperation();
    return result;
}

std::error_code MVLCObject::stackTransaction(const QVector<u32> &stackUploadData,
                                             QVector<u32> &responseDest)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.stackTransaction(stackUploadData, responseDest);
    postDialogOperation();
    return result;
}

std::error_code MVLCObject::readKnownBuffer(QVector<u32> &dest)
{
    auto guard = getLocks().lockCmd();
    return m_dialog.readKnownBuffer(dest);
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

MVLCNotificationPoller::MVLCNotificationPoller(MVLCObject &mvlc, QObject *parent)
    : QObject(parent)
    , m_mvlc(mvlc)
{
    connect(&m_pollTimer, &QTimer::timeout, this, [this] ()
    {
        if (m_mvlc.isConnected())
        {
            qDebug() << "polling for stack error notifications";
            QVector<u32> buffer;

            do
            {
                static const unsigned PollReadTimeout_ms = 1;
                unsigned timeout = m_mvlc.getReadTimeout(Pipe::Command);
                m_mvlc.setReadTimeout(Pipe::Command, PollReadTimeout_ms);
                m_mvlc.readKnownBuffer(buffer);
                m_mvlc.setReadTimeout(Pipe::Command, timeout);

                if (!buffer.isEmpty())
                {
                    emit stackErrorNotification(buffer);
                }
            } while (!buffer.isEmpty());
        }
    });
}

void MVLCNotificationPoller::enablePolling(int interval_ms)
{
    m_pollTimer.start(interval_ms);
}

void MVLCNotificationPoller::enablePolling(const std::chrono::milliseconds &interval)
{
    m_pollTimer.start(interval);
}

void MVLCNotificationPoller::disablePolling()
{
    m_pollTimer.stop();
}

} // end namespace mvlc
} // end namespace mesytec
