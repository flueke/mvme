/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#include "mvme_stream_worker.h"

#include "analysis/a2_adapter.h"
#include "analysis/analysis.h"
#include "analysis/analysis_impl_switch.h"
#include "histo1d.h"
#include "mesytec_diagnostics.h"
#include "mvme_context.h"
#include "mvme_listfile.h"
#include "timed_block.h"
#include "vme_analysis_common.h"

#include <atomic>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

using vme_analysis_common::TimetickGenerator;

namespace
{

enum InternalState
{
    KeepRunning,
    StopIfQueueEmpty,
    StopImmediately,
    Pause
};

static const u32 FilledBufferWaitTimeout_ms = 125;
static const u32 ProcessEventsMinInterval_ms = 500;
static const double PauseMaxSleep_ms = 125.0;

} // end anon namespace

struct MVMEStreamWorkerPrivate
{
    MVMEStreamProcessor streamProcessor;
    MVMEContext *context = nullptr;
    u32 m_listFileVersion = 1;

    std::atomic<InternalState> internalState;
    MVMEStreamWorkerState state = MVMEStreamWorkerState::Idle;

    RunInfo runInfo;

    ThreadSafeDataBufferQueue *freeBuffers,
                              *fullBuffers;

    inline void processNextBuffer();
};

void MVMEStreamWorkerPrivate::processNextBuffer()
{
    DataBuffer *buffer = nullptr;

    {
        QMutexLocker lock(&fullBuffers->mutex);

        if (fullBuffers->queue.isEmpty())
        {
            if (internalState == StopIfQueueEmpty)
            {
                internalState = StopImmediately;
                return;
            }

            fullBuffers->wc.wait(&fullBuffers->mutex, FilledBufferWaitTimeout_ms);
        }

        if (!fullBuffers->queue.isEmpty())
        {
            buffer = fullBuffers->queue.dequeue();
        }
    }
    // The mutex is unlocked again at this point

    if (buffer)
    {
        streamProcessor.processDataBuffer(buffer);

        // Put the buffer back into the free queue
        enqueue(freeBuffers, buffer);
    }
}

MVMEStreamWorker::MVMEStreamWorker(MVMEContext *context,
                                   ThreadSafeDataBufferQueue *freeBuffers,
                                   ThreadSafeDataBufferQueue *fullBuffers)
    : m_d(new MVMEStreamWorkerPrivate)
{
    m_d->internalState = KeepRunning;
    m_d->context = context;
    m_d->freeBuffers = freeBuffers;
    m_d->fullBuffers = fullBuffers;
}

MVMEStreamWorker::~MVMEStreamWorker()
{
    delete m_d;
}

MVMEStreamProcessor *MVMEStreamWorker::getStreamProcessor() const
{
    return &m_d->streamProcessor;
}

void MVMEStreamWorker::setState(MVMEStreamWorkerState newState)
{
    auto oldState = m_d->state;
    m_d->state = newState;

    qDebug() << __PRETTY_FUNCTION__ << static_cast<int>(oldState) << "->" << static_cast<int>(newState);

    emit stateChanged(newState);

    switch (newState)
    {
        case MVMEStreamWorkerState::Running:
            emit started();
            break;
        case MVMEStreamWorkerState::Idle:
            emit stopped();
            break;
        case MVMEStreamWorkerState::Paused:
            break;
    }

    QCoreApplication::processEvents();
}

void MVMEStreamWorker::beginRun(const RunInfo &runInfo, VMEConfig *vmeConfig)
{
    m_d->runInfo = runInfo;

    m_d->streamProcessor.beginRun(
        runInfo,
        m_d->context->getAnalysis(),
        m_d->context->getVMEConfig(),
        m_d->m_listFileVersion,
        [this](const QString &msg) { m_d->context->logMessage(msg); });
}

/* Used at the start of a run after beginRun() has been called and to resume from
 * paused state.
 * Does a2_begin_run() and a2_end_run() (threading stuff if enabled). */
