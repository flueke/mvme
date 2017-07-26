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
#include "mvme_event_processor.h"
#include "mvme_context.h"
#include "mvme_listfile.h"
#include "histo1d.h"
#include "mesytec_diagnostics.h"
#include "analysis/analysis.h"

#include <QCoreApplication>
#include <QElapsedTimer>

//#define MVME_EVENT_PROCESSOR_DEBUGGING

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
    inline QDebug qEPDebug() { return QDebug(QtDebugMsg); }
#else
    inline QNoDebug qEPDebug() { return QNoDebug(); }
#endif

enum RunAction
{
    KeepRunning,
    StopIfQueueEmpty,
    StopImmediately
};

struct MVMEEventProcessorPrivate
{
    MVMEContext *context = nullptr;

    MesytecDiagnostics *diag = nullptr;

    analysis::Analysis *analysis_ng;
    volatile RunAction m_runAction = KeepRunning;
    EventProcessorState m_state = EventProcessorState::Idle;

    u32 m_listFileVersion;
    int SectionTypeMask;
    int SectionTypeShift;
    int SectionSizeMask;
    int SectionSizeShift;
    int EventTypeMask;
    int EventTypeShift;
    int ModuleTypeMask;
    int ModuleTypeShift;
    int SubEventSizeMask;
    int SubEventSizeShift;
};

MVMEEventProcessor::MVMEEventProcessor(MVMEContext *context)
    : m_d(new MVMEEventProcessorPrivate)
{
    m_d->context = context;
    setListFileVersion(1);
}

MVMEEventProcessor::~MVMEEventProcessor()
{
    delete m_d->diag;
    delete m_d;
}

void MVMEEventProcessor::setDiagnostics(MesytecDiagnostics *diag)
{
    qDebug() << __PRETTY_FUNCTION__ << diag;
    delete m_d->diag;
    m_d->diag = diag;
}

MesytecDiagnostics *MVMEEventProcessor::getDiagnostics() const
{
    return m_d->diag;
}

void MVMEEventProcessor::removeDiagnostics()
{
    setDiagnostics(nullptr);
}

void MVMEEventProcessor::newRun(const RunInfo &runInfo)
{
    if (m_d->diag)
        m_d->diag->reset();

    {
        m_d->analysis_ng = m_d->context->getAnalysis();

        if (m_d->analysis_ng)
        {
            m_d->analysis_ng->beginRun(runInfo);
        }
    }
}

