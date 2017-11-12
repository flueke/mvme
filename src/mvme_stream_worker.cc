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

#include <atomic>
#include <QCoreApplication>
#include <QElapsedTimer>

enum RunAction
{
    KeepRunning,
    StopIfQueueEmpty,
    StopImmediately
};

static const u32 FilledBufferWaitTimeout_ms = 250;
static const u32 ProcessEventsMinInterval_ms = 500;

struct MVMEStreamWorkerPrivate
{
    MVMEStreamProcessor streamProcessor;
    MVMEContext *context = nullptr;
    u32 m_listFileVersion = 1;

    std::atomic<RunAction> m_runAction;
    EventProcessorState m_state = EventProcessorState::Idle;
};

MVMEStreamWorker::MVMEStreamWorker(MVMEContext *context)
    : m_d(new MVMEStreamWorkerPrivate)
{
    m_d->m_runAction = KeepRunning;
    m_d->context = context;
}

MVMEStreamWorker::~MVMEStreamWorker()
{
    //delete m_d->diag; // FIXME: why? what?
    delete m_d;
}

void MVMEStreamWorker::beginRun(const RunInfo &runInfo, VMEConfig *vmeConfig)
{
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
    Q_ASSERT(m_freeBuffers);
    Q_ASSERT(m_fullBuffers);
    Q_ASSERT(m_d->m_state == EventProcessorState::Idle);

    auto &counters = m_d->streamProcessor.getCounters();
    counters.startTime = QDateTime::currentDateTime();
    counters.stopTime  = QDateTime();

    emit started();
    emit stateChanged(m_d->m_state = EventProcessorState::Running);

    QCoreApplication::processEvents();

    QElapsedTimer timeSinceLastProcessEvents;
    timeSinceLastProcessEvents.start();

    m_d->m_runAction = KeepRunning;

    auto analysis = m_d->context->getAnalysis();

    if (analysis)
    {
        if (auto a2State = analysis->getA2AdapterState())
        {
            // This is here instead of in Analysis::beginRun() because the
            // latter is called way too much from everywhere and I don't want
            // to rebuild the a2 system all the time.
            a2::a2_begin_run(a2State->a2);
        }
    }

    while (m_d->m_runAction != StopImmediately)
    {
        DataBuffer *buffer = nullptr;

        {
            QMutexLocker lock(&m_fullBuffers->mutex);

            if (m_fullBuffers->queue.isEmpty())
            {
                if (m_d->m_runAction == StopIfQueueEmpty)
                    break;

                m_fullBuffers->wc.wait(&m_fullBuffers->mutex, FilledBufferWaitTimeout_ms);
            }

            if (!m_fullBuffers->queue.isEmpty())
            {
                buffer = m_fullBuffers->queue.dequeue();
            }
        }
        // The mutex is unlocked again at this point

        if (buffer)
        {
            m_d->streamProcessor.processDataBuffer(buffer);

            // Put the buffer back into the free queue
            enqueue(m_freeBuffers, buffer);
        }

        // Process Qt events to be able to "receive" queued calls to our slots.
        if (timeSinceLastProcessEvents.elapsed() > ProcessEventsMinInterval_ms)
        {
            QCoreApplication::processEvents();
            timeSinceLastProcessEvents.restart();
        }
    }

    counters.stopTime = QDateTime::currentDateTime();

    if (analysis)
    {
        if (auto a2State = analysis->getA2AdapterState())
        {
            a2::a2_end_run(a2State->a2);
        }
    }

    emit stopped();
    emit stateChanged(m_d->m_state = EventProcessorState::Idle);

    qDebug() << __PRETTY_FUNCTION__ << "end";
}

void MVMEStreamWorker::stopProcessing(bool whenQueueEmpty)
{
    qDebug() << QDateTime::currentDateTime().toString("HH:mm:ss")
        << __PRETTY_FUNCTION__ << (whenQueueEmpty ? "when empty" : "immediately");

    m_d->m_runAction = whenQueueEmpty ? StopIfQueueEmpty : StopImmediately;
}

EventProcessorState MVMEStreamWorker::getState() const
{
    return m_d->m_state;
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
