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
#include "mvlc/mvlc_qt_object.h"

#include <cassert>
#include <QDebug>
#include <QtConcurrent>

#include "mesytec-mvlc/mvlc_basic_interface.h"

namespace mesytec
{
namespace mvme_mvlc
{

MVLCObject::MVLCObject(mvlc::MVLC mvlc, QObject *parent)
    : QObject(parent)
    , m_mvlc(mvlc)
    , m_state(Disconnected)
{
    qDebug() << __PRETTY_FUNCTION__ << this << this->getConnectionInfo();

    if (m_mvlc.isConnected())
        setState(Connected);
}

MVLCObject::~MVLCObject()
{
    qDebug() << __PRETTY_FUNCTION__ << this << this->getConnectionInfo();
}

void MVLCObject::setState(const State &newState)
{
    if (m_state != newState)
    {
        auto prevState = m_state;
        m_state = newState;

        if (newState == Connected)
            clearStackErrorNotifications();

        emit stateChanged(prevState, newState);
    }
};

#if 0
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

    //qDebug() << __FUNCTION__ << "entering notification polling loop" << QThread::currentThread();

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

    //qDebug() << __FUNCTION__ << "left polling loop after" << iterationCount << "iterations";

    m_isPolling = false;
}
#endif

} // end namespace mvme_mvlc
} // end namespace mesytec