// Process an event buffer containing one or more events.
void MVMEEventProcessor::processDataBuffer(DataBuffer *buffer)
{
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
    qDebug() << __PRETTY_FUNCTION__ << "begin processing" << buffer;
#endif

    auto &stats = m_d->context->getDAQStats();

    try
    {
        //qEPDebug() << __PRETTY_FUNCTION__ << buffer;
        ++stats.mvmeBuffersSeen;

        BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align32);

        while (iter.longwordsLeft())
        {
            u32 sectionHeader = iter.extractU32();
            int sectionType = (sectionHeader & m_d->SectionTypeMask) >> m_d->SectionTypeShift;
            u32 sectionSize = (sectionHeader & m_d->SectionSizeMask) >> m_d->SectionSizeShift;

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
            qDebug() << __PRETTY_FUNCTION__
                << "sectionHeader" <<  QString::number(sectionHeader, 16)
                << "sectionType" << sectionType
                << "sectionSize" << sectionSize;
#endif

            if (sectionType != ListfileSections::SectionType_Event)
            {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                qDebug() << __PRETTY_FUNCTION__ << "skipping non event section";
#endif
                iter.skip(sectionSize * sizeof(u32));
                continue;
            }

            stats.addEventsRead(1);

            int eventIndex = (sectionHeader & m_d->EventTypeMask) >> m_d->EventTypeShift;

            u32 wordsLeftInSection = sectionSize;

            auto eventConfig = m_d->context->getConfig()->getEventConfig(eventIndex);

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
            qDebug() << __PRETTY_FUNCTION__
                << "eventIndex" << eventIndex
                << "eventConfig" << eventConfig;
#endif

            if (eventConfig)
                ++stats.eventCounters[eventConfig].events;

            if (m_d->analysis_ng && eventConfig)
                m_d->analysis_ng->beginEvent(eventConfig->getId());

            int moduleIndex = 0;

            // start of event section

            while (wordsLeftInSection > 1)
            {
                u8 *oldBufferP = iter.buffp;
                u32 subEventHeader = iter.extractU32();
                u32 subEventSize = (subEventHeader & m_d->SubEventSizeMask) >> m_d->SubEventSizeShift;
                u8 moduleType  = static_cast<u8>((subEventHeader & m_d->ModuleTypeMask) >> m_d->ModuleTypeShift);
                auto moduleConfig = m_d->context->getConfig()->getModuleConfig(eventIndex, moduleIndex);

                // TODO: skip event and report error in the following case:
                // moduleConfig->getModuleMeta().type != moduleType

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                qDebug() << __PRETTY_FUNCTION__
                    << "moduleSectionHeader" << QString::number(subEventHeader, 16)
                    << "moduleSectionSize" << subEventSize
                    << "moduleType" << static_cast<s32>(moduleType)
                    << "moduleConfig" << moduleConfig;
#endif


                MesytecDiagnostics *diag = nullptr;
                if (m_d->diag && m_d->diag->getEventIndex() == eventIndex && m_d->diag->getModuleIndex() == moduleIndex)
                {
                    diag = m_d->diag;
                    diag->beginEvent();
                }

                s32 wordIndexInSubEvent = 0;

                /* Iterate over a subevent. The last word in the subevent is
                 * the EndMarker so the actual data is in
                 * subevent[0..subEventSize-2]. */
                for (u32 i=0; i<subEventSize-1; ++i, ++wordIndexInSubEvent)
                {
                    u32 currentWord = iter.extractU32();

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                    qDebug("subEventSize=%u, i=%u, currentWord=0x%08x",
                           subEventSize, i, currentWord);
#endif

                    /* Do not pass BerrMarker words to the analysis if and only
                     * if they are the last words in the subevent.
                     * Specific to VMUSB: for BLT readouts one BerrMarker is
                     * written to the output stream, for MBLT readouts two of
                     * those markers are written!
                     * There is still the possibilty of missing the actual last
                     * word of the readout if that last word is the same as
                     * BerrMarker and the readout did not actually result in a
                     * BERR on the bus. */

                    // The MBLT case: if the two last words are BerrMarkers skip the current word.
                    if (subEventSize >= 3 && (i == subEventSize-3)
                        && (currentWord == BerrMarker)
                        && (iter.peekU32() == BerrMarker))
                        continue;

                    // If the last word is a BerrMarker skip it.
                    if (subEventSize >= 2 && (i == subEventSize-2)
                        && (currentWord == BerrMarker))
                        continue;

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                    if (moduleType == VMEModuleType::MesytecCounter)
                    {
                        emit logMessage(QString("CounterWord %1: 0x%2, evtIdx=%3, modIdx=%4")
                                        .arg(wordIndexInSubEvent)
                                        .arg(currentWord, 8, 16, QLatin1Char('0'))
                                        .arg(eventIndex)
                                        .arg(moduleIndex)
                                        );
                    }
#endif
                    if (diag)
                    {
                        diag->handleDataWord(currentWord);
                    }

                    if (m_d->analysis_ng && eventConfig && moduleConfig)
                    {
                        m_d->analysis_ng->processDataWord(eventConfig->getId(), moduleConfig->getId(),
                                                          currentWord, wordIndexInSubEvent);
                    }
                }

                if (diag)
                {
                    diag->endEvent();
                }

                u32 nextWord = iter.peekU32();
                if (nextWord == EndMarker)
                {
                    iter.extractU32();
                }
                else
                {
                    emit logMessage(QString("Error: did not find marker at end of subevent section "
                                            "(eventIndex=%1, moduleIndex=%2)")
                                    .arg(eventIndex)
                                    .arg(moduleIndex)
                                   );
                    return;
                }

                u8 *newBufferP = iter.buffp;
                wordsLeftInSection -= (newBufferP - oldBufferP) / sizeof(u32);
                ++moduleIndex;
            }

            // end of event section

            u32 nextWord = iter.peekU32();

            if (nextWord == EndMarker)
            {
                iter.extractU32();
            }
            else
            {
                emit logMessage(QString("Error: did not find marker at end of event section "
                                        "(eventIndex=%1)")
                                .arg(eventIndex)
                               );
                return;
            }

            if (m_d->analysis_ng && eventConfig)
                m_d->analysis_ng->endEvent(eventConfig->getId());
        }
        ++stats.totalBuffersProcessed;
    } catch (const end_of_buffer &)
    {
        emit logMessage(QString("Error: unexpectedly reached end of buffer"));
        ++stats.mvmeBuffersWithErrors;
    }

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
    qDebug() << __PRETTY_FUNCTION__ << "end processing" << buffer;
#endif
}

