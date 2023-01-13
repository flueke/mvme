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
#include "mvme_stream_worker.h"

#include "analysis/a2_adapter.h"
#include "analysis/analysis.h"
#include "analysis/analysis_session.h"
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
    Pause,
    SingleStep,
    PausedAfterSingleStep
};

static const QMap<InternalState, QString> InternalState_StringTable =
{
    { KeepRunning,                      QSL("InternalState::KeepRunning") },
    { StopIfQueueEmpty,                 QSL("InternalState::StopIfQueueEmpty") },
    { StopImmediately,                  QSL("InternalState::StopImmediately") },
    { Pause,                            QSL("InternalState::Pause") },
    { SingleStep,                       QSL("InternalState::SingleStep") },
    { PausedAfterSingleStep,            QSL("InternalState::PausedAfterSingleStep") },
};

static const u32 FilledBufferWaitTimeout_ms = 125;
static const u32 ProcessEventsMinInterval_ms = 500;
static const double PauseMaxSleep_ms = 125.0;

} // end anon namespace

struct MVMEStreamWorkerPrivate
{
    MVMEStreamProcessor streamProcessor;
    MVMEContext *context = nullptr;
    u32 m_listFileVersion = CurrentListfileVersion;
    bool m_startPaused = false;

    std::atomic<InternalState> internalState;
    AnalysisWorkerState state = AnalysisWorkerState::Idle;

    RunInfo runInfo;

    ThreadSafeDataBufferQueue *freeBuffers,
                              *fullBuffers;

    u64 nextBufferNumber = 0;
    inline DataBuffer *dequeueNextBuffer();
};

