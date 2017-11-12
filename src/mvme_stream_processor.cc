#include "mvme_stream_processor.h"
#include "analysis/a2_adapter.h"
#include "analysis/analysis.h"
#include "databuffer.h"
#include "mesytec_diagnostics.h"
#include "mvme_listfile.h"
#include "util/perf.h"

//#define MVME_STREAM_PROCESSOR_DEBUG
//#define MVME_STREAM_PROCESSOR_DEBUG_BUFFERS

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

struct MVMEStreamProcessorPrivate
{
    MVMEStreamProcessorCounters counters = {};
    analysis::Analysis *analysis = nullptr;
    VMEConfig *vmeConfig = nullptr;
    std::shared_ptr<MesytecDiagnostics> diag;
    MVMEStreamProcessor::Logger logger = nullptr;

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

    QVector<IMVMEStreamConsumer *> consumers;
};

MVMEStreamProcessor::MVMEStreamProcessor()
    : m_d(std::make_unique<MVMEStreamProcessorPrivate>())
{
}

MVMEStreamProcessor::~MVMEStreamProcessor()
{
}

void MVMEStreamProcessor::MVMEStreamProcessor::beginRun(
    const RunInfo &runInfo, analysis::Analysis *analysis, VMEConfig *vmeConfig,
    u32 listfileVersion, Logger logger)
{
    if (listfileVersion == 0)
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

    m_d->counters = {};
    m_d->analysis = analysis;
    m_d->vmeConfig = vmeConfig;
    m_d->logger = logger;

    m_d->eventInfos.fill({}); // Full clear of the eventInfo cache
    m_d->eventConfigs.fill(nullptr);
    m_d->doMultiEventProcessing.fill(false); // FIXME: change default to true some point

    // build info for multievent processing
    auto eventConfigs = vmeConfig->getEventConfigs();

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

    // beginRun for consumers


    if (m_d->analysis)
    {
        const auto vmeMap = vme_analysis_common::build_id_to_index_mapping(vmeConfig);
        m_d->analysis->beginRun(runInfo, vmeMap);
    }

    if (m_d->diag)
    {
        m_d->diag->beginRun();
    }

    for (auto c: m_d->consumers)
    {
        c->beginRun(runInfo, vmeConfig);
    }
}

void MVMEStreamProcessor::endRun()
{
    for (auto c: m_d->consumers)
    {
        c->endRun();
    }
}