static const u32 FilledBufferWaitTimeout_ms = 250;
static const u32 ProcessEventsMinInterval_ms = 500;

void MVMEEventProcessor::startProcessing()
{
    qDebug() << __PRETTY_FUNCTION__ << "begin";
    Q_ASSERT(m_freeBufferQueue);
    Q_ASSERT(m_filledBufferQueue);
    Q_ASSERT(m_d->m_state == EventProcessorState::Idle);

    emit started();
    emit stateChanged(m_d->m_state = EventProcessorState::Running);

    QCoreApplication::processEvents();

    QElapsedTimer timeSinceLastProcessEvents;
    timeSinceLastProcessEvents.start();

    m_d->m_runAction = KeepRunning;

    while (m_d->m_runAction != StopImmediately)
    {
        DataBuffer *buffer = nullptr;

        {
            QMutexLocker lock(&m_filledBufferQueue->mutex);

            if (m_filledBufferQueue->queue.isEmpty())
            {
                if (m_d->m_runAction == StopIfQueueEmpty)
                    break;

                m_filledBufferQueue->wc.wait(&m_filledBufferQueue->mutex, FilledBufferWaitTimeout_ms);
            }

            if (!m_filledBufferQueue->queue.isEmpty())
            {
                buffer = m_filledBufferQueue->queue.dequeue();
            }
        }
        // The mutex is unlocked again at this point

        if (buffer)
        {
            processDataBuffer(buffer);

            // Put the buffer back into the free queue
            m_freeBufferQueue->mutex.lock();
            m_freeBufferQueue->queue.enqueue(buffer);
            m_freeBufferQueue->mutex.unlock();
            m_freeBufferQueue->wc.wakeOne();
        }

        // Process Qt events to be able to "receive" queued calls to our slots.
        if (timeSinceLastProcessEvents.elapsed() > ProcessEventsMinInterval_ms)
        {
            QCoreApplication::processEvents();
            timeSinceLastProcessEvents.restart();
        }
    }

    emit stopped();
    emit stateChanged(m_d->m_state = EventProcessorState::Idle);

    qDebug() << __PRETTY_FUNCTION__ << "end";
}

void MVMEEventProcessor::stopProcessing(bool whenQueueEmpty)
{
    qDebug() << QDateTime::currentDateTime().toString("HH:mm:ss")
        << __PRETTY_FUNCTION__ << (whenQueueEmpty ? "when empty" : "immediately");

    m_d->m_runAction = whenQueueEmpty ? StopIfQueueEmpty : StopImmediately;
}

EventProcessorState MVMEEventProcessor::getState() const
{
    return m_d->m_state;
}

void MVMEEventProcessor::setListFileVersion(u32 version)
{
    qDebug() << __PRETTY_FUNCTION__ << version;

    m_d->m_listFileVersion = version;

    if (m_d->m_listFileVersion == 0)
    {
        m_d->SectionTypeMask    = listfile_v0::SectionTypeMask;
        m_d->SectionTypeShift   = listfile_v0::SectionTypeShift;
        m_d->SectionSizeMask    = listfile_v0::SectionSizeMask;
        m_d->SectionSizeShift   = listfile_v0::SectionSizeShift;
        m_d->EventTypeMask      = listfile_v0::EventTypeMask;
        m_d->EventTypeShift     = listfile_v0::EventTypeShift;
        m_d->ModuleTypeMask     = listfile_v0::ModuleTypeMask;
        m_d->ModuleTypeShift    = listfile_v0::ModuleTypeShift;
        m_d->SubEventSizeMask   = listfile_v0::SubEventSizeMask;
        m_d->SubEventSizeShift  = listfile_v0::SubEventSizeShift;
    }
    else
    {
        m_d->SectionTypeMask    = listfile_v1::SectionTypeMask;
        m_d->SectionTypeShift   = listfile_v1::SectionTypeShift;
        m_d->SectionSizeMask    = listfile_v1::SectionSizeMask;
        m_d->SectionSizeShift   = listfile_v1::SectionSizeShift;
        m_d->EventTypeMask      = listfile_v1::EventTypeMask;
        m_d->EventTypeShift     = listfile_v1::EventTypeShift;
        m_d->ModuleTypeMask     = listfile_v1::ModuleTypeMask;
        m_d->ModuleTypeShift    = listfile_v1::ModuleTypeShift;
        m_d->SubEventSizeMask   = listfile_v1::SubEventSizeMask;
        m_d->SubEventSizeShift  = listfile_v1::SubEventSizeShift;
    }
}
