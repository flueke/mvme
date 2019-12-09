#include "mvlc/mvlc_qt_object.h"

#include <cassert>
#include <QDebug>
#include <QtConcurrent>

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

        if (newState == Connected)
        {
            auto guard = m_stackErrors.lock();
            m_stackErrors.counters = {};
        }

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
    // our own methods which do take the lock themselves.

    auto errorFrames = m_dialog.getStackErrorNotifications();

    if (errorFrames.isEmpty())
        return;

    auto lock = m_stackErrors.lock();

    for (const auto &errorFrame: errorFrames)
    {
        qDebug() << __PRETTY_FUNCTION__ << "updating stack error counters";
        update_stack_error_counters(m_stackErrors.counters, errorFrame);
    }
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
    auto tStart = QDateTime::currentDateTime();
    auto guard = getLocks().lockCmd();
    auto tLock = QDateTime::currentDateTime();
    preDialogOperation();
    auto result = m_dialog.vmeSingleWrite(address, value, amod, dataWidth);
    postDialogOperation();
    auto tEnd = QDateTime::currentDateTime();

#if 0
    qDebug() << __FUNCTION__
        << ", lock_ms =" << tStart.msecsTo(tLock)
        << ", comm_ms =" << tLock.msecsTo(tEnd)
        << ", total_ms =" << tStart.msecsTo(tEnd)
        ;
#endif
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

#if 0
std::error_code MVLCObject::readRegisterBlock(u16 address, u16 words,
                                              QVector<u32> &dest)
{
    auto guard = getLocks().lockCmd();
    preDialogOperation();
    auto result = m_dialog.readRegisterBlock(address, words, dest);
    postDialogOperation();
    return result;
}
#endif

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

#ifndef __WIN32
std::error_code MVLCObject::readKnownBuffer(QVector<u32> &dest, unsigned timeout_ms)
{
    auto tStart = QDateTime::currentDateTime();
    auto guard = getLocks().lockCmd();
    auto tLock = QDateTime::currentDateTime();
    auto result = m_dialog.readKnownBuffer(dest, timeout_ms);
    auto tEnd = QDateTime::currentDateTime();

#if 0
    qDebug() << __FUNCTION__
        << "timeout_ms =" << timeout_ms
        << ", lock_ms =" << tStart.msecsTo(tLock)
        << ", read_ms =" << tLock.msecsTo(tEnd)
        << ", total_ms =" << tStart.msecsTo(tEnd)
        ;
#endif
    return result;
}
#endif

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

QString MVLCObject::getConnectionInfo() const
{
    auto guards = getLocks().lockBoth();

    if (!m_impl->isConnected())
        return "not connected";

    return QString::fromStdString(m_impl->connectionInfo());
}

//
// MVLCNotificationPoller
//
MVLCNotificationPoller::MVLCNotificationPoller(MVLCObject &mvlc, QObject *parent)
    : QObject(parent)
    , m_mvlc(mvlc)
    , m_isPolling(false)
{
    connect(&m_pollTimer, &QTimer::timeout, this, [this] ()
    {
        //qDebug() << __PRETTY_FUNCTION__ << "pre poller via QtConcurrent::run" << QDateTime::currentDateTime();
        QtConcurrent::run(this, &MVLCNotificationPoller::doPoll);
        //qDebug() << __PRETTY_FUNCTION__ << "post poller via QtConcurrent::run" << QDateTime::currentDateTime();
    });
}

void MVLCNotificationPoller::enablePolling(int interval_ms)
{
    qDebug() << __PRETTY_FUNCTION__ << "interval =" << interval_ms;
    m_pollTimer.start(interval_ms);
}

#if (QT_VERSION >= QT_VERSION_CHECK(5, 8, 0))
void MVLCNotificationPoller::enablePolling(const std::chrono::milliseconds &interval)
{
    qDebug() << __PRETTY_FUNCTION__ << "interval =" << interval.count();
    m_pollTimer.start(interval);
}
#endif

void MVLCNotificationPoller::disablePolling()
{
    qDebug() << __PRETTY_FUNCTION__;
    m_pollTimer.stop();
}

void MVLCNotificationPoller::doPoll()
{
    // This avoids having multiple instances of this polling code run in
    // parallel.
    // Can only happen if either the poll interval is very short or the mvlc
    // read timeouts are longer than the poll timer interval or the read loop
    // always gets notification data back and thus spends more time in the loop
    // than the poll interval.
    bool f = false;
    if (!m_isPolling.compare_exchange_weak(f, true))
        return;

    //qDebug() << __FUNCTION__ << "entering polling loop" << QThread::currentThread();

    QVector<u32> buffer;
    size_t iterationCount = 0u;

    do
    {
        auto tStart = QDateTime::currentDateTime();
        //qDebug() << __FUNCTION__ << tStart << "  begin read";


#ifndef __WIN32
        auto ec = m_mvlc.readKnownBuffer(buffer, PollReadTimeout_ms);
#else
        auto ec = m_mvlc.readKnownBuffer(buffer);
#endif
        (void)ec;

        auto tEnd = QDateTime::currentDateTime();

        //qDebug() << __FUNCTION__ << tEnd << "  end read: "
        //    << ec.message().c_str()
        //    << ", duration:" << tStart.msecsTo(tEnd);

        if (!buffer.isEmpty())
        {
            auto &errorCounters = m_mvlc.getGuardedStackErrorCounters();
            auto lock = errorCounters.lock();
            update_stack_error_counters(errorCounters.counters, buffer);
        }

        if (++iterationCount >= SinglePollMaxIterations)
            break;

    } while (!buffer.isEmpty());

    qDebug() << __FUNCTION__ << "left polling loop after" << iterationCount << "iterations";

    m_isPolling = false;
}

} // end namespace mvlc
} // end namespace mesytec