void MVMEStreamProcessor::processDataBuffer(DataBuffer *buffer)
{
    try
    {
        const auto bufferNumber = m_d->counters.buffersProcessed;

        BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align32);

#ifdef MVME_STREAM_PROCESSOR_DEBUG_BUFFERS
        logMessage(QString(">>> Begin mvme buffer #%1").arg(bufferNumber));

        logBuffer(iter, [this](const QString &str) { logMessage(str); });

        logMessage(QString("<<< End mvme buffer #%1") .arg(bufferNumber));
#endif

        while (iter.longwordsLeft())
        {
            u32 sectionHeader = iter.extractU32();
            int sectionType = (sectionHeader & m_d->SectionTypeMask) >> m_d->SectionTypeShift;
            u32 sectionSize = (sectionHeader & m_d->SectionSizeMask) >> m_d->SectionSizeShift;

            if (unlikely(sectionSize > iter.longwordsLeft()))
            {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                QString msg = (QString("Error (mvme fmt): extracted section size exceeds buffer size!"
                                       " mvme buffer #%1, sectionHeader=0x%2, sectionSize=%3, wordsLeftInBuffer=%4")
                               .arg(bufferNumber)
                               .arg(sectionHeader, 8, 16, QLatin1Char('0'))
                               .arg(sectionSize)
                               .arg(iter.longwordsLeft()));
                qDebug() << msg;
                logMessage(msg);
#endif
                throw end_of_buffer();
            }

            if (likely(sectionType == ListfileSections::SectionType_Event))
            {
                processEventSection(sectionHeader, iter.asU32(), sectionSize);
                iter.skip(sectionSize * sizeof(u32));
            }
            else if (sectionType == ListfileSections::SectionType_Timetick
                     && m_d->analysis)
            {
                // pass timeticks to the analysis
                Q_ASSERT(sectionSize == 0);
                m_d->analysis->processTimetick();
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
    catch (const end_of_buffer &)
    {
        // TODO: call endRun() for consumers here? pass error information to them?

        QString msg = QSL("Error (mvme fmt): unexpectedly reached end of buffer");
        qDebug() << msg;
        logMessage(msg);
        ++m_d->counters.buffersWithErrors;
        if (m_d->diag)
        {
            m_d->diag->beginRun();
        }
    }
}

void MVMEStreamProcessor::processEventSection(u32 sectionHeader, u32 *data, u32 size)
{
    ++m_d->counters.eventSections;
    u32 eventIndex = (sectionHeader & m_d->EventTypeMask) >> m_d->EventTypeShift;

    if (unlikely(eventIndex >= m_d->eventConfigs.size()))
    {
        ++m_d->counters.invalidEventIndices;
        qDebug() << __PRETTY_FUNCTION__ << "no event config for eventIndex = " << eventIndex
            << ", skipping input buffer";
        return;
    }

    ++m_d->counters.eventCounters[eventIndex];

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

        moduleInfos[moduleIndex].subEventHeader = eventIter.asU32();
        moduleInfos[moduleIndex].moduleHeader   = eventIter.asU32() + 1;
        // moduleHeader now points to the first module header in the event section

        u32 subEventHeader = eventIter.extractU32();
        u32 subEventSize   = (subEventHeader & m_d->SubEventSizeMask) >> m_d->SubEventSizeShift;
#ifdef MVME_STREAM_PROCESSOR_DEBUG
        qDebug("%s   eventIndex=%d, moduleIndex=%d, subEventSize=%u",
               __PRETTY_FUNCTION__, eventIndex, moduleIndex, subEventSize);
#endif
        // skip to the next subevent
        eventIter.skip(sizeof(u32), subEventSize);
    }

#ifdef MVME_STREAM_PROCESSOR_DEBUG
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
#ifdef MVME_STREAM_PROCESSOR_DEBUG
        qDebug("%s eventIndex=%u: Begin Step 2 loop", __PRETTY_FUNCTION__, eventIndex);
#endif

        /* Early test to see if the first module still has a matching
         * header. This is done to avoid looping one additional time and
         * calling beginEvent()/endEvent() without any valid data available
         * for extractors to process. */
        if (m_d->doMultiEventProcessing[eventIndex]
            && !a2::data_filter::matches(moduleInfos[0].moduleHeaderFilter, *moduleInfos[0].moduleHeader))
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug("%s (early check): moduleHeader=0x%08x did not match header filter -> done processing event section.",
                   __PRETTY_FUNCTION__, *moduleInfos[0].moduleHeader);
#endif
            done = true;
            break;
        }

        if (m_d->analysis)
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug("%s analysis::beginEvent()", __PRETTY_FUNCTION__);
#endif
            m_d->analysis->beginEvent(eventIndex, eventConfig->getId());
        }

        for (auto c: m_d->consumers)
        {
            c->beginEvent(eventIndex);
        }

        for (u32 moduleIndex = 0;
             moduleInfos[moduleIndex].subEventHeader;
             ++moduleIndex)
        {
            auto &mi(moduleInfos[moduleIndex]);
            Q_ASSERT(mi.moduleHeader);

            MesytecDiagnostics *diag = nullptr;

            // FIXME: why is this in here instead of one level up?
            if (m_d->diag)
            {
                m_d->diag->beginEvent(eventIndex);
            }

            if (!m_d->doMultiEventProcessing[eventIndex])
            {
                // Do single event processing as multi event splitting is not
                // enabled for this event.
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                qDebug("%s eventIndex=%u, moduleIndex=%u: multi event disabled for event -> doing single event processing only",
                       __PRETTY_FUNCTION__, eventIndex, moduleIndex);
#endif

                m_d->counters.moduleCounters[eventIndex][moduleIndex]++;
                eventCountsByModule[moduleIndex]++;

                u32 subEventSize = (*mi.subEventHeader & m_d->SubEventSizeMask) >> m_d->SubEventSizeShift;

                if (m_d->analysis)
                {
                    m_d->analysis->processModuleData(
                        eventIndex,
                        moduleIndex,
                        eventConfig->getId(),
                        mi.moduleConfig->getId(),
                        mi.moduleHeader,
                        subEventSize);
                }

                if (diag)
                {
                    diag->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, subEventSize);
                }

                for (auto c: m_d->consumers)
                {
                    c->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, subEventSize);
                }
            }
            // Multievent splitting is possible. Check for a header match and extract the data size.
            else if (a2::data_filter::matches(
                    mi.moduleHeaderFilter, *mi.moduleHeader))
            {

                u32 moduleEventSize = a2::data_filter::extract(
                    mi.filterCacheModuleSectionSize, *mi.moduleHeader);


                if (unlikely(mi.moduleHeader + moduleEventSize + 1 > ptrToLastWord))
                {
                    m_d->counters.buffersWithErrors++;

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
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                    qDebug("%s moduleIndex=%u, moduleHeader=0x%08x @%p, moduleEventSize=%u",
                           __PRETTY_FUNCTION__, moduleIndex, *mi.moduleHeader, mi.moduleHeader, moduleEventSize);
#endif

                    m_d->counters.moduleCounters[eventIndex][moduleIndex]++;

                    if (m_d->analysis)
                    {
                        m_d->analysis->processModuleData(
                            eventIndex,
                            moduleIndex,
                            eventConfig->getId(),
                            mi.moduleConfig->getId(),
                            mi.moduleHeader,
                            moduleEventSize + 1);
                    }

                    if (diag)
                    {
                        diag->processModuleData(eventIndex, moduleIndex, mi.moduleHeader, moduleEventSize + 1);
                    }

                    for (auto c: m_d->consumers)
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

#ifdef MVME_STREAM_PROCESSOR_DEBUG
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
                diag->endEvent(eventIndex);
            }
        }

        if (m_d->analysis)
        {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
            qDebug("%s analysis::endEvent()", __PRETTY_FUNCTION__);
#endif
            m_d->analysis->endEvent(eventIndex, eventConfig->getId());
        }

        for (auto c: m_d->consumers)
        {
            c->endEvent(eventIndex);
        }

        // Single event processing: terminate after one loop through the modules.
        if (!m_d->doMultiEventProcessing[eventIndex])
        {
            break;
        }
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

void MVMEStreamProcessor::logMessage(const QString &msg)
{
    if (m_d->logger)
    {
        m_d->logger(msg);
    }
}

const MVMEStreamProcessorCounters &MVMEStreamProcessor::getCounters() const
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

void MVMEStreamProcessor::attachConsumer(IMVMEStreamConsumer *c)
{
    m_d->consumers.push_back(c);
}

void MVMEStreamProcessor::removeConsumer(IMVMEStreamConsumer *c)
{
    m_d->consumers.removeAll(c);
}