DataBuffer *MVMEStreamWorkerPrivate::dequeueNextBuffer()
{
    DataBuffer *buffer = nullptr;

    {
        QMutexLocker lock(&fullBuffers->mutex);

        if (fullBuffers->queue.isEmpty())
        {
            if (internalState == StopIfQueueEmpty)
            {
                //internalState = StopImmediately;
                return buffer;
            }

            fullBuffers->wc.wait(&fullBuffers->mutex, FilledBufferWaitTimeout_ms);
        }

        if (!fullBuffers->queue.isEmpty())
        {
            buffer = fullBuffers->queue.dequeue();
        }
    }

    // Set increasing buffer number for MVMELST buffers only. MVLC buffers have
    // a buffer  number assigned by the readout side.
    if (buffer && buffer->tag == static_cast<int>(ListfileBufferFormat::MVMELST))
    {
        buffer->id = this->nextBufferNumber++;
    }

    return buffer;
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

void MVMEStreamWorker::setState(AnalysisWorkerState newState)
{
    auto oldState = m_d->state;
    m_d->state = newState;

    qDebug() << __PRETTY_FUNCTION__
        << to_string(oldState) << "(" << static_cast<int>(oldState) << ") ->"
        << to_string(newState) << "(" << static_cast<int>(newState) << ")";

    emit stateChanged(newState);

    switch (newState)
    {
        case AnalysisWorkerState::Running:
            emit started();
            break;
        case AnalysisWorkerState::Idle:
            emit stopped();
            break;
        case AnalysisWorkerState::Paused:
        case AnalysisWorkerState::SingleStepping:
            break;
    }

    // for signals to cross thread boundaries
    QCoreApplication::processEvents();
}

void MVMEStreamWorker::logMessage(const QString &msg)
{
    m_d->context->logMessage(msg);
}

namespace
{

using ProcessingState = MVMEStreamProcessor::ProcessingState;

[[maybe_unused]] void debug_dump(const ProcessingState &procState)
{
    Q_ASSERT(procState.buffer);

    qDebug() << ">>> begin ProcessingState";

    qDebug("  buffer=%p, buffer.id=%u, buffer.data=%p, buffer.used=%lu bytes, %lu words",
           procState.buffer,
           procState.buffer->id,
           procState.buffer->data,
           procState.buffer->used,
           procState.buffer->used / sizeof(u32)
          );

    u32 lastSectionHeader = *procState.buffer->indexU32(procState.lastSectionHeaderOffset);

    qDebug("  lastSectionHeader=0x%08x, lastSectionHeaderOffset=%d",
           lastSectionHeader, procState.lastSectionHeaderOffset);

    for (s32 moduleIndex = 0;
         moduleIndex < MaxVMEModules;
         moduleIndex++)
    {
        if (procState.lastModuleDataSectionHeaderOffsets[moduleIndex] >= 0
            || procState.lastModuleDataBeginOffsets[moduleIndex] >= 0
            || procState.lastModuleDataEndOffsets[moduleIndex] >= 0)
        {
            qDebug("  moduleIndex=%d, dataSectionHeaderOffset=%d, moduleDataBeginOffset=%d, moduleDataEndOffset=%d => dataSize=%d",
                   moduleIndex,
                   procState.lastModuleDataSectionHeaderOffsets[moduleIndex],
                   procState.lastModuleDataBeginOffsets[moduleIndex],
                   procState.lastModuleDataEndOffsets[moduleIndex],
                   procState.lastModuleDataEndOffsets[moduleIndex] - procState.lastModuleDataBeginOffsets[moduleIndex]
                  );

            //qDebug("  moduleIndex=%d, dataSectionHeader=0x%08x, moduleDataBegin=0x%08x, moduleDataEnd=0x%08x"
        }
    }

    qDebug() << ">>> end ProcessingState";
}

static const QMap<ProcessingState::StepResult, QString> StepResult_StringTable =
{
    { ProcessingState::StepResult_Unset,            QSL("Unspecified") },
    { ProcessingState::StepResult_EventHasMore,     QSL("MultiEvent") },
    { ProcessingState::StepResult_EventComplete,    QSL("EventComplete") },
    { ProcessingState::StepResult_AtEnd,            QSL("BufferCompleted") },
    { ProcessingState::StepResult_Error,            QSL("ProcessingError") },
};

QTextStream &
log_processing_step(QTextStream &out, const ProcessingState &procState,
                    const vats::MVMETemplates &vatsTemplates,
                    const ListfileConstants &lfc)
{
    Q_ASSERT(procState.buffer);

    out << "buffer #" << procState.buffer->id
        << ", size=" << (procState.buffer->used / sizeof(u32)) << " words"
        << ", step result: " << StepResult_StringTable[procState.stepResult]
        << endl;

    try
    {
        if (procState.stepResult == ProcessingState::StepResult_EventHasMore
            || procState.stepResult == ProcessingState::StepResult_EventComplete)
        {
            u32 eventSectionHeader = *procState.buffer->indexU32(procState.lastSectionHeaderOffset);
            u32 eventIndex         = (eventSectionHeader & lfc.EventIndexMask) >> lfc.EventIndexShift;
            u32 eventSectionSize   = (eventSectionHeader & lfc.SectionSizeMask) >> lfc.SectionSizeShift;

            out << "  "
                << (QString("eventHeader=0x%1, @offset %2, idx=%3, sz=%4 words")
                    .arg(eventSectionHeader, 8, 16, QLatin1Char('0'))
                    .arg(procState.lastSectionHeaderOffset)
                    .arg(eventIndex)
                    .arg(eventSectionSize)
                   )
                << endl;

            bool endlFlag = false;

            for (s32 moduleIndex = 0;
                 moduleIndex < MaxVMEModules;
                 moduleIndex++)
            {
                if (procState.lastModuleDataSectionHeaderOffsets[moduleIndex] >= 0
                    && procState.lastModuleDataBeginOffsets[moduleIndex] >= 0
                    && procState.lastModuleDataEndOffsets[moduleIndex] >= 0)
                {
                    u32 moduleSectionHeader = *procState.buffer->indexU32(
                        procState.lastModuleDataSectionHeaderOffsets[moduleIndex]);

                    u32 *moduleDataPtr = procState.buffer->indexU32(
                        procState.lastModuleDataBeginOffsets[moduleIndex]);

                    const u32 *moduleDataEndPtr = procState.buffer->indexU32(
                        procState.lastModuleDataEndOffsets[moduleIndex]);


                    u32 moduleSectionSize = (moduleSectionHeader & lfc.ModuleDataSizeMask) >> lfc.ModuleDataSizeShift;
                    u32 moduleType = (moduleSectionHeader & lfc.ModuleTypeMask) >> lfc.ModuleTypeShift;
                    QString moduleTypeString = vats::get_module_meta_by_typeId(vatsTemplates, moduleType).typeName;

                    if (endlFlag) out << endl;

                    out << "    "
                        << (QString("moduleHeader=0x%1, @offset %2, idx=%3, sz=%4 words, type=%5")
                            .arg(moduleSectionHeader, 8, 16, QLatin1Char('0'))
                            .arg(procState.lastModuleDataSectionHeaderOffsets[moduleIndex])
                            .arg(moduleIndex)
                            .arg(moduleSectionSize)
                            .arg(moduleTypeString)
                           )
                        << endl;

                    size_t dataSize_words = moduleDataEndPtr - moduleDataPtr;
                    size_t dataSize_bytes = dataSize_words * sizeof(u32);

                    if (procState.stepResult == ProcessingState::StepResult_EventHasMore)
                    {
                        // the multievent case (except for the last part which I can't distinguish for now)

                        out << "    "
                            << (QString("multievent: begin@=%1, end@=%2, sz=%3")
                                .arg(procState.lastModuleDataBeginOffsets[moduleIndex])
                                .arg(procState.lastModuleDataEndOffsets[moduleIndex])
                                .arg(dataSize_words)
                               )
                            << endl;
                    }

                    BufferIterator moduleDataIter(reinterpret_cast<u8 *>(moduleDataPtr), dataSize_bytes);
                    logBuffer(moduleDataIter, [&out](const QString &str) { out << "      " << str << endl; });

                    endlFlag = true;
                }
            }
        }
    }
    catch (const end_of_buffer &)
    {
        out << "!!! Error formatting last processing step in buffer #" << procState.buffer->id
            << ": unexpectedly reached end of buffer. This is a bug!"
            << endl;
    }

    return out;
}

void single_step_one_event(ProcessingState &procState, MVMEStreamProcessor &streamProc)
{
    for (bool done = false; !done;)
    {
        streamProc.singleStepNextStep(procState);

        switch (procState.stepResult)
        {
            case ProcessingState::StepResult_EventHasMore:
            case ProcessingState::StepResult_EventComplete:
            case ProcessingState::StepResult_AtEnd:
            case ProcessingState::StepResult_Error:
                done = true;
                break;

            case ProcessingState::StepResult_Unset:
                break;
        }
    }

#ifndef NDEBUG
    if (procState.stepResult == ProcessingState::StepResult_EventHasMore
        || procState.stepResult == ProcessingState::StepResult_EventComplete)
    {
        debug_dump(procState);
    }
#endif
}

} // end anon namespace

void MVMEStreamWorker::startupConsumers()
{
    m_d->streamProcessor.startup();
}

void MVMEStreamWorker::shutdownConsumers()
{
    m_d->streamProcessor.shutdown();
}

/* The main worker loop. */
void MVMEStreamWorker::start()
{
    qDebug() << __PRETTY_FUNCTION__ << "begin";

    Q_ASSERT(m_d->freeBuffers);
    Q_ASSERT(m_d->fullBuffers);
    Q_ASSERT(m_d->state == AnalysisWorkerState::Idle);
    Q_ASSERT(m_d->context->getAnalysis());

    m_d->runInfo = m_d->context->getRunInfo();

    m_d->streamProcessor.beginRun(
        m_d->runInfo,
        m_d->context->getAnalysis(),
        m_d->context->getVMEConfig(),
        m_d->m_listFileVersion,
        [this](const QString &msg) { m_d->context->logMessage(msg); });

    m_d->nextBufferNumber = 0;

    using ProcessingState = MVMEStreamProcessor::ProcessingState;

    // Single stepping support (the templates are used for logging output)
    MVMEStreamProcessor::ProcessingState singleStepProcState;
    auto vatsTemplates = vats::read_templates();

    const auto &lfc = listfile_constants(m_d->m_listFileVersion);

    auto &counters = m_d->streamProcessor.getCounters();
    counters.startTime = QDateTime::currentDateTime();
    counters.stopTime  = QDateTime();

    TimetickGenerator timetickGen;

    /* Fixed in MVMEContext::startDAQReplay:
     * There is a potential  race condition here that leads to being stuck in
     * the loop below. If the replay is very short and the listfile reader is
     * finished before we reach this line here then stop(IfQueueEmpty) may
     * already have been called. Thus internalState will be StopIfQueueEmpty
     * and we will overwrite it below with either Pause or KeepRunning.  As the
     * listfile reader already sent its finished signal - which makes the
     * context call our stop() method - we won't get any more calls to stop().
     * A way to fix this is to wait for the stream processor to enter its loop
     * and only then start the listfile reader. This fix has been implemented
     * in MVMEContext.
     */

    // Start out in running state unless pause mode was requested.
    m_d->internalState = m_d->m_startPaused ? Pause : KeepRunning;
    InternalState internalState = m_d->internalState;

    /* This emits started(). I've deliberately placed this after
     * m_d->internalState has been copied to avoid race conditions. */
    setState(AnalysisWorkerState::Running);

    while (internalState != StopImmediately)
    {
        if (m_d->state == AnalysisWorkerState::Running)
        {
            switch (internalState)
            {
                case KeepRunning:
                case StopIfQueueEmpty:
                    // keep running and process full buffers
                    if (auto buffer = m_d->dequeueNextBuffer())
                    {
                        m_d->streamProcessor.processDataBuffer(buffer);
                        enqueue(m_d->freeBuffers, buffer);
                    }
                    else if (internalState == StopIfQueueEmpty)
                        m_d->internalState = StopImmediately;

                    break;

                case Pause:
                    // transition to paused
                    setState(AnalysisWorkerState::Paused);
                    break;

                case StopImmediately:
                    // noop. loop will exit
                    break;

                case SingleStep:
                case PausedAfterSingleStep:
                    // logic error: may only happen in paused state
                    InvalidCodePath;
                    break;
            }
        }
        else if (m_d->state == AnalysisWorkerState::Paused)
        {
            switch (internalState)
            {
                case Pause:
                case PausedAfterSingleStep:
                    // stay paused
                    //qDebug() << __PRETTY_FUNCTION__ << "Paused: Pause|PausedAfterSingleStep";
                    QThread::msleep(std::min(PauseMaxSleep_ms, timetickGen.getTimeToNextTick_ms()));
                    break;

                case SingleStep:
                    //qDebug() << __PRETTY_FUNCTION__ << "Paused: SingleStep";

                    if (!singleStepProcState.buffer)
                    {
                        if (auto buffer = m_d->dequeueNextBuffer())
                        {
                            singleStepProcState = m_d->streamProcessor.singleStepInitState(buffer);
                        }
                        else if (internalState == StopIfQueueEmpty)
                            m_d->internalState = StopImmediately;
                    }

                    if (singleStepProcState.buffer)
                    {
                        single_step_one_event(singleStepProcState, m_d->streamProcessor);

                        QString logBuffer;
                        QTextStream logStream(&logBuffer);
                        log_processing_step(logStream, singleStepProcState, vatsTemplates, lfc);
                        m_d->context->logMessageRaw(logBuffer);

                        if (singleStepProcState.stepResult == ProcessingState::StepResult_AtEnd
                            || singleStepProcState.stepResult == ProcessingState::StepResult_Error)
                        {
                            enqueue(m_d->freeBuffers, singleStepProcState.buffer);
                            singleStepProcState = MVMEStreamProcessor::ProcessingState();
                        }
                    }

                    m_d->internalState = PausedAfterSingleStep;

                    break;

                case KeepRunning:
                case StopIfQueueEmpty:
                case StopImmediately:
                    // resume
                    setState(AnalysisWorkerState::Running);

                    // if singlestepping stopped in the middle of a buffer
                    // process the rest of the buffer, then go back to running
                    // state
                    if (singleStepProcState.buffer)
                    {
                        while (true)
                        {
                            single_step_one_event(singleStepProcState, m_d->streamProcessor);

                            //qDebug() << __PRETTY_FUNCTION__ << "resume after stepping case."
                            //    << "stepResult is: " << StepResult_StringTable[singleStepProcState.stepResult];

                            if (singleStepProcState.stepResult == ProcessingState::StepResult_AtEnd
                                || singleStepProcState.stepResult == ProcessingState::StepResult_Error)
                            {
                                enqueue(m_d->freeBuffers, singleStepProcState.buffer);
                                singleStepProcState = MVMEStreamProcessor::ProcessingState();
                                break;
                            }
                        }
                    }

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

        // reload the possibly modified atomic
        internalState = m_d->internalState;
    }

    counters.stopTime = QDateTime::currentDateTime();

    m_d->streamProcessor.endRun(m_d->context->getDAQStats());

    // analysis session auto save
    auto sessionPath = m_d->context->getWorkspacePath(QSL("SessionDirectory"));

    if (!sessionPath.isEmpty())
    {
        auto filename = sessionPath + "/last_session" + analysis::SessionFileExtension;
        auto result   = save_analysis_session(filename, m_d->context->getAnalysis());

        if (result.first)
        {
            //logMessage(QString("Auto saved analysis session to %1").arg(filename));
        }
        else
        {
            logMessage(QString("Error saving analysis session to %1: %2")
                       .arg(filename)
                       .arg(result.second));
        }
    }

    setState(AnalysisWorkerState::Idle);

    qDebug() << __PRETTY_FUNCTION__ << "end";
}

void MVMEStreamWorker::stop(bool whenQueueEmpty)
{
    qDebug() << QDateTime::currentDateTime().toString("HH:mm:ss")
        << __PRETTY_FUNCTION__ << (whenQueueEmpty ? "when empty" : "immediately");

    m_d->internalState = whenQueueEmpty ? StopIfQueueEmpty : StopImmediately;
}

void MVMEStreamWorker::pause()
{
    qDebug() << __PRETTY_FUNCTION__;

    Q_ASSERT(m_d->internalState != InternalState::Pause);
    m_d->internalState = InternalState::Pause;
}

void MVMEStreamWorker::resume()
{
    qDebug() << __PRETTY_FUNCTION__;

    Q_ASSERT(m_d->internalState == InternalState::Pause
             || m_d->internalState == InternalState::PausedAfterSingleStep);

    m_d->internalState = InternalState::KeepRunning;
}

void MVMEStreamWorker::singleStep()
{
    qDebug() << __PRETTY_FUNCTION__ << "current internalState ="
        << InternalState_StringTable[m_d->internalState];

    Q_ASSERT(m_d->internalState == InternalState::Pause
             || m_d->internalState == InternalState::PausedAfterSingleStep);

    qDebug() << __PRETTY_FUNCTION__ << "setting internalState to SingleStep";
    m_d->internalState = SingleStep;
}

AnalysisWorkerState MVMEStreamWorker::getState() const
{
    return m_d->state;
}

MVMEStreamProcessorCounters MVMEStreamWorker::getCounters() const
{
    return m_d->streamProcessor.getCounters();
}

void MVMEStreamWorker::setListFileVersion(u32 version)
{
    qDebug() << __PRETTY_FUNCTION__ << version;

    m_d->m_listFileVersion = version;
}

void MVMEStreamWorker::setStartPaused(bool startPaused)
{
    qDebug() << __PRETTY_FUNCTION__ << startPaused;
    Q_ASSERT(getState() == AnalysisWorkerState::Idle);

    m_d->m_startPaused = startPaused;
}

bool MVMEStreamWorker::getStartPaused() const
{
    return m_d->m_startPaused;
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

void MVMEStreamWorker::attachModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &consumer)
{
    m_d->streamProcessor.attachModuleConsumer(consumer);
}

void MVMEStreamWorker::removeModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &consumer)
{
    m_d->streamProcessor.removeModuleConsumer(consumer);
}

void MVMEStreamWorker::attachBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &consumer)
{
    m_d->streamProcessor.attachBufferConsumer(consumer);
}

void MVMEStreamWorker::removeBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &consumer)
{
    m_d->streamProcessor.removeBufferConsumer(consumer);
}