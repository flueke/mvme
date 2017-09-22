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

// Maximum number of possible events in the vme config. Basically IRQ1-7 +
// external inputs + timers + some room to grow.
static const u32 MaxEvents = 12;
static const u32 MaxModulesPerEvent = 20;

struct MultiEventModuleInfo
{
    u32 *subEventHeader = nullptr;  // Points to the mvme header preceeding the module data.
                                    // Null if the entry is not used.
    u32 *moduleHeader   = nullptr;  // Points to the current module data header.
                                    // Null if no header has been read yet.
    DataFilter moduleHeaderFilter;
};

using ModuleInfoArray = std::array<MultiEventModuleInfo, MaxModulesPerEvent>;

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

    std::array<ModuleInfoArray, MaxEvents> eventInfos;
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
    m_d->eventInfos.fill({}); // Full clear of the eventInfo cache

    auto eventConfigs = m_d->context->getEventConfigs();

    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto moduleConfigs = eventConfig->getModuleConfigs();

        for (s32 moduleIndex = 0;
             moduleIndex < moduleConfigs.size();
             ++moduleIndex)
        {
            auto moduleConfig = moduleConfigs[moduleIndex];
            MultiEventModuleInfo &modInfo(m_d->eventInfos[eventIndex][moduleIndex]);
            modInfo.moduleHeaderFilter = makeFilterFromBytes(moduleConfig->getEventHeaderFilter());
        }
    }

    m_d->analysis_ng = m_d->context->getAnalysis();

    if (m_d->analysis_ng)
    {
        m_d->analysis_ng->beginRun(runInfo);
    }

    if (m_d->diag)
        m_d->diag->reset();
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

            if (sectionType == ListfileSections::SectionType_Timetick)
            {
                Q_ASSERT(sectionSize == 0);
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                qDebug() << __PRETTY_FUNCTION__ << "got a timetick section";
#endif
                if (m_d->analysis_ng)
                    m_d->analysis_ng->processTimetick();
                continue;
            }

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

                // Ensure the extracted module type and the module type from
                // the vme configuration are the same.
                if (moduleConfig->getModuleMeta().typeId != moduleType)
                {
                    QString msg = (QString("Error (mvme fmt): subEvent module type %1 "
                                           "does not match expected module type %3."
                                           "Skipping rest of buffer.")
                                   .arg(static_cast<u32>(moduleType))
                                   .arg(static_cast<u32>(moduleConfig->getModuleMeta().typeId))
                                  );
                    qDebug() << msg;
                    emit logMessage(msg);
                    return;
                }

                // Make sure the subevent size does not exceed the event
                // section size.
                if (wordsLeftInSection - 1 < subEventSize)
                {
                    QString msg = (QString("Error (mvme fmt): subevent size exceeds section size "
                                           "(subEventHeader=0x%1, subEventSize=%2, wordsLeftInSection=%3). "
                                           "Skipping rest of buffer.")
                                   .arg(subEventHeader, 8, 16, QLatin1Char('0'))
                                   .arg(subEventSize)
                                   .arg(wordsLeftInSection - 1)
                                  );
                    qDebug() << msg;
                    emit logMessage(msg);
                    return;

                }

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

                if (subEventSize > 0)
                {
                    u32 *firstWord = iter.asU32();
                    u32 *lastWord  = firstWord + subEventSize - 1;

                    if (*lastWord == EndMarker)
                    {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                        qDebug() << __PRETTY_FUNCTION__ << "did find EndMarker";
#endif
                        --lastWord;
                    }

                    if (subEventSize > 1 && *lastWord == BerrMarker)
                    {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                        qDebug() << __PRETTY_FUNCTION__ << "did find BerrMarker1";
#endif
                        --lastWord;
                    }

                    if (subEventSize > 2 && *lastWord == BerrMarker)
                    {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                        qDebug() << __PRETTY_FUNCTION__ << "did find BerrMarker2";
#endif
                        --lastWord;
                    }

                    u32 moduleDataSize = lastWord - firstWord + 1;

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                    qDebug() << __PRETTY_FUNCTION__ << "full module data dump:";
                    for (u32 i = 0; i < moduleDataSize; ++i)
                    {
                        qDebug("%s 0x%08x", __PRETTY_FUNCTION__, *(firstWord + i));
                    }
#endif

                    m_d->analysis_ng->processModuleData(eventConfig->getId(),
                                                        moduleConfig->getId(),
                                                        firstWord,
                                                        moduleDataSize);

                    if (diag)
                    {
                        diag->processModuleData(firstWord, moduleDataSize);
                    }
                }
                else
                {
                    QString msg = (QString("Warning (mvme fmt): got a module section of size 0! "
                                           "moduleSectionHeader=0x%1, moduleType=%2, eventIndex=%3, moduleIndex=%4")
                                   .arg(subEventHeader, 8, 16, QLatin1Char('0'))
                                   .arg(static_cast<s32>(moduleType))
                                   .arg(eventIndex)
                                   .arg(moduleIndex)
                                   );
                    qDebug() << msg;
                    emit logMessage(msg);
                }

                if (diag)
                {
                    diag->endEvent();
                }

                // end marker for subevent (aka module section)
                u32 *sectionLastWordPtr = iter.asU32() + subEventSize - 1;

                if (*sectionLastWordPtr != EndMarker)
                {
                    QString msg = (QString("Error (mvme fmt): did not find marker at end of subevent section "
                                           "(eventIndex=%1, moduleIndex=%2, nextWord=0x%3). Skipping rest of buffer.")
                                   .arg(eventIndex)
                                   .arg(moduleIndex)
                                   .arg(*sectionLastWordPtr, 8, 16, QLatin1Char('0'))
                                  );
                    qDebug() << msg;
                    emit logMessage(msg);
                    return;
                }
                ++sectionLastWordPtr;

                u8 *newBufferP = reinterpret_cast<u8 *>(sectionLastWordPtr);
                wordsLeftInSection -= (newBufferP - oldBufferP) / sizeof(u32);
                iter.buffp = newBufferP; // advance the iterator
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
                QString msg = (QString("Error (mvme fmt): did not find marker at end of event section "
                                       "(eventIndex=%1, nextWord=0x%2). Skipping rest of buffer.")
                               .arg(eventIndex)
                               .arg(nextWord, 8, 16, QLatin1Char('0'))
                              );
                qDebug() << msg;
                emit logMessage(msg);
                return;
            }

            if (m_d->analysis_ng && eventConfig)
                m_d->analysis_ng->endEvent(eventConfig->getId());
        }
        ++stats.totalBuffersProcessed;
    } catch (const end_of_buffer &)
    {
        emit logMessage(QString("Error (mvme fmt): unexpectedly reached end of buffer"));
        ++stats.mvmeBuffersWithErrors;
    }

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
    qDebug() << __PRETTY_FUNCTION__ << "end processing" << buffer;
