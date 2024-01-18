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
#include "mvme_stream_processor.h"

#include "analysis/a2_adapter.h"
#include "analysis/analysis.h"
#include "databuffer.h"
#include "mvme_listfile.h"
#include "mesytec_diagnostics.h"
#include "mvme_listfile.h"
#include "util/leaky_bucket.h"
#include "util/perf.h"

//#define MVME_STREAM_PROCESSOR_DEBUG
//#define MVME_STREAM_PROCESSOR_DEBUG_BUFFERS

struct MultiEventModuleInfo
{
    /* Points to the mvme header preceeding the module data. nullptr if the
     * entry is not used. */
    u32 *moduleDataHeader = nullptr;                            // updated in processEventSection()

    /* Points to the current module data header. This is part of the readout
     * data, not part of the mvme format structure. */
    u32 *moduleHeader   = nullptr;

    /* The filter used to identify module headers and extract the module
     * section size. */
    a2::data_filter::DataFilter moduleHeaderFilter;           // constant between beginRun() and endRun()
    a2::data_filter::CacheEntry filterCacheModuleSectionSize; // constant between beginRun() and endRun()

    /* Cache pointers to the corresponding module config. */
    ModuleConfig *moduleConfig = nullptr;
};

using ModuleInfoArray = std::array<MultiEventModuleInfo, MaxVMEModules>;
using ProcessingState = MVMEStreamProcessor::ProcessingState;

struct MVMEStreamProcessorPrivate
{
    MVMEStreamProcessorCounters counters = {};
    RunInfo runInfo = {};
    analysis::Analysis *analysis = nullptr;
    VMEConfig *vmeConfig = nullptr;
    std::shared_ptr<MesytecDiagnostics> diag;
    MVMEStreamProcessor::Logger logger = nullptr;
    LeakyBucketMeter m_logThrottle;

    ListfileConstants listfileConstants;

    std::array<ModuleInfoArray, MaxVMEEvents> eventInfos;
    std::array<EventConfig *, MaxVMEEvents> eventConfigs;  // initialized in beginRun()
    std::array<bool, MaxVMEEvents> doMultiEventProcessing; // initialized in beginRun()

    QVector<std::shared_ptr<IStreamModuleConsumer>> moduleConsumers;
    QVector<std::shared_ptr<IStreamBufferConsumer>> bufferConsumers;

    MVMEStreamProcessorPrivate();

    void consumersBeginRun();

    void processEventSection(u32 sectionHeader, u32 *data, u32 size, u64 bufferNumber);
    void logMessage(const QString &msg, bool useThrottle = true);

    // Single Stepping
    struct SingleStepState
    {
        BufferIterator bufferIter;
        BufferIterator eventIter;
    };

    SingleStepState singleStepState;

    void initEventSectionIteration(ProcessingState &procState,
                                   u32 sectionHeader, u32 *data, u32 size);
    void stepNextEvent(ProcessingState &procState);
};

static const size_t MaxLogMessagesPerSecond = 5;

MVMEStreamProcessorPrivate::MVMEStreamProcessorPrivate()
    : m_logThrottle(MaxLogMessagesPerSecond, std::chrono::seconds(1))
{
}

MVMEStreamProcessor::MVMEStreamProcessor()
    : m_d(std::make_unique<MVMEStreamProcessorPrivate>())
{
}

MVMEStreamProcessor::~MVMEStreamProcessor()
{
}

void MVMEStreamProcessor::startup()
{
    for (auto &c: m_d->moduleConsumers)
        c->startup();

    for (auto &c: m_d->bufferConsumers)
        c->startup();
}

void MVMEStreamProcessor::shutdown()
{
    for (auto &c: m_d->moduleConsumers)
        c->shutdown();

    for (auto &c: m_d->bufferConsumers)
        c->shutdown();
}

void MVMEStreamProcessor::MVMEStreamProcessor::beginRun(
    const RunInfo &runInfo, analysis::Analysis *analysis, VMEConfig *vmeConfig,
    u32 listfileVersion, Logger logger)
{
    m_d->listfileConstants = listfile_constants(listfileVersion);

    Q_ASSERT(analysis);
    Q_ASSERT(vmeConfig);

    m_d->counters = {};

    m_d->runInfo = runInfo;
    m_d->analysis = analysis;
    m_d->vmeConfig = vmeConfig;
    m_d->logger = logger;

    m_d->eventInfos.fill({}); // Full clear of the eventInfo cache
    m_d->eventConfigs.fill(nullptr);
    m_d->doMultiEventProcessing.fill(false);

    //
    // build info for multievent processing
    //

    auto eventConfigs = vmeConfig->getEventConfigs();

    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto moduleConfigs = eventConfig->getModuleConfigs();

        // multievent enable: start out using the setting from the analysis side
        auto eventSettings = analysis->getVMEObjectSettings(eventConfig->getId());

        m_d->doMultiEventProcessing[eventIndex] =
            eventSettings.value("MultiEventProcessing").toBool();

        for (s32 moduleIndex = 0;
             moduleIndex < moduleConfigs.size();
             ++moduleIndex)
        {
            auto moduleConfig = moduleConfigs[moduleIndex];
            auto moduleSettings = analysis->getVMEObjectSettings(moduleConfig->getId());
            MultiEventModuleInfo &modInfo(m_d->eventInfos[eventIndex][moduleIndex]);

            // Check the analysis side module settings for the multi event header filter.
            // If none is found use the moduleConfig to get the default filter for the
            // module type.

            auto moduleEventHeaderFilter = moduleSettings.value(
                QSL("MultiEventHeaderFilter")).toString();

            if (moduleEventHeaderFilter.isEmpty())
            {
                moduleEventHeaderFilter = moduleConfig->getModuleMeta().eventHeaderFilter;
            }

            if (moduleEventHeaderFilter.isEmpty() || !moduleConfig->isEnabled())
            {
                // FIXME: why does mutli event for the whole event get disabled if one of
                // the modules disabled? This does seem unnecessary.

                // multievent enable: override the event setting as we don't
                // have a filter for splitting the event data
                m_d->doMultiEventProcessing[eventIndex] = false;
            }
            else
            {
                modInfo.moduleHeaderFilter = a2::data_filter::make_filter(
                    moduleEventHeaderFilter.toStdString());

                modInfo.filterCacheModuleSectionSize = a2::data_filter::make_cache_entry(
                    modInfo.moduleHeaderFilter, 'S');
            }

            modInfo.moduleConfig = moduleConfig;

            qDebug() << __PRETTY_FUNCTION__ << moduleConfig->objectName() <<
                a2::data_filter::to_string(modInfo.moduleHeaderFilter).c_str();
        }

        m_d->eventConfigs[eventIndex] = eventConfig;
    }

    // TODO: check that the analysis has been built (no object needs a rebuild)

    m_d->consumersBeginRun();

    if (m_d->diag)
    {
        m_d->diag->beginRun();
    }
}

