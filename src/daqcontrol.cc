/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "daqcontrol.h"
#include <QTimer>

DAQControl::DAQControl(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_context(context)
{
    connect(m_context, &MVMEContext::daqStateChanged,
            this, &DAQControl::daqStateChanged);
}

DAQControl::~DAQControl()
{
}

DAQState DAQControl::getDAQState() const
{
    return m_context->getDAQState();
}

void DAQControl::startDAQ(
    u32 nCycles, bool keepHistoContents,
    const std::chrono::milliseconds &runDuration)
{
    if (m_context->getDAQState() != DAQState::Idle)
        return;

    if (m_context->getMVMEStreamWorkerState() != AnalysisWorkerState::Idle)
        return;

    if (m_context->getMode() == GlobalMode::DAQ)
    {
        if (runDuration != std::chrono::milliseconds::zero())
            m_timedRunControl = std::make_unique<TimedRunControl>(
                this, runDuration);
        else
            m_timedRunControl = {};

        m_context->startDAQReadout(nCycles, keepHistoContents);
    }
    else if (m_context->getMode() == GlobalMode::ListFile)
    {
        if (runDuration != std::chrono::milliseconds::zero())
            qWarning("DAQControl::startDAQ(): runDuration ignored for replays");

        m_timedRunControl = {};

        m_context->startDAQReplay(nCycles, keepHistoContents);
    }
}

void DAQControl::stopDAQ()
{
    m_context->stopDAQ();
}

void DAQControl::pauseDAQ()
{
    m_context->pauseDAQ();
}

void DAQControl::resumeDAQ(u32 nCycles)
{
    m_context->resumeDAQ(nCycles);
}

TimedRunControl::TimedRunControl(
    DAQControl *ctrl,
    const std::chrono::milliseconds &runDuration,
    QObject *parent)
: QObject(parent)
, m_ctrl(ctrl)
, m_shouldStop(false)
{
    assert(ctrl);
    assert(ctrl->getDAQState() == DAQState::Idle);

    if (ctrl->getDAQState() != DAQState::Idle)
        return;

    connect(ctrl, &DAQControl::daqStateChanged,
            this, &TimedRunControl::onDAQStateChanged);

    connect(&m_timer, &QTimer::timeout,
            this, &TimedRunControl::onTimerTimeout);

    m_timer.setInterval(runDuration);
    m_timer.setSingleShot(true);
}

void TimedRunControl::onDAQStateChanged(const DAQState &newState)
{
    switch (newState)
    {
        case DAQState::Running:
            assert(!m_timer.isActive());
            m_shouldStop = true;
            m_timer.start();
            qDebug() << __PRETTY_FUNCTION__ << "Running: started timer";
            break;

        case DAQState::Stopping:
            qDebug() << __PRETTY_FUNCTION__ << "Stopping: stopping timer";
            m_shouldStop = false;
            m_timer.stop();
            break;

        case DAQState::Idle:
        case DAQState::Starting:
        case DAQState::Paused:
            break;
    }
}

void TimedRunControl::onTimerTimeout()
{
    qDebug() << __PRETTY_FUNCTION__ << "shouldStop =" << m_shouldStop;
    if (m_shouldStop)
        m_ctrl->stopDAQ();
    m_shouldStop = false;
}
