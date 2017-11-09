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

#include "analysis/a2_adapter.h"
#include "analysis/analysis.h"
#include "analysis/analysis_impl_switch.h"
#include "histo1d.h"
#include "mesytec_diagnostics.h"
#include "mvme_context.h"
#include "mvme_listfile.h"
#include "timed_block.h"

#include <QCoreApplication>
#include <QElapsedTimer>

//#define MVME_EVENT_PROCESSOR_DEBUGGING
//#define MVME_EVENT_PROCESSOR_DEBUG_BUFFERS

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

struct MultiEventModuleInfo
{
    u32 *subEventHeader = nullptr;  // Points to the mvme header preceeding the module data.
                                    // Null if the entry is not used.

    u32 *moduleHeader   = nullptr;  // Points to the current module data header.


    // The filter used to identify module headers and extract the module
    // section size.
    a2::data_filter::DataFilter moduleHeaderFilter;
    a2::data_filter::CacheEntry filterCacheModuleSectionSize;

    // Cache pointers to the corresponding module config.
    ModuleConfig *moduleConfig = nullptr;
};

using ModuleInfoArray = std::array<MultiEventModuleInfo, MaxVMEModules>;

struct MVMEEventProcessorPrivate
{
    MVMEContext *context = nullptr;

    MesytecDiagnostics *diag = nullptr;

    analysis::Analysis *analysis_ng = nullptr;
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

    std::array<ModuleInfoArray, MaxVMEEvents> eventInfos;
    std::array<EventConfig *, MaxVMEEvents> eventConfigs;
    std::array<bool, MaxVMEEvents> doMultiEventProcessing;

    MVMEEventProcessorCounters m_localStats;
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

void MVMEEventProcessor::newRun(const RunInfo &runInfo, const vme_analysis_common::VMEIdToIndex &vmeMap)
{
    m_d->analysis_ng = m_d->context->getAnalysis();

    m_d->eventInfos.fill({}); // Full clear of the eventInfo cache
    m_d->eventConfigs.fill(nullptr);
    m_d->doMultiEventProcessing.fill(false); // FIXME: change default to true some point

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

            modInfo.moduleHeaderFilter = a2::data_filter::make_filter(
                moduleConfig->getEventHeaderFilter().toStdString());

            modInfo.filterCacheModuleSectionSize = a2::data_filter::make_cache_entry(
                modInfo.moduleHeaderFilter, 'S');

            modInfo.moduleConfig = moduleConfig;

            qDebug() << __PRETTY_FUNCTION__ << moduleConfig->objectName() <<
                a2::data_filter::to_string(modInfo.moduleHeaderFilter).c_str();
        }

        m_d->eventConfigs[eventIndex] = eventConfig;
        m_d->doMultiEventProcessing[eventIndex] = eventConfig->isMultiEventProcessingEnabled();
    }

    if (m_d->analysis_ng)
    {
        m_d->analysis_ng->beginRun(runInfo, vmeMap);
    }

    if (m_d->diag)
    {
        m_d->diag->reset();
    }


    m_d->m_localStats = MVMEEventProcessorCounters();
    m_d->m_localStats.startTime = QDateTime::currentDateTime();
    m_d->m_localStats.stopTime  = QDateTime();
}