void MVMEStreamProcessorPrivate::consumersBeginRun()
{
    qDebug() << __PRETTY_FUNCTION__ << "starting stream consumers";

    for (auto &c: moduleConsumers)
        c->beginRun(runInfo, vmeConfig, analysis);

    for (auto &c: bufferConsumers)
        c->beginRun(runInfo, vmeConfig, analysis);
}

void MVMEStreamProcessor::endRun(const DAQStats &stats)
{
    qDebug() << __PRETTY_FUNCTION__ << "begin";

    for (auto &c: m_d->moduleConsumers)
        c->endRun(stats);

    for (auto &c: m_d->bufferConsumers)
        c->endRun(stats);

    m_d->analysis->endRun();

    qDebug() << __PRETTY_FUNCTION__ << "end";
}
void MVMEStreamProcessor::processDataBuffer(DataBuffer *buffer)
{
    Q_ASSERT(!m_d->singleStepState.bufferIter.data);
    Q_ASSERT(buffer);

    const auto bufferNumber = buffer->id;

    try
    {
        BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align32);

#ifdef MVME_STREAM_PROCESSOR_DEBUG_BUFFERS
        m_d->logMessage(QString(">>> Begin mvme buffer #%1").arg(bufferNumber), false);

        logBuffer(iter, [this](const QString &str) { m_d->logMessage(str, false); });

        m_d->logMessage(QString("<<< End mvme buffer #%1") .arg(bufferNumber), false);
#endif

        const auto &lf(m_d->listfileConstants);

        while (iter.longwordsLeft())
        {
            u32 sectionHeader = iter.extractU32();
            u32 sectionType = lf.getSectionType(sectionHeader);
            u32 sectionSize = lf.getSectionSize(sectionHeader);

            if (unlikely(sectionSize > iter.longwordsLeft()))
            {
//#ifdef MVME_STREAM_PROCESSOR_DEBUG
                QString msg = (QString("Error (mvme stream, buffer#%1): extracted section size exceeds buffer size!"
                                       " sectionHeader=0x%2, sectionSize=%3, wordsLeftInBuffer=%4")
                               .arg(bufferNumber)
                               .arg(sectionHeader, 8, 16, QLatin1Char('0'))
                               .arg(sectionSize)
                               .arg(iter.longwordsLeft()));
                m_d->logMessage(msg);
//#endif
                throw end_of_buffer();
            }

            if (sectionType == ListfileSections::SectionType_Timetick)
            {
                /* Assert the behaviour of the readout workers here: they should
                 * not enqueue timetick buffers as it's handled via
                 * processExternalTimetick(). Timetick sections should only be
                 * encountered during replay. */
                Q_ASSERT(m_d->runInfo.isReplay);

                for (auto &c: m_d->moduleConsumers)
                    c->processTimetick();
            }

            if (likely(sectionType == ListfileSections::SectionType_Event))
            {
                m_d->processEventSection(sectionHeader, iter.asU32(), sectionSize, bufferNumber);
                iter.skip(sectionSize * sizeof(u32));
            }
            else if (sectionType == ListfileSections::SectionType_Timetick
                     && m_d->runInfo.isReplay)
            {
                // If it's a replay pass timetick sections to the analysis.
                m_d->analysis->processTimetick();
                iter.skip(sectionSize * sizeof(u32));
            }
            else
            {
                // skip other section types
                iter.skip(sectionSize * sizeof(u32));
            }

            // sectionSize + header in bytes
            m_d->counters.bytesProcessed += sectionSize * sizeof(u32) + sizeof(u32);
        }

        m_d->counters.buffersProcessed++;
    }
    catch (const end_of_buffer &e)
    {
        QString msg = QSL("Error (mvme stream, buffer#%1, full processing): "
                          "unexpectedly reached end of buffer").arg(bufferNumber);
        m_d->logMessage(msg);
        m_d->counters.buffersWithErrors++;

        if (m_d->diag)
        {
            m_d->diag->beginRun();
        }
    }

    for (auto &c: m_d->bufferConsumers)
        c->processBuffer(buffer->tag, buffer->id, buffer->asU32(), buffer->usedU32());
}

