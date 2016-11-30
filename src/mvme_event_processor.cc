#include "mvme_event_processor.h"
#include "mvme_context.h"
#include "mvme_listfile.h"
#include "hist1d.h"
#include "hist2d.h"
#include "mesytec_diagnostics.h"

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
    inline QDebug qEPDebug() { return QDebug(QtDebugMsg); }
#else
    inline QNoDebug qEPDebug() { return QNoDebug(); }
#endif

using namespace listfile;

using DataFilterConfigList = AnalysisConfig::DataFilterConfigList;

struct MVMEEventProcessorPrivate
{
    MVMEContext *context;
    QMap<int, QMap<int, DataFilterConfigList>> filterConfigs; // eventIdx -> moduleIdx -> filterList
    QHash<DataFilterConfig *, QHash<int, Hist1D *>> histogramsByFilterConfig; // filter -> address -> histo
    QHash<DataFilterConfig *, QHash<int, QVector<u32>>> valuesByFilterConfig; // filter -> address -> list of values
    QHash<Hist2DConfig *, Hist2D *> hist2dByConfig;
    QHash<QUuid, DataFilterConfig *> filterConfigsById;
    MesytecDiagnostics *diag = nullptr;
    bool isProcessingBuffer = false;
    QMutex isProcessingMutex;
};

MVMEEventProcessor::MVMEEventProcessor(MVMEContext *context)
    : m_d(new MVMEEventProcessorPrivate)
{
    m_d->context = context;
}

MVMEEventProcessor::~MVMEEventProcessor()
{
    delete m_d->diag;
    delete m_d;
}