void MVMEEventProcessor::processDataBuffer(DataBuffer *buffer)
{
    auto &stats = m_d->context->getDAQStats();

#if 1
    try
    {
#endif
        const auto bufferNumber = stats.mvmeBuffersSeen;

        stats.mvmeBuffersSeen++;
        BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align32);

#ifdef MVME_EVENT_PROCESSOR_DEBUG_BUFFERS
        logMessage(QString(">>> Begin mvme buffer #%1").arg(bufferNumber));

        logBuffer(iter, [this](const QString &str) { logMessage(str); });

        logMessage(QString("<<< End mvme buffer #%1") .arg(bufferNumber));
#endif

        while (iter.longwordsLeft())
        {
            u32 sectionHeader = iter.extractU32();
            int sectionType = (sectionHeader & m_d->SectionTypeMask) >> m_d->SectionTypeShift;
            u32 sectionSize = (sectionHeader & m_d->SectionSizeMask) >> m_d->SectionSizeShift;

            if (sectionSize > iter.longwordsLeft())
            {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                QString msg = (QString("Error (mvme fmt): extracted section size exceeds buffer size!"
                                       " mvme buffer #%1, sectionHeader=0x%2, sectionSize=%3, wordsLeftInBuffer=%4")
                               .arg(bufferNumber)
                               .arg(sectionHeader, 8, 16, QLatin1Char('0'))
                               .arg(sectionSize)
                               .arg(iter.longwordsLeft()));
                qDebug() << msg;
                emit logMessage(msg);
#endif
                throw end_of_buffer();
            }

            if (sectionType == ListfileSections::SectionType_Event)
            {
                processEventSection(sectionHeader, iter.asU32(), sectionSize);
                iter.skip(sectionSize * sizeof(u32));
                //stats.addEventsRead(1);
            }
            else if (sectionType == ListfileSections::SectionType_Timetick
                     && m_d->analysis_ng)
            {
                // pass timeticks to the analysis
                Q_ASSERT(sectionSize == 0);
                m_d->analysis_ng->processTimetick();
            }
            else
            {
                // skip other section types
                iter.skip(sectionSize * sizeof(u32));
            }

            // sectionSize + header in bytes
            m_d->m_localStats.bytesProcessed += sectionSize * sizeof(u32) + sizeof(u32);
        }

        ++m_d->m_localStats.buffersProcessed;
#if 1
    }
    catch (const end_of_buffer &)
    {
        QString msg = QSL("Error (mvme fmt): unexpectedly reached end of buffer");
        qDebug() << msg;
        emit logMessage(msg);
        ++stats.mvmeBuffersWithErrors;
        ++m_d->m_localStats.buffersWithErrors;
        if (m_d->diag)
        {
            m_d->diag->reset();
        }
    }
#endif
}