void MVMEStreamProcessor::processExternalTimetick()
{
    Q_ASSERT(!m_d->runInfo.isReplay);

    m_d->analysis->processTimetick();

    for (auto &c: m_d->moduleConsumers)
        c->processTimetick();
}

void MVMEStreamProcessorPrivate::processEventSection(u32 sectionHeader,
                                                     u32 *data, u32 size,
                                                     u64 bufferNumber)
{
    const auto &lf(listfileConstants);

    this->counters.totalEvents++;
    const u32 eventIndex = lf.getEventIndex(sectionHeader);

    if (unlikely(eventIndex >= this->eventConfigs.size()
                 || !this->eventConfigs[eventIndex]))
    {
        ++this->counters.invalidEventIndices;
        qDebug() << __PRETTY_FUNCTION__ << "no event config for eventIndex = " << eventIndex
            << ", skipping input buffer";
        return;
    }

    this->counters.eventCounters[eventIndex]++;

    auto &moduleInfos(this->eventInfos[eventIndex]);

    // Clear modInfo pointers for the current eventIndex.
    for (auto &modInfo: moduleInfos)
    {
        modInfo.moduleDataHeader = nullptr;
        modInfo.moduleHeader   = nullptr;
    }

    BufferIterator eventIter(reinterpret_cast<u8 *>(data), size * sizeof(u32));

    //
    // Step1: collect all subevent headers and store them in the modinfo structures
    //
#ifdef MVME_STREAM_PROCESSOR_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "Begin Step 1";
#endif

    for (u32 moduleIndex = 0;
         moduleIndex < MaxVMEModules;
         ++moduleIndex)
    {
        if (eventIter.atEnd())
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "  break because eventIter.atEnd()";
#endif
            break;
        }

        if (eventIter.peekU32() == EndMarker)
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "  break because EndMarker found";
#endif
            break;
        }

        moduleInfos[moduleIndex].moduleDataHeader = eventIter.asU32();
        moduleInfos[moduleIndex].moduleHeader   = eventIter.asU32() + 1;
        // moduleHeader now points to the first module header in the event section

        u32 moduleDataHeader = eventIter.extractU32();
        u32 moduleDataSize   = lf.getModuleDataSize(moduleDataHeader);
#ifdef MVME_STREAM_PROCESSOR_DEBUG
        qDebug("%s   eventIndex=%d, moduleIndex=%d, moduleDataSize=%u",
               __PRETTY_FUNCTION__, eventIndex, moduleIndex, moduleDataSize);
#endif
        // skip to the next subevent
        eventIter.skip(sizeof(u32), moduleDataSize);
    }

#ifdef MVME_STREAM_PROCESSOR_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "Step 1 complete: ";

    for (u32 moduleIndex = 0; moduleIndex < MaxVMEModules; ++moduleIndex)
    {
        const auto &mi(moduleInfos[moduleIndex]);

        if (!mi.moduleDataHeader)
            break;

        const auto &filter = mi.moduleHeaderFilter;

        qDebug("  eventIndex=%d, moduleIndex=%d, moduleDataHeader=0x%08x, moduleHeader=0x%08x, filter=%s, moduleConfig=%s",
               eventIndex, moduleIndex, *mi.moduleDataHeader, *mi.moduleHeader,
               a2::data_filter::to_string(filter).c_str(),
               mi.moduleConfig->objectName().toLocal8Bit().constData()
              );

    }
    qDebug() << __PRETTY_FUNCTION__ << "End Step 1 reporting";
