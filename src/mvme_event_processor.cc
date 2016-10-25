#include "mvme_event_processor.h"
#include "mvme_context.h"
#include "mvme_listfile.h"
#include "hist1d.h"

using namespace listfile;

struct MVMEEventProcessorPrivate
{
    MVMEContext *context;
    QList<QList<AnalysisConfig::DataFilterConfigList>> filterConfigs;
    QHash<DataFilterConfig *, QHash<int, Hist1D *>> histogramsByFilterConfig;
    QHash<DataFilterConfig *, QHash<int, s64>> valuesByFilterConfig;
    QHash<Hist2DConfig *, Hist2D *> hist2dByConfig;
    QHash<QUuid, DataFilterConfig *> filterConfigsById;
};

MVMEEventProcessor::MVMEEventProcessor(MVMEContext *context)
    : m_d(new MVMEEventProcessorPrivate)
{
    m_d->context = context;
}

MVMEEventProcessor::~MVMEEventProcessor()
{
    delete m_d;
}

void MVMEEventProcessor::newRun()
{
    m_d->filterConfigs.clear();
    m_d->histogramsByFilterConfig.clear();
    m_d->valuesByFilterConfig.clear();
    m_d->hist2dByConfig.clear();
    m_d->filterConfigsById.clear();

    auto analysisConfig = m_d->context->getAnalysisConfig();

    m_d->filterConfigs = analysisConfig->getFilters();

    for (auto hist1d: m_d->context->getObjects<Hist1D *>())
    {
        auto id = hist1d->property("DataFilterId").toUuid();
        auto address = hist1d->property("DataFilterAddress").toUInt();
        auto filterConfig = analysisConfig->findChildById<DataFilterConfig *>(id);
        if (filterConfig)
            m_d->histogramsByFilterConfig[filterConfig][address] = hist1d;
    }

    for (auto hist2dConfig: analysisConfig->get2DHistograms())
    {
        auto hist2d = qobject_cast<Hist2D *>(m_d->context->getMappedObject(hist2dConfig, QSL("ConfigToObject")));
        if (hist2d)
            m_d->hist2dByConfig[hist2dConfig] = hist2d;
    }

    for (auto filterConfig: analysisConfig->findChildren<DataFilterConfig *>())
    {
        m_d->filterConfigsById[filterConfig->getId()] = filterConfig;
    }
}

// Process an event buffer containing one or more events.
void MVMEEventProcessor::processDataBuffer(DataBuffer *buffer)
{
    auto &stats = m_d->context->getDAQStats();

    try
    {
        //qDebug() << __PRETTY_FUNCTION__ << buffer;
        ++stats.mvmeBuffersSeen;

        BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align32);

        while (iter.longwordsLeft())
        {
            u32 sectionHeader = iter.extractU32();
            int sectionType = (sectionHeader & SectionTypeMask) >> SectionTypeShift;
            u32 sectionSize = (sectionHeader & SectionSizeMask) >> SectionSizeShift;

            if (sectionType != SectionType_Event)
            {
                iter.skip(sectionSize * sizeof(u32));
                continue;
            }

            stats.addEventsRead(1);

            int eventIndex = (sectionHeader & EventTypeMask) >> EventTypeShift;
            u32 wordsLeftInSection = sectionSize;

            auto eventConfig = m_d->context->getConfig()->getEventConfig(eventIndex);
            if (eventConfig)
                ++stats.eventCounters[eventConfig].events;

            for (const auto &filterList: m_d->filterConfigs.value(eventIndex))
                for (auto filterConfig: filterList)
                    m_d->valuesByFilterConfig[filterConfig].clear();

            int moduleIndex = 0;

            while (wordsLeftInSection > 1)
            {
                u8 *oldBufferP = iter.buffp;
                u32 subEventHeader = iter.extractU32();
                u32 subEventSize = (subEventHeader & SubEventSizeMask) >> SubEventSizeShift;
                auto moduleType  = static_cast<VMEModuleType>((subEventHeader & ModuleTypeMask) >> ModuleTypeShift);

                const auto filterConfigs = m_d->filterConfigs.value(eventIndex).value(moduleIndex);

                for (u32 i=0; i<subEventSize-1; ++i)
                {
                    u32 currentWord = iter.extractU32();

                    for (auto filterConfig: filterConfigs)
                    {
                        if (filterConfig->getFilter().matches(currentWord))
                        {
                            u32 address = filterConfig->getFilter().extractData(currentWord, 'A');
                            u32 data    = filterConfig->getFilter().extractData(currentWord, 'D');
                            auto histo  = m_d->histogramsByFilterConfig[filterConfig].value(address);
                            if (histo)
                                histo->inc(data);
                            m_d->valuesByFilterConfig[filterConfig][address] = data;
                        }
                    }
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
                    emit bufferProcessed(buffer);
                    return;
                }

                u8 *newBufferP = iter.buffp;
                wordsLeftInSection -= (newBufferP - oldBufferP) / sizeof(u32);
                ++moduleIndex;
            }

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
                emit bufferProcessed(buffer);
                return;
            }

            //
            // fill 2D Histograms
            //
            for (auto hist2dConfig: m_d->hist2dByConfig.keys())
            {
                auto hist2d    = m_d->hist2dByConfig[hist2dConfig];
                auto xFilterId = hist2dConfig->getXFilterId();
                auto xFilter   = m_d->filterConfigsById.value(xFilterId, nullptr);
                auto xAddress  = hist2dConfig->getXFilterAddress();
                auto yFilterId = hist2dConfig->getYFilterId();
                auto yFilter   = m_d->filterConfigsById.value(yFilterId, nullptr);
                auto yAddress  = hist2dConfig->getYFilterAddress();

                s64 xValue = m_d->valuesByFilterConfig[xFilter].value(xAddress, -1);
                s64 yValue = m_d->valuesByFilterConfig[yFilter].value(yAddress, -1);


                if (hist2d && xValue >= 0 && yValue >= 0)
                {
                    int shiftX = 0;
                    int shiftY = 0;

                    {
                        int dataBits  = xFilter->getFilter().getExtractBits('D');
                        int histoBits = hist2d->getXBits();
                        shiftX = std::min(dataBits - histoBits, 0);
                    }

                    {
                        int dataBits  = yFilter->getFilter().getExtractBits('D');
                        int histoBits = hist2d->getYBits();
                        shiftY = std::min(dataBits - histoBits, 0);
                    }

                    hist2d->fill(xValue >> shiftX, yValue >> shiftY);
                }
            }
        }
    } catch (const end_of_buffer &)
    {
        emit logMessage(QString("Error: unexpectedly reached end of buffer"));
        ++stats.mvmeBuffersWithErrors;
    }


    emit bufferProcessed(buffer);
}