void MVMEEventProcessor::processEventSection(u32 sectionHeader, u32 *data, u32 size)
{
    ++m_d->m_localStats.eventSections;
    auto &stats = m_d->context->getDAQStats();
    u32 eventIndex = (sectionHeader & m_d->EventTypeMask) >> m_d->EventTypeShift;

    if (eventIndex >= m_d->eventConfigs.size())
    {
        ++m_d->m_localStats.invalidEventIndices;
        qDebug() << __PRETTY_FUNCTION__ << "no event config for eventIndex = " << eventIndex
            << ", skipping input buffer";
        return;
    }

    ++m_d->m_localStats.eventCounters[eventIndex];

    auto &moduleInfos(m_d->eventInfos[eventIndex]);
    auto eventConfig = m_d->eventConfigs[eventIndex];

    // Clear modInfo pointers for the current eventIndex.
    for (auto &modInfo: moduleInfos)
    {
        modInfo.subEventHeader = nullptr;
        modInfo.moduleHeader   = nullptr;
    }

    BufferIterator eventIter(reinterpret_cast<u8 *>(data), size * sizeof(u32));

    //
    // Step1: collect all subevent headers and store them in the modinfo structures
    //
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
    qDebug() << __PRETTY_FUNCTION__ << "Begin Step 1";
#endif

    for (u32 moduleIndex = 0;
         moduleIndex < MaxVMEModules;
         ++moduleIndex)
    {
        if (eventIter.atEnd())
        {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
            qDebug() << __PRETTY_FUNCTION__ << "  break because eventIter.atEnd()";
#endif
            break;
        }

        if (eventIter.peekU32() == EndMarker)
        {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
            qDebug() << __PRETTY_FUNCTION__ << "  break because EndMarker found";
#endif
            break;
        }

        moduleInfos[moduleIndex].subEventHeader = eventIter.asU32();
        moduleInfos[moduleIndex].moduleHeader   = eventIter.asU32() + 1;
        // moduleHeader now points to the first module header in the event section

        u32 subEventHeader = eventIter.extractU32();
        u32 subEventSize   = (subEventHeader & m_d->SubEventSizeMask) >> m_d->SubEventSizeShift;
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
        qDebug("%s   eventIndex=%d, moduleIndex=%d, subEventSize=%u",
               __PRETTY_FUNCTION__, eventIndex, moduleIndex, subEventSize);
#endif
        // skip to the next subevent
        eventIter.skip(sizeof(u32), subEventSize);
    }

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
    qDebug() << __PRETTY_FUNCTION__ << "Step 1 complete: ";

    for (u32 moduleIndex = 0; moduleIndex < MaxVMEModules; ++moduleIndex)
    {
        const auto &mi(moduleInfos[moduleIndex]);

        if (!mi.subEventHeader)
            break;

        const auto &filter = mi.moduleHeaderFilter;

        qDebug("  eventIndex=%d, moduleIndex=%d, subEventHeader=0x%08x, moduleHeader=0x%08x, filter=%s, moduleConfig=%s",
               eventIndex, moduleIndex, *mi.subEventHeader, *mi.moduleHeader,
               a2::data_filter::to_string(filter).c_str(),
               mi.moduleConfig->objectName().toLocal8Bit().constData()
              );

    }
    qDebug() << __PRETTY_FUNCTION__ << "End Step 1 reporting";
#endif

    //
    // Step2: yield events in the correct order:
    // (mod0, ev0), (mod1, ev0), .., (mod0, ev1), (mod1, ev1), ..
    //

    // Avoid hanging in the loop below if the did not get a single subEventHeader in step 1
    bool done = (moduleInfos[0].subEventHeader == nullptr);
    const u32 *ptrToLastWord = data + size;
    std::array<u32, MaxVMEModules> eventCountsByModule;
    eventCountsByModule.fill(0);

    while (!done)
    {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
        qDebug("%s eventIndex=%u: Begin Step 2 loop", __PRETTY_FUNCTION__, eventIndex);
#endif

        {
            TIMED_BLOCK(TimedBlockId_MEP_Analysis_Loop);

            /* Early test to see if the first module still has a matching
             * header. This is done to avoid looping one additional time and
             * calling beginEvent()/endEvent() without any valid data available
             * for extractors to process. */
            if (m_d->doMultiEventProcessing[eventIndex]
                && !a2::data_filter::matches(moduleInfos[0].moduleHeaderFilter, *moduleInfos[0].moduleHeader))
            {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                    qDebug("%s (early check): moduleHeader=0x%08x did not match header filter -> done processing event section.",
                           __PRETTY_FUNCTION__, *moduleInfos[0].moduleHeader);
#endif
                done = true;
                break;
            }

            if (m_d->analysis_ng)
            {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                qDebug("%s analysis::beginEvent()", __PRETTY_FUNCTION__);
#endif
                m_d->analysis_ng->beginEvent(eventConfig->getId());
            }

            for (u32 moduleIndex = 0;
                 moduleInfos[moduleIndex].subEventHeader;
                 ++moduleIndex)
            {
                auto &mi(moduleInfos[moduleIndex]);
                Q_ASSERT(mi.moduleHeader);

                MesytecDiagnostics *diag = nullptr;

                if (m_d->diag
                    && m_d->diag->getEventIndex() == (s32)eventIndex
                    && m_d->diag->getModuleIndex() == (s32)moduleIndex)
                {
                    diag = m_d->diag;
                    diag->beginEvent();
                }

                if (!m_d->doMultiEventProcessing[eventIndex])
                {
                    // Do single event processing as multi event splitting is not
                    // enabled for this event.
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                    qDebug("%s eventIndex=%u, moduleIndex=%u: multi event disabled for event -> doing single event processing only",
                           __PRETTY_FUNCTION__, eventIndex, moduleIndex);
#endif

                    m_d->m_localStats.moduleCounters[eventIndex][moduleIndex]++;
                    eventCountsByModule[moduleIndex]++;

                    u32 subEventSize = (*mi.subEventHeader & m_d->SubEventSizeMask) >> m_d->SubEventSizeShift;

                    if (m_d->analysis_ng)
                    {
                        m_d->analysis_ng->processModuleData(eventConfig->getId(),
                                                            mi.moduleConfig->getId(),
                                                            mi.moduleHeader,
                                                            subEventSize);
                    }

                    if (diag)
                    {
                        diag->processModuleData(mi.moduleHeader, subEventSize);
                    }
                }
                // Multievent splitting is possible. Check for a header match and extract the data size.
                else if (a2::data_filter::matches(
                        mi.moduleHeaderFilter, *mi.moduleHeader))
                {

                    u32 moduleEventSize = a2::data_filter::extract(
                        mi.filterCacheModuleSectionSize, *mi.moduleHeader);


                    if (mi.moduleHeader + moduleEventSize + 1 > ptrToLastWord)
                    {
                        m_d->m_localStats.buffersWithErrors++;

                        QString msg = (QString("Error (mvme fmt): extracted module event size (%1) exceeds buffer size!"
                                               " eventIndex=%2, moduleIndex=%3, moduleHeader=0x%4, skipping event")
                                       .arg(moduleEventSize)
                                       .arg(eventIndex)
                                       .arg(moduleIndex)
                                       .arg(*mi.moduleHeader, 8, 16, QLatin1Char('0'))
                                      );
                        qDebug() << msg;
                        emit logMessage(msg);

                        done = true;
                        break;
                    }
                    else
                    {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                        qDebug("%s moduleIndex=%u, moduleHeader=0x%08x @%p, moduleEventSize=%u",
                               __PRETTY_FUNCTION__, moduleIndex, *mi.moduleHeader, mi.moduleHeader, moduleEventSize);
#endif

                        m_d->m_localStats.moduleCounters[eventIndex][moduleIndex]++;

                        if (m_d->analysis_ng)
                        {
                            m_d->analysis_ng->processModuleData(
                                eventConfig->getId(),
                                mi.moduleConfig->getId(),
                                mi.moduleHeader,
                                moduleEventSize + 1);
                        }

                        if (diag)
                        {
                            diag->processModuleData(mi.moduleHeader, moduleEventSize + 1);
                        }

                        // advance the moduleHeader by the event size
                        mi.moduleHeader += moduleEventSize + 1;
                        ++eventCountsByModule[moduleIndex];
                    }
                }
                else
                {
                    // We're at a data word inside the first module for which the
                    // header filter did not match. If the data is intact this
                    // should happen for the first module, meaning moduleIndex==0.
                    // At this point the same number of events have been processed
                    // for all modules in this event section. Checks could be done
                    // to make sure that all of the module header pointers now
                    // point to either BerrMarker or EndMarker. Also starting from
                    // the last modules pointer there should be optional
                    // BerrMarkers and an EndMarker followed by another EndMarker
                    // for the end of the event section we're in.
                    // TODO: implement the EndMarker checks

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                    qDebug("%s moduleHeader=0x%08x did not match header filter -> done processing event section.",
                           __PRETTY_FUNCTION__, *mi.moduleHeader);
#endif

                    // The data word did not match the module header filter. If the
                    // data structure is in intact this just means that we're at
                    // the end of the event section.
                    done = true;
                    break;
                }

                if (diag)
                {
                    diag->endEvent();
                }
            }

            if (m_d->analysis_ng)
            {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                qDebug("%s analysis::endEvent()", __PRETTY_FUNCTION__);
#endif
                m_d->analysis_ng->endEvent(eventConfig->getId());
            }

            // Single event processing: terminate after one loop through the modules.
            if (!m_d->doMultiEventProcessing[eventIndex])
            {
                break;
            }
        } // end TimedBlockId_MEP_Analysis_Loop
    }

    u32 firstModuleCount = eventCountsByModule[0];

    for (u32 moduleIndex = 0; moduleIndex < MaxVMEModules; ++moduleIndex)
    {
        if (m_d->eventInfos[eventIndex][moduleIndex].subEventHeader)
        {
            if (eventCountsByModule[moduleIndex] != firstModuleCount)
            {
                QString msg = (QString("Error (mvme multievent): Unequal number of subevents!"
                                       " eventIndex=%1, moduleIndex=%2, firstModuleCount=%3, thisModuleCount=%4")
                               .arg(eventIndex)
                               .arg(moduleIndex)
                               .arg(firstModuleCount)
                               .arg(eventCountsByModule[moduleIndex])
                              );
                qDebug() << msg;
                emit logMessage(msg);
            }
        }
    }
}