#endif

    //
    // Empty event suppression. Should not happen if the readout is configured
    // and working correctly.
    //
    if (!this->doMultiEventProcessing[eventIndex])
    {
        bool allEmpty = true;

        for (u32 moduleIndex=0; moduleIndex < MaxVMEModules; ++moduleIndex)
        {
            auto &mi = moduleInfos[moduleIndex];

            if (!mi.moduleDataHeader)
                continue;

            if (lf.getModuleDataSize(*mi.moduleDataHeader) != 1)
            {
                allEmpty = false;
                break;
            }

            if (*(mi.moduleDataHeader + 1) != EndMarker)
            {
                allEmpty = false;
                break;
            }
        }

        if (allEmpty)
        {
            //qDebug("  eventIndex=%d, all subevents are empty! suppressing event", eventIndex);
            ++this->counters.suppressedEmptyEvents;
            return;
        }
    }

    //
    // Step2: yield events in the correct order:
    // (mod0, ev0), (mod1, ev0), .., (mod0, ev1), (mod1, ev1), ..
    //

    bool done = (moduleInfos[0].moduleDataHeader == nullptr);
    const u32 *ptrToLastWord = data + size;
    std::array<u32, MaxVMEModules> eventCountsByModule;
    eventCountsByModule.fill(0);

    while (!done)
    {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
        qDebug("%s eventIndex=%u: Begin Step 2 loop", __PRETTY_FUNCTION__, eventIndex);
#endif

        /* Early test to see if the first module still has a matching
         * header. This is done to avoid looping one additional time and
         * calling beginEvent()/endEvent() without any valid data available
         * for extractors to process. */
        if (this->doMultiEventProcessing[eventIndex]
            && !a2::data_filter::matches(moduleInfos[0].moduleHeaderFilter, *moduleInfos[0].moduleHeader))
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug("%s (early check): moduleHeader=0x%08x did not match header filter -> done processing event section.",
                   __PRETTY_FUNCTION__, *moduleInfos[0].moduleHeader);
#endif
            done = true;
            break;
        }

        if (this->analysis)
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug("%s analysis::beginEvent()", __PRETTY_FUNCTION__);
#endif
            this->analysis->beginEvent(eventIndex);
        }

        for (auto &c: this->moduleConsumers)
        {
            c->beginEvent(eventIndex);
        }

        for (u32 moduleIndex = 0;
             moduleInfos[moduleIndex].moduleDataHeader;
             ++moduleIndex)
        {
            auto &mi(moduleInfos[moduleIndex]);
            Q_ASSERT(mi.moduleHeader);

            MesytecDiagnostics *diag = this->diag.get();

            // FIXME: why is this in here instead of one level up?
            if (this->diag)
            {
                this->diag->beginEvent(eventIndex);
            }

            if (!this->doMultiEventProcessing[eventIndex])
            {
                // Do single event processing as multi event splitting is not
                // enabled for this event.
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                qDebug("%s eventIndex=%u, moduleIndex=%u: multi event disabled for event -> doing single event processing only",
                       __PRETTY_FUNCTION__, eventIndex, moduleIndex);
#endif

                u32 moduleDataSize = lf.getModuleDataSize(*mi.moduleDataHeader);

                // Check if we should count the module hit or if the readout
                // yielded empty data.

                bool countModuleHit = true;

                // For the SIS3153: empty frames contain an EndMarker added to
                // the mvmelst format.
                if (moduleDataSize == 1 && *mi.moduleHeader == EndMarker)
                {
                    countModuleHit = false;
                }
                // VMUSB: The VME blockread yield two BerrMarkers and the
                // mvmelst format adds an EndMarker
                else if (moduleDataSize == 3
                         && mi.moduleHeader[0] == BerrMarker
                         && mi.moduleHeader[1] == BerrMarker
                         && mi.moduleHeader[2] == EndMarker)
                {
                    countModuleHit = false;
                }

                if (countModuleHit)
                {
                    this->counters.moduleCounters[eventIndex][moduleIndex]++;
                    eventCountsByModule[moduleIndex]++;
                }


                if (this->analysis)
                {
                    this->analysis->processModuleData(
                        eventIndex,
                        moduleIndex,
                        mi.moduleHeader,
                        moduleDataSize);
                }

                if (diag)
                {
                    diag->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, moduleDataSize);
                }

                for (auto c: this->moduleConsumers)
                {
                    c->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, moduleDataSize);
                }
            }
            // Multievent splitting is possible. Check for a header match and extract the data size.
            else if (a2::data_filter::matches(mi.moduleHeaderFilter, *mi.moduleHeader))
            {
                u32 moduleEventSize = a2::data_filter::extract(
                    mi.filterCacheModuleSectionSize, *mi.moduleHeader);

                if (unlikely(mi.moduleHeader + moduleEventSize + 1 > ptrToLastWord))
                {
                    this->counters.buffersWithErrors++;
                    if (eventIndex < MaxVMEEvents && moduleIndex < MaxVMEModules)
                        ++counters.moduleEventSizeExceedsBuffer[eventIndex][moduleIndex];

                    QString msg = (QString("Error (mvme stream, buffer#%1): extracted module event size (%2) exceeds buffer size!"
                                           " eventIndex=%3, moduleIndex=%4, moduleHeader=0x%5, skipping event")
                                   .arg(bufferNumber)
                                   .arg(moduleEventSize)
                                   .arg(eventIndex)
                                   .arg(moduleIndex)
                                   .arg(*mi.moduleHeader, 8, 16, QLatin1Char('0'))
                                  );
                    logMessage(msg);

                    done = true;
                    break;
                }
                else
                {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                    qDebug("%s moduleIndex=%u, moduleHeader=0x%08x @%p, moduleEventSize=%u",
                           __PRETTY_FUNCTION__, moduleIndex, *mi.moduleHeader, mi.moduleHeader, moduleEventSize);
#endif

                    this->counters.moduleCounters[eventIndex][moduleIndex]++;

                    if (this->analysis)
                    {
                        this->analysis->processModuleData(
                            eventIndex,
                            moduleIndex,
                            mi.moduleHeader,
                            moduleEventSize + 1);
                    }

                    if (diag)
                    {
                        diag->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, moduleEventSize + 1);
                    }

                    for (auto c: this->moduleConsumers)
                    {
                        c->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, moduleEventSize + 1);
                    }

                    // advance the moduleHeader by the event size
                    mi.moduleHeader += moduleEventSize + 1;
                    ++eventCountsByModule[moduleIndex];
                }
            }
            else
            {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                qDebug("%s moduleHeader=0x%08x did not match header filter -> done processing event section.",
                       __PRETTY_FUNCTION__, *mi.moduleHeader);
#endif
                // We're at a data word inside the first module for which the
                // header filter did not match. If the data is intact this
                // should happen for the first module, meaning moduleIndex==0.
                // At this point the same number of events have been processed
                // for all modules in this event section. Checks could be done
                // to make sure that all of the module header pointers now
                // point to either BerrMarker (in the case of vmusb) or
                // EndMarker. Also starting from the last modules pointer there
                // should be optional BerrMarkers and an EndMarker followed by
                // another EndMarker for the end of the event section we're in.
                // => Some checks are done below, outside the outer loop.

                done = true;
                break;
            }

            if (diag)
            {
                diag->endEvent(eventIndex);
            }
        }

        /* At this point the data of all the modules in the current event has
         * been passed to the analysis. The analysis performed the parameter
         * extraction step. The extracted parameter double values are available
         * at the output pipes of the data sources attached to each module of
         * the current event. */
        if (this->analysis)
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug("%s analysis::endEvent()", __PRETTY_FUNCTION__);
#endif
            /* This call makes the analysis system perform one step of all
             * operators for the current event index. After the call the output
             * pipes of the involved operators will contain the data produced
             * for this event. */
            this->analysis->endEvent(eventIndex);
        }

        for (auto c: this->moduleConsumers)
        {
            c->endEvent(eventIndex);
        }

        // Single event processing: terminate after one loop through the modules.
        if (!this->doMultiEventProcessing[eventIndex])
        {
            break;
        }
    }

    // Some final integrity checks if multievent splitting was done
    if (this->doMultiEventProcessing[eventIndex])
    {
        bool err = false;

        for (u32 moduleIndex = 0; moduleIndex < MaxVMEModules; ++moduleIndex)
        {
            const auto &mi(this->eventInfos[eventIndex][moduleIndex]);

            if (!mi.moduleHeader)
                break;

            u32 *endMarkerPtr = mi.moduleHeader;

            // Skip over BerrMarkers inserted by VMUSB. One marker for BLT, two for
            // MBLT but only if the readout actually hit Berr.
            while (*endMarkerPtr == BerrMarker && endMarkerPtr <= ptrToLastWord) endMarkerPtr++;

            if (*endMarkerPtr != EndMarker)
            {
                QString msg = (QString(
                        "Error (mvme stream, buffer#%1, multievent): "
                        "module header filter did not match -> expected EndMarker but found "
                        "0x%2, module data structure might be corrupt."
                        " (eventIndex=%3, moduleIndex=%4)")
                    .arg(bufferNumber)
                    .arg(*endMarkerPtr, 8, 16, QLatin1Char('0'))
                    .arg(eventIndex)
                    .arg(moduleIndex)
                    );
                logMessage(msg);
                err = true;
                break; // reporting one error is enough
            }
        }

        u32 firstModuleCount = eventCountsByModule[0];

        for (u32 moduleIndex = 0; moduleIndex < MaxVMEModules; ++moduleIndex)
        {
            if (this->eventInfos[eventIndex][moduleIndex].moduleDataHeader)
            {
                if (eventCountsByModule[moduleIndex] != firstModuleCount)
                {
                    QString msg = (QString("Error (mvme stream, buffer#%1, multievent): Unequal number of subevents!"
                                           " eventIndex=%2, moduleIndex=%3, firstModuleCount=%4, thisModuleCount=%5")
                                   .arg(bufferNumber)
                                   .arg(eventIndex)
                                   .arg(moduleIndex)
                                   .arg(firstModuleCount)
                                   .arg(eventCountsByModule[moduleIndex])
                                  );
                    logMessage(msg);
                    err = true;
                    break; // reporting one error is enough
                }
            }
        }

        if (err)
        {
            this->counters.buffersWithErrors++;
        }
    }
}