void MVMEStreamWorker::startProcessing()
{
    qDebug() << __PRETTY_FUNCTION__ << "begin";
    Q_ASSERT(m_d->freeBuffers);
    Q_ASSERT(m_d->fullBuffers);
    Q_ASSERT(m_d->state == MVMEStreamWorkerState::Idle);
    Q_ASSERT(m_d->context->getAnalysis());

    auto analysis = m_d->context->getAnalysis();

    if (auto a2State = analysis->getA2AdapterState())
    {
        // Move this into Analysis::beginRun()?
        a2::a2_begin_run(a2State->a2);
    }

    auto &counters = m_d->streamProcessor.getCounters();
    counters.startTime = QDateTime::currentDateTime();
    counters.stopTime  = QDateTime();

    TimetickGenerator timetickGen;

    setState(MVMEStreamWorkerState::Running);

    m_d->internalState = KeepRunning;

    InternalState internalState = m_d->internalState;

    while (internalState != StopImmediately)
    {
        if (m_d->state == MVMEStreamWorkerState::Running)
        {
            switch (internalState)
            {
                case KeepRunning:
                case StopIfQueueEmpty:
                    m_d->processNextBuffer();
                    break;

                case Pause:
                    setState(MVMEStreamWorkerState::Paused);
                    break;

                case StopImmediately:
                    break;
            }
        }
        else if (m_d->state == MVMEStreamWorkerState::Paused)
        {
            switch (internalState)
            {
                case KeepRunning:
                case StopIfQueueEmpty:
                case StopImmediately:
                    setState(MVMEStreamWorkerState::Running);
                    break;

                case Pause:
                    QThread::msleep(std::min(PauseMaxSleep_ms, timetickGen.getTimeToNextTick()));
                    break;
            }
        }
        else
        {
            InvalidCodePath;
        }

        if (!m_d->runInfo.isReplay)
        {
            int elapsedSeconds = timetickGen.generateElapsedSeconds();

            while (elapsedSeconds >= 1)
            {
                m_d->streamProcessor.processExternalTimetick();
                elapsedSeconds--;
            }
        }

        internalState = m_d->internalState;
    }

    counters.stopTime = QDateTime::currentDateTime();

    if (auto a2State = analysis->getA2AdapterState())
    {
        a2::a2_end_run(a2State->a2);
    }

    m_d->streamProcessor.endRun();

    setState(MVMEStreamWorkerState::Idle);

    qDebug() << __PRETTY_FUNCTION__ << "end";
}

void MVMEStreamWorker::stopProcessing(bool whenQueueEmpty)
{
    qDebug() << QDateTime::currentDateTime().toString("HH:mm:ss")
        << __PRETTY_FUNCTION__ << (whenQueueEmpty ? "when empty" : "immediately");

    m_d->internalState = whenQueueEmpty ? StopIfQueueEmpty : StopImmediately;
}

void MVMEStreamWorker::pause()
{
    m_d->internalState = InternalState::Pause;
    qDebug() << __PRETTY_FUNCTION__;
}

void MVMEStreamWorker::resume()
{
    qDebug() << __PRETTY_FUNCTION__;
    m_d->internalState = InternalState::KeepRunning;
}

MVMEStreamWorkerState MVMEStreamWorker::getState() const
{
    return m_d->state;
}

const MVMEStreamProcessorCounters &MVMEStreamWorker::getCounters() const
{
    return m_d->streamProcessor.getCounters();
}

void MVMEStreamWorker::setListFileVersion(u32 version)
{
    qDebug() << __PRETTY_FUNCTION__ << version;

    m_d->m_listFileVersion = version;
}

void MVMEStreamWorker::setDiagnostics(std::shared_ptr<MesytecDiagnostics> diag)
{
    qDebug() << __PRETTY_FUNCTION__ << diag.get();
    m_d->streamProcessor.attachDiagnostics(diag);
}

bool MVMEStreamWorker::hasDiagnostics() const
{
    return m_d->streamProcessor.hasDiagnostics();
}

void MVMEStreamWorker::removeDiagnostics()
{
    m_d->streamProcessor.removeDiagnostics();
}