#endif
}

void MVMEEventProcessor::processDataBuffer2(DataBuffer *buffer)
{
    auto &stats = m_d->context->getDAQStats();

    try
    {
        ++stats.mvmeBuffersSeen;
        BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align32);

        while (iter.longwordsLeft())
        {
            u32 sectionHeader = iter.extractU32();
            int sectionType = (sectionHeader & m_d->SectionTypeMask) >> m_d->SectionTypeShift;
            u32 sectionSize = (sectionHeader & m_d->SectionSizeMask) >> m_d->SectionSizeShift;

            if (sectionSize > iter.longwordsLeft())
                throw end_of_buffer();

            if (sectionType == ListfileSections::SectionType_Event)
            {
                processEventSection(sectionHeader, iter.asU32(), sectionSize);
                iter.skip(sectionSize * sizeof(u32));
            }
            if (sectionType == ListfileSections::SectionType_Timetick
                && m_d->analysis_ng)
            {
                // pass timeticks to the analysis
                Q_ASSERT(sectionSize == 0);
                m_d->analysis_ng->processTimetick();
            }
            else
            {
                // skip non-event sections
                iter.skip(sectionSize * sizeof(u32));
            }
        }
    }
    catch (const end_of_buffer &)
    {
        emit logMessage(QString("Error (mvme fmt): unexpectedly reached end of buffer"));
        ++stats.mvmeBuffersWithErrors;
    }
}