MVMEStreamProcessor::ProcessingState
MVMEStreamProcessor::singleStepInitState(DataBuffer *buffer)
{
    Q_ASSERT(!m_d->singleStepState.bufferIter.data);

    ProcessingState procState = ProcessingState();
    procState.buffer = buffer;

    m_d->singleStepState.bufferIter = BufferIterator(buffer->data, buffer->used, BufferIterator::Align32);
    m_d->singleStepState.eventIter = {};

    return procState;
}

MVMEStreamProcessor::ProcessingState &
MVMEStreamProcessor::singleStepNextStep(ProcessingState &procState)
{
    Q_ASSERT(m_d->singleStepState.bufferIter.data);
    Q_ASSERT(procState.buffer);

    const auto bufferNumber = procState.buffer->id;

    try
    {
        // has more event data?
        if (m_d->singleStepState.eventIter.data)
        {
            m_d->stepNextEvent(procState);
            return procState;
        }

        Q_ASSERT(!m_d->singleStepState.eventIter.data);
        auto &iter(m_d->singleStepState.bufferIter);

        procState.resetModuleDataOffsets();
        procState.stepResult = ProcessingState::StepResult_Unset;

        auto &lf(m_d->listfileConstants);

        if (iter.longwordsLeft())
        {
            u32 sectionHeader = iter.extractU32();
            u32 sectionType = lf.getSectionType(sectionHeader);
            u32 sectionSize = lf.getSectionSize(sectionHeader);

            procState.lastSectionHeaderOffset = iter.current32BitOffset() - 1;

            if (unlikely(sectionSize > iter.longwordsLeft()))
            {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                QString msg = (QString("Error (mvme stream, buffer#%1): extracted section size exceeds buffer size!"
                                       " sectionHeader=0x%2, sectionSize=%3, wordsLeftInBuffer=%4")
                               .arg(bufferNumber)
                               .arg(sectionHeader, 8, 16, QLatin1Char('0'))
                               .arg(sectionSize)
                               .arg(iter.longwordsLeft()));
                m_d->logMessage(msg);
#endif
                throw end_of_buffer();
            }

            // Handle the start of a new event section.
            // If multievent processing is enabled for the event multiple calls
            // to step() may be required to completely process the event
            // section.
            if (likely(sectionType == ListfileSections::SectionType_Event))
            {
                // initialize event iteration state
                m_d->initEventSectionIteration(procState, sectionHeader, iter.asU32(), sectionSize);

                if (procState.stepResult == ProcessingState::StepResult_Error)
                    return procState;

                // perform the first step. Updates procState
                m_d->stepNextEvent(procState);

                // Test if the event was already completely processed. This
                // happens in the non-multievent case.
                if (procState.stepResult == ProcessingState::StepResult_EventComplete)
                {
                    iter.skip(sectionSize * sizeof(u32));
                    m_d->singleStepState.eventIter = {};
                    m_d->counters.bytesProcessed += sectionSize * sizeof(u32) + sizeof(u32);
                }
            }
            else if (sectionType == ListfileSections::SectionType_Timetick)
            {
                // If it's a replay pass timetick sections to the analysis and consumers.
                Q_ASSERT(m_d->runInfo.isReplay);
                Q_ASSERT(sectionSize == 0);

                m_d->analysis->processTimetick();

                for (auto &c: m_d->moduleConsumers)
                    c->processTimetick();

                m_d->counters.bytesProcessed += sectionSize * sizeof(u32) + sizeof(u32);
            }
            else
            {
                // skip other section types
                iter.skip(sectionSize * sizeof(u32));
                m_d->counters.bytesProcessed += sectionSize * sizeof(u32) + sizeof(u32);
            }
        }
        else
        {
            procState.resetModuleDataOffsets();
            procState.stepResult = ProcessingState::StepResult_AtEnd;
            m_d->singleStepState.bufferIter = {};
            m_d->singleStepState.eventIter = {};
            m_d->counters.buffersProcessed++;
        }
    }
    catch (const end_of_buffer &e)
    {
        QString msg = QSL("Error (mvme stream, buffer#%1, stepping): "
                          "unexpectedly reached end of buffer").arg(bufferNumber);
        m_d->logMessage(msg);
        m_d->counters.buffersWithErrors++;

        if (m_d->diag)
        {
            m_d->diag->beginRun();
        }

        // Set error flag and reset state. It's illegal to call step() after
        // this. Instead a new buffer has to be passed in via initState().
        procState.resetModuleDataOffsets();
        procState.stepResult = ProcessingState::StepResult_Error;
        m_d->singleStepState.bufferIter = {};
        m_d->singleStepState.eventIter = {};
    }

    return procState;
}