bool MVMEEventProcessor::isProcessingBuffer() const
{
    QMutexLocker locker(&m_d->isProcessingMutex);
    return m_d->isProcessingBuffer;
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

void MVMEEventProcessor::newRun()
{
    m_d->filterConfigs.clear();
    m_d->histogramsByFilterConfig.clear();
    m_d->valuesByFilterConfig.clear();
    m_d->hist2dByConfig.clear();
    m_d->filterConfigsById.clear();
    if (m_d->diag)
        m_d->diag->reset();

    auto analysisConfig = m_d->context->getAnalysisConfig();

    m_d->filterConfigs = analysisConfig->getFilters();

    for (auto histo: m_d->context->getObjects<Hist1D *>())
    {
        if (auto histoConfig = analysisConfig->findChildById<Hist1DConfig *>(
            histo->property("configId").toUuid()))
        {
            if (auto filterConfig = analysisConfig->findChildById<DataFilterConfig *>(
                    histoConfig->getFilterId()))
            {
                auto address = histoConfig->getFilterAddress();

                qEPDebug() << __PRETTY_FUNCTION__
                    << "filter:" << filterConfig << address
                    << "histo:" << histo;

                m_d->histogramsByFilterConfig[filterConfig][address] = histo;
            }
        }
    }

    for (auto histo: m_d->context->getObjects<Hist2D *>())
    {
        if (auto histoConfig = analysisConfig->findChildById<Hist2DConfig *>(
            histo->property("configId").toUuid()))
        {
            qEPDebug() << __PRETTY_FUNCTION__
                << "hist2d:" << histoConfig << histo;

            m_d->hist2dByConfig[histoConfig] = histo;
        }
    }

    for (auto filterConfig: analysisConfig->findChildren<DataFilterConfig *>())
    {
        auto id = filterConfig->getId();
        qEPDebug() << __PRETTY_FUNCTION__ << "filterById:" << id << filterConfig;
        m_d->filterConfigsById[id] = filterConfig;
    }
}

// Process an event buffer containing one or more events.
void MVMEEventProcessor::processDataBuffer(DataBuffer *buffer)
{
    {
        QMutexLocker locker(&m_d->isProcessingMutex);
        m_d->isProcessingBuffer = true;
    }

    auto &stats = m_d->context->getDAQStats();

    try
    {
        //qEPDebug() << __PRETTY_FUNCTION__ << buffer;
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

            {
                // clears the values for the current eventIndex
                // FIXME: slow!
                const auto &filterMap = m_d->filterConfigs.value(eventIndex);

                for (auto it=filterMap.begin(); it!=filterMap.end(); ++it)
                {
                    for (const auto filterConfig: it.value())
                    {
                        auto &valueHash = m_d->valuesByFilterConfig[filterConfig];

                        for (auto jt=valueHash.begin(); jt!=valueHash.end(); ++jt)
                        {
                            jt.value().clear();
                        }
                    }
                }
            }

            int moduleIndex = 0;

            while (wordsLeftInSection > 1)
            {
                u8 *oldBufferP = iter.buffp;
                u32 subEventHeader = iter.extractU32();
                u32 subEventSize = (subEventHeader & SubEventSizeMask) >> SubEventSizeShift;
                auto moduleType  = static_cast<VMEModuleType>((subEventHeader & ModuleTypeMask) >> ModuleTypeShift);

                const auto filterConfigs = m_d->filterConfigs.value(eventIndex).value(moduleIndex);

                MesytecDiagnostics *diag = nullptr;
                if (m_d->diag && m_d->diag->getEventIndex() == eventIndex && m_d->diag->getModuleIndex() == moduleIndex)
                {
                    diag = m_d->diag;
                    diag->beginEvent();
                }

                for (u32 i=0; i<subEventSize-1; ++i)
                {
                    u32 currentWord = iter.extractU32();

                    for (auto filterConfig: filterConfigs)
                    {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                        qEPDebug() << __PRETTY_FUNCTION__ << "trying filter" << filterConfig
                            << filterConfig->getFilter().toString()
                            << "current word" << hex << currentWord << dec;
#endif

                        if (filterConfig->getFilter().matches(currentWord))
                        {
                            u32 address = filterConfig->getFilter().extractData(currentWord, 'A');
                            u32 data    = filterConfig->getFilter().extractData(currentWord, 'D');
                            auto histo  = m_d->histogramsByFilterConfig[filterConfig].value(address);
                            if (histo)
                            {
                                qEPDebug() << __PRETTY_FUNCTION__ << "fill" << histo << data;
                                histo->fill(data);
                            }
                            else
                            {
                                qEPDebug() << __PRETTY_FUNCTION__ << "filter matched but found no histo!";
                            }
                            m_d->valuesByFilterConfig[filterConfig][address].push_back(data);
                        }
                    }

                    if (diag)
                    {
                        diag->handleDataWord(currentWord);
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
            for (const auto hist2dConfig: m_d->hist2dByConfig.keys())
            {
                auto hist2d    = m_d->hist2dByConfig[hist2dConfig];

                auto xFilterId = hist2dConfig->getFilterId(Qt::XAxis);
                auto xFilter   = m_d->filterConfigsById.value(xFilterId, nullptr);
                auto xAddress  = hist2dConfig->getFilterAddress(Qt::XAxis);

                auto yFilterId = hist2dConfig->getFilterId(Qt::YAxis);
                auto yFilter   = m_d->filterConfigsById.value(yFilterId, nullptr);
                auto yAddress  = hist2dConfig->getFilterAddress(Qt::YAxis);

                const u32 xOffset     = hist2dConfig->getOffset(Qt::XAxis);
                const u32 xShift      = hist2dConfig->getShift(Qt::XAxis);
                const u32 yOffset     = hist2dConfig->getOffset(Qt::YAxis);
                const u32 yShift      = hist2dConfig->getShift(Qt::YAxis);

                if (hist2d && xFilter && yFilter)
                {
                    const auto &xValues = m_d->valuesByFilterConfig[xFilter][xAddress];
                    const auto &yValues = m_d->valuesByFilterConfig[yFilter][yAddress];

                    if (xValues.size() > 0 && xValues.size() == yValues.size())
                    {
                        //int shiftX = 0;
                        //int shiftY = 0;

                        //{
                        //    int dataBits  = xFilter->getFilter().getExtractBits('D');
                        //    int histoBits = hist2d->getXBits();
                        //    //shiftX = std::max(dataBits - histoBits, 0);
                        //    shiftX = std::max(dataBits - xBinBits, 0);
                        //}

                        //{
                        //    int dataBits  = yFilter->getFilter().getExtractBits('D');
                        //    int histoBits = hist2d->getYBits();
                        //    //shiftY = std::max(dataBits - histoBits, 0);
                        //    shiftY = std::max(dataBits - yBinBits, 0);
                        //}

                        for (auto i=0; i<xValues.size(); ++i)
                        {
                            auto xValue = xValues[i];
                            auto yValue = yValues[i];

#if 0
                            if (xOffset != 0)
                            {
                                qDebug()
                                    << "xOffset" << xOffset
                                    << "xShift" << xShift
                                    << "xValue" << xValue;
                            }

                            if (yOffset != 0)
                            {
                                qDebug()
                                    << "yOffset" << yOffset
                                    << "yShift" << yShift
                                    << "yValue" << yValue;
                            }
#endif

                            if (xOffset <= xValue && yOffset <= yValue)
                            {
                                xValue -= xOffset;
                                yValue -= yOffset;

                                xValue >>= xShift;
                                yValue >>= yShift;

                                hist2d->fill(xValue, yValue);
                            }
                        }
                    }
                }
            }
        }
    } catch (const end_of_buffer &)
    {
        emit logMessage(QString("Error: unexpectedly reached end of buffer"));
        ++stats.mvmeBuffersWithErrors;
    }


    emit bufferProcessed(buffer);

    {
        QMutexLocker locker(&m_d->isProcessingMutex);
        m_d->isProcessingBuffer = false;
    }
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
                                    //    qEPDebug() << "eventvalues overwrite!";
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