#if 0
                {




                    ModuleConfig *cfg = m_d->context->getConfig()->getModuleConfig(eventIndex, subEventIndex);

                    if (cfg)
                    {
                        ++stats.eventCounters[cfg].events;
                    }

                    if (isMesytecModule(moduleType) && cfg)
                    {
                        HistogramCollection *histo = m_mod2hist.value(cfg);

                        const u32 dataExtractMask = cfg->getDataExtractMask();
                        const int dataBits = cfg->getDataBits();
                        int histoShift = dataBits - RawHistogramBits;
                        if (histoShift < 0)
                            histoShift = 0;

                        for (u32 i=0; i<subEventSize-1; ++i)
                        {
                            u32 currentWord = iter.extractU32();

                            if (currentWord == 0xFFFFFFFF || currentWord == 0x00000000)
                                continue;

                            bool header_found_flag = (currentWord & 0xC0000000) == 0x40000000;
                            bool data_found_flag = ((currentWord & 0xF0000000) == 0x10000000) // MDPP
                                || ((currentWord & 0xFF800000) == 0x04000000); // MxDC
                            bool eoe_found_flag = (currentWord & 0xC0000000) == 0xC0000000;

                            if (header_found_flag)
                            {
                                ++stats.eventCounters[cfg].headerWords;
                            }

                            if (data_found_flag)
                            {
                                ++stats.eventCounters[cfg].dataWords;

                                u16 address = (currentWord & 0x003F0000) >> 16; // 6 bit address
                                u32 value   = (currentWord & dataExtractMask);

                                if (histo)
                                {
                                    histo->incValue(address, value >> histoShift);
                                }

                                if (subEventIndex < SUBEVENT_MAX && address < ADDRESS_MAX)
                                {
                                    //if (eventValues[subEventIndex][address] >= 0)
                                    //{
                                    //    qDebug() << "eventvalues overwrite!";
                                    //}

                                    eventValues[subEventIndex][address] = value;
                                }
                                else
                                {
                                    emit logMessage(QString("subEventIndex %1 or address %2 out of range")
                                                    .arg(subEventIndex)
                                                    .arg(address));
                                }
                            }

                            if (eoe_found_flag)
                            {
                                ++stats.eventCounters[cfg].eoeWords;
                            }
                        }

                        u32 nextWord = iter.peekU32();
                        if (nextWord == EndMarker)
                        {
                            iter.extractU32();
                        }
                        else
                        {
                            emit logMessage(QString("Error: did not find marker at end of subevent section "
                                                    "(eventIndex=%1, subEventIndex=%2)")
                                            .arg(eventIndex)
                                            .arg(subEventIndex)
                                           );
                            emit bufferProcessed(buffer);
                            return;
                        }
                    }
                    else
                    {
                        if (!isMesytecModule(moduleType))
                        {
#if 0
                            emit logMessage(QString("Skipping subevent of size %1 (module type=%2, not a mesytec module)")
                                            .arg(subEventSize)
                                            .arg(static_cast<int>(moduleType))
                                           );
#endif
                        }
                        else if (!cfg)
                        {
                            emit logMessage(QString("Skipping subevent of size %1 (no module config found)")
                                            .arg(subEventSize));
                        }
                        iter.skip(subEventSize * sizeof(u32));
                    }
                }
#endif