void MVMEStreamProcessorPrivate::initEventSectionIteration(
    ProcessingState &procState, u32 sectionHeader, u32 *data, u32 size)
{
    Q_ASSERT(!this->singleStepState.eventIter.data);

    auto &lf(listfileConstants);

    this->counters.totalEvents++;
    const u32 eventIndex = lf.getEventIndex(sectionHeader);

    if (unlikely(eventIndex >= this->eventConfigs.size()
                 || !this->eventConfigs[eventIndex]))
    {
        ++this->counters.invalidEventIndices;
        qDebug() << __PRETTY_FUNCTION__ << "no event config for eventIndex = " << eventIndex
            << ", skipping input buffer";
        procState.stepResult = ProcessingState::StepResult_Error;
        return;
    }

    this->counters.eventCounters[eventIndex]++;

    auto &moduleInfos(this->eventInfos[eventIndex]);

    // Clear modInfo pointers for the current eventIndex.
    for (auto &modInfo: moduleInfos)
    {
        modInfo.moduleDataHeader = nullptr;
        modInfo.moduleHeader   = nullptr;
    }

    procState.resetModuleDataOffsets();

    BufferIterator eventIter(reinterpret_cast<u8 *>(data), size * sizeof(u32));

    //
    // Step1: collect all subevent headers and store them in the modinfo structures
    //
#ifdef MVME_STREAM_PROCESSOR_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "Begin Step 1";
#endif

    for (u32 moduleIndex = 0;
         moduleIndex < MaxVMEModules;
         ++moduleIndex)
    {
        if (eventIter.atEnd())
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "  break because eventIter.atEnd()";
#endif
            break;
        }

        if (eventIter.peekU32() == EndMarker)
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "  break because EndMarker found";
#endif
            break;
        }

        moduleInfos[moduleIndex].moduleDataHeader = eventIter.asU32();
        moduleInfos[moduleIndex].moduleHeader   = eventIter.asU32() + 1;
        // moduleHeader now points to the first module header in the event section

        u32 moduleDataHeader = eventIter.extractU32();
        u32 moduleDataSize   = lf.getModuleDataSize(moduleDataHeader);
#ifdef MVME_STREAM_PROCESSOR_DEBUG
        qDebug("%s   eventIndex=%d, moduleIndex=%d, moduleDataSize=%u",
               __PRETTY_FUNCTION__, eventIndex, moduleIndex, moduleDataSize);
#endif
        // skip to the next subevent
        eventIter.skip(sizeof(u32), moduleDataSize);
    }

#ifdef MVME_STREAM_PROCESSOR_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "Step 1 complete: ";

    for (u32 moduleIndex = 0; moduleIndex < MaxVMEModules; ++moduleIndex)
    {
        const auto &mi(moduleInfos[moduleIndex]);

        if (!mi.moduleDataHeader)
            break;

        const auto &filter = mi.moduleHeaderFilter;

        qDebug("  eventIndex=%d, moduleIndex=%d, moduleDataHeader=0x%08x, moduleHeader=0x%08x, filter=%s, moduleConfig=%s",
               eventIndex, moduleIndex, *mi.moduleDataHeader, *mi.moduleHeader,
               a2::data_filter::to_string(filter).c_str(),
               mi.moduleConfig->objectName().toLocal8Bit().constData()
              );

    }
    qDebug() << __PRETTY_FUNCTION__ << "End Step 1 reporting";