void MVMEEventProcessor::processEventSection(u32 sectionHeader, u32 *data, u32 size)
{
    int eventIndex = (sectionHeader & m_d->EventTypeMask) >> m_d->EventTypeShift;
    auto &moduleInfos(m_d->eventInfos[eventIndex]);

    // Clear modInfo pointers for the current eventIndex.
    for (auto &modInfo: moduleInfos)
    {
        modInfo.subEventHeader = nullptr;
        modInfo.moduleHeader   = nullptr;
    }

    BufferIterator eventIter(reinterpret_cast<u8 *>(data), size * sizeof(u32));

    // Step1: collect all subevent headers and store them in the modinfo structures
    for (u32 moduleIndex = 0;
         moduleIndex < MaxModulesPerEvent;
         ++moduleIndex)
    {
        if (eventIter.atEnd() || eventIter.peekU32() == EndMarker)
            break;

        moduleInfos[moduleIndex].subEventHeader = eventIter.asU32();
        moduleInfos[moduleIndex].moduleHeader   = eventIter.asU32() + 1;
        // moduleHeader now points to the first module header in the event section

        u32 subEventHeader = eventIter.extractU32();
        u32 subEventSize   = (subEventHeader & m_d->SubEventSizeMask) >> m_d->SubEventSizeShift;
        // skip to the next subevent
        eventIter.skip(sizeof(u32), subEventSize);
    }

    // Step2: yield events in the correct order:
    // (mod0, ev0), (mod1, ev0), .., (mod0, ev1), (mod1, ev1), ..
    bool done = false;
    while (!done)
    {
        for (u32 moduleIndex = 0;
             moduleInfos[moduleIndex].subEventHeader;
             ++moduleIndex)
        {
            auto &mi(moduleInfos[moduleIndex]);
            Q_ASSERT(mi.moduleHeader);

            if (mi.moduleHeaderFilter.matches(*mi.moduleHeader))
            {
                u32 moduleEventSize = mi.moduleHeaderFilter.extractData(
                    *mi.moduleHeader, 'S');
#error "Continue working on this piece of code please!"
                /* TODO:
                    if (m_d->analysis_ng && eventConfig)
                        m_d->analysis_ng->beginEvent(eventConfig->getId());

                    m_d->analysis_ng->processModuleData(eventConfig->getId(),
                                                        moduleConfig->getId(),
                                                        firstWord,
                                                        moduleDataSize);

                    if (m_d->analysis_ng && eventConfig)
                        m_d->analysis_ng->endEvent(eventConfig->getId());
                */

                // advance the moduleHeader by the event size
                mi.moduleHeader += moduleEventSize + 1;
            }
            else
            {
                // The data word did not match the module header filter. If the
                // data structure is in intact this just means that we're at
                // the end of the event section.
                done = true;
                break;
            }
        }
    }
}

static const u32 FilledBufferWaitTimeout_ms = 250;
static const u32 ProcessEventsMinInterval_ms = 500;

void MVMEEventProcessor::startProcessing()
{
    qDebug() << __PRETTY_FUNCTION__ << "begin";
    Q_ASSERT(m_freeBuffers);
    Q_ASSERT(m_fullBuffers);
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
            processDataBuffer(buffer);

            // Put the buffer back into the free queue
            m_freeBuffers->mutex.lock();
            m_freeBuffers->queue.enqueue(buffer);
            m_freeBuffers->mutex.unlock();
            m_freeBuffers->wc.wakeOne();
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