static const u32 FilledBufferWaitTimeout_ms = 250;
static const u32 ProcessEventsMinInterval_ms = 500;

/* Used at the start of a run after newRun() has been called and to resume from
 * paused state.
 * Does a2_begin_run() and a2_end_run() (threading stuff if enabled). */
void MVMEEventProcessor::startProcessing()
{
    qDebug() << __PRETTY_FUNCTION__ << "begin";
    Q_ASSERT(m_freeBuffers);
    Q_ASSERT(m_fullBuffers);
    Q_ASSERT(m_d->m_state == EventProcessorState::Idle);

    m_d->m_localStats.startTime = QDateTime::currentDateTime();
    m_d->m_localStats.stopTime  = QDateTime();

    emit started();
    emit stateChanged(m_d->m_state = EventProcessorState::Running);

    QCoreApplication::processEvents();

    QElapsedTimer timeSinceLastProcessEvents;
    timeSinceLastProcessEvents.start();

    m_d->m_runAction = KeepRunning;

    if (m_d->analysis_ng)
    {
        if (auto a2State = m_d->analysis_ng->getA2AdapterState())
        {
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

    m_d->m_localStats.stopTime = QDateTime::currentDateTime();

    if (m_d->analysis_ng)
    {
        if (auto a2State = m_d->analysis_ng->getA2AdapterState())
        {
            a2::a2_end_run(a2State->a2);
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

const MVMEEventProcessorCounters &MVMEEventProcessor::getCounters() const
{
    return m_d->m_localStats;
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