#endif

    this->singleStepState.eventIter = BufferIterator(reinterpret_cast<u8 *>(data), size * sizeof(u32));

    procState.stepResult = ProcessingState::StepResult_Unset;
}

void MVMEStreamProcessorPrivate::stepNextEvent(ProcessingState &procState)
{
    Q_ASSERT(this->singleStepState.eventIter.data);

    //
    // Step2: yield events in the correct order:
    // (mod0, ev0), (mod1, ev0), .., (mod0, ev1), (mod1, ev1), ..
    //

    Q_ASSERT(procState.buffer);
    Q_ASSERT(0 <= procState.lastSectionHeaderOffset);
    Q_ASSERT(procState.lastSectionHeaderOffset < static_cast<s64>(procState.buffer->size / sizeof(u32)));

    auto &lf(listfileConstants);

    const auto bufferNumber = procState.buffer->id;
    u32 sectionHeader = *procState.buffer->asU32(procState.lastSectionHeaderOffset * sizeof(u32));
    u32 sectionType = lf.getSectionType(sectionHeader);
    //u32 sectionSize = lf.getSectionSize(sectionHeader);
    const u32 eventIndex = lf.getEventIndex(sectionHeader);

    Q_ASSERT(sectionType == ListfileSections::SectionType_Event);

    auto &moduleInfos(this->eventInfos[eventIndex]);
    bool done = (moduleInfos[0].moduleDataHeader == nullptr);
    const u32 *ptrToLastWord = reinterpret_cast<const u32 *>(this->singleStepState.eventIter.data
                                                             + this->singleStepState.eventIter.size);
    std::array<u32, MaxVMEModules> eventCountsByModule;
    eventCountsByModule.fill(0);

    while (!done)
    {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
        qDebug("%s eventIndex=%u: Begin Step 2 loop", __PRETTY_FUNCTION__, eventIndex);
#endif

        /* Early test to see if the first module still has a matching
         * header. This is done to avoid looping one additional time and
         * calling beginEvent()/endEvent() without any valid data available
         * for extractors to process. */
        if (this->doMultiEventProcessing[eventIndex]
            && !a2::data_filter::matches(moduleInfos[0].moduleHeaderFilter, *moduleInfos[0].moduleHeader))
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug("%s (early check): moduleHeader=0x%08x did not match header filter -> done processing event section.",
                   __PRETTY_FUNCTION__, *moduleInfos[0].moduleHeader);
#endif
            done = true;
            procState.stepResult = ProcessingState::StepResult_EventComplete;
            break;
        }

        if (this->analysis)
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug("%s analysis::beginEvent()", __PRETTY_FUNCTION__);
#endif
            this->analysis->beginEvent(eventIndex);
        }

        for (auto c: this->moduleConsumers)
        {
            c->beginEvent(eventIndex);
        }

        for (u32 moduleIndex = 0;
             moduleInfos[moduleIndex].moduleDataHeader;
             ++moduleIndex)
        {
            auto &mi(moduleInfos[moduleIndex]);
            Q_ASSERT(mi.moduleHeader);

            MesytecDiagnostics *diag = nullptr;

            if (this->diag)
            {
                this->diag->beginEvent(eventIndex);
            }

            procState.lastModuleDataSectionHeaderOffsets[moduleIndex] =
                mi.moduleDataHeader - procState.buffer->asU32(0);

            if (!this->doMultiEventProcessing[eventIndex])
            {
                // Do single event processing as multi event splitting is not
                // enabled for this event.
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                qDebug("%s eventIndex=%u, moduleIndex=%u: multi event disabled for event -> doing single event processing only",
                       __PRETTY_FUNCTION__, eventIndex, moduleIndex);
#endif

                this->counters.moduleCounters[eventIndex][moduleIndex]++;
                eventCountsByModule[moduleIndex]++;

                u32 moduleDataSize = lf.getModuleDataSize(*mi.moduleDataHeader);

                procState.lastModuleDataBeginOffsets[moduleIndex] =
                    mi.moduleHeader - procState.buffer->asU32(0);

                procState.lastModuleDataEndOffsets[moduleIndex] =
                    procState.lastModuleDataBeginOffsets[moduleIndex] + moduleDataSize;


                if (this->analysis)
                {
                    this->analysis->processModuleData(
                        eventIndex,
                        moduleIndex,
                        mi.moduleHeader,
                        moduleDataSize);
                }

                if (diag)
                {
                    diag->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, moduleDataSize);
                }

                for (auto c: this->moduleConsumers)
                {
                    c->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, moduleDataSize);
                }
            }
            // Multievent splitting is possible. Check for a header match and extract the data size.
            else if (a2::data_filter::matches(mi.moduleHeaderFilter, *mi.moduleHeader))
            {

                u32 moduleEventSize = a2::data_filter::extract(
                    mi.filterCacheModuleSectionSize, *mi.moduleHeader);

                if (unlikely(mi.moduleHeader + moduleEventSize + 1 > ptrToLastWord))
                {
                    this->counters.buffersWithErrors++;

                    QString msg = (QString("Error (mvme stream, buffer#%1): extracted module event size (%2) exceeds buffer size!"
                                           " eventIndex=%3, moduleIndex=%4, moduleHeader=0x%5, skipping event")
                                   .arg(bufferNumber)
                                   .arg(moduleEventSize)
                                   .arg(eventIndex)
                                   .arg(moduleIndex)
                                   .arg(*mi.moduleHeader, 8, 16, QLatin1Char('0'))
                                  );
                    logMessage(msg);

                    procState.stepResult = ProcessingState::StepResult_Error;
                    done = true;
                    break;
                }
                else
                {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                    qDebug("%s moduleIndex=%u, moduleHeader=0x%08x @%p, moduleEventSize=%u",
                           __PRETTY_FUNCTION__, moduleIndex, *mi.moduleHeader, mi.moduleHeader, moduleEventSize);
#endif

                    procState.lastModuleDataBeginOffsets[moduleIndex] =
                        mi.moduleHeader - procState.buffer->asU32(0);

                    procState.lastModuleDataEndOffsets[moduleIndex] =
                        procState.lastModuleDataBeginOffsets[moduleIndex] + moduleEventSize + 1;

                    this->counters.moduleCounters[eventIndex][moduleIndex]++;

                    if (this->analysis)
                    {
                        this->analysis->processModuleData(
                            eventIndex,
                            moduleIndex,
                            mi.moduleHeader,
                            moduleEventSize + 1);
                    }

                    if (diag)
                    {
                        diag->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, moduleEventSize + 1);
                    }

                    for (auto c: this->moduleConsumers)
                    {
                        c->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, moduleEventSize + 1);
                    }

                    // advance the moduleHeader by the event size
                    mi.moduleHeader += moduleEventSize + 1;
                    ++eventCountsByModule[moduleIndex];
                }
            }
            else
            {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                qDebug("%s moduleHeader=0x%08x did not match header filter -> done processing event section.",
                       __PRETTY_FUNCTION__, *mi.moduleHeader);
#endif
                // We're at a data word inside the first module for which the
                // header filter did not match. If the data is intact this
                // should happen for the first module, meaning moduleIndex==0.
                // At this point the same number of events have been processed
                // for all modules in this event section. Checks could be done
                // to make sure that all of the module header pointers now
                // point to either BerrMarker (in the case of vmusb) or
                // EndMarker. Also starting from the last modules pointer there
                // should be optional BerrMarkers and an EndMarker followed by
                // another EndMarker for the end of the event section we're in.
                // => Some checks are done below, outside the outer loop.

                done = true;
                procState.stepResult = ProcessingState::StepResult_EventComplete;
                break;
            }

            if (diag)
            {
                diag->endEvent(eventIndex);
            }
        } // end of module loop

        if (this->analysis)
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug("%s analysis::endEvent()", __PRETTY_FUNCTION__);
#endif
            this->analysis->endEvent(eventIndex);
        }

        for (auto c: this->moduleConsumers)
        {
            c->endEvent(eventIndex);
        }

        if (!this->doMultiEventProcessing[eventIndex])
        {
            // Single event processing -> the event has been completely processed
            procState.stepResult = ProcessingState::StepResult_EventComplete;
            this->singleStepState.eventIter = {}; // FIXME: "StopFromPausedStateBug"
        }
        else if (!a2::data_filter::matches(moduleInfos[0].moduleHeaderFilter, *moduleInfos[0].moduleHeader))
        {
            // multi event processing but the header filter does not match anymore
            procState.stepResult = ProcessingState::StepResult_EventComplete;
            this->singleStepState.eventIter = {}; // FIXME: "StopFromPausedStateBug"
        }
        else
        {
            procState.stepResult = ProcessingState::StepResult_EventHasMore;
        }

        break; // break after one step
    } // end of outer while (!done) loop
}

void MVMEStreamProcessorPrivate::logMessage(const QString &msg, bool useThrottle)
{
    if (!this->logger)
        return;

    if (!useThrottle)
    {
        qDebug() << msg;
        this->logger(msg);
    }
    else
    {
        // have to store this before the call to eventOverflows()
        size_t suppressedMessages = m_logThrottle.overflow();

        if (!m_logThrottle.eventOverflows())
        {
            if (unlikely(suppressedMessages))
            {
                auto finalMsg(QString("%1 (suppressed %2 earlier messages)")
                              .arg(msg)
                              .arg(suppressedMessages)
                             );
                qDebug() << finalMsg;
                this->logger(finalMsg);
            }
            else
            {
                qDebug() << msg;
                this->logger(msg);
            }
        }
    }
}

MVMEStreamProcessorCounters MVMEStreamProcessor::getCounters() const
{
    return m_d->counters;
}

MVMEStreamProcessorCounters &MVMEStreamProcessor::getCounters()
{
    return m_d->counters;
}

void MVMEStreamProcessor::attachDiagnostics(std::shared_ptr<MesytecDiagnostics> diag)
{
    assert(!m_d->diag);
    m_d->diag = diag;
}

void MVMEStreamProcessor::removeDiagnostics()
{
    assert(m_d->diag);
    m_d->diag.reset();
}

bool MVMEStreamProcessor::hasDiagnostics() const
{
    return (bool)m_d->diag;
}

void MVMEStreamProcessor::attachModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &c)
{
    m_d->moduleConsumers.push_back(c);
}

void MVMEStreamProcessor::removeModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &c)
{
    m_d->moduleConsumers.removeAll(c);
}

void MVMEStreamProcessor::attachBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &c)
{
    m_d->bufferConsumers.push_back(c);
}

void MVMEStreamProcessor::removeBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &c)
{
    m_d->bufferConsumers.removeAll(c);
}
