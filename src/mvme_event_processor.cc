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

/* Maybe FIXME: With DualWordDataFilters: if a module does not appear in a
 * certain event I still swap the current and last value vectors and then clear
 * the new current vector. This means the actual last value of the module is
 * lost. It should be kept around if the module did not appear in the event and
 * used once the module appears again. I think. Maybe. */

using namespace listfile;

struct MVMEEventProcessorPrivate
{
    MVMEContext *context = nullptr;

    QMap<int, QMap<int, DataFilterConfigList>> filterConfigs; // eventIdx -> moduleIdx -> filterList
    QHash<DataFilterConfig *, QHash<int, Hist1D *>> histogramsByFilterConfig; // filter -> address -> histo
    QHash<DataFilterConfig *, QHash<int, QVector<u32>>> valuesByFilterConfig; // filter -> address -> list of values
    QHash<Hist2DConfig *, Hist2D *> hist2dByConfig;
    QHash<QUuid, DataFilterConfig *> filterConfigsById;

    QMap<int, QMap<int, DualWordDataFilterConfigList>> dualWordFilterConfigs; // eventIdx -> moduleIdx -> filterList
    QHash<DualWordDataFilterConfig *, Hist1D *> histogramsByDualWordDataFilterConfig;
    DualWordFilterValues currentDualWordFilterValues;
    DualWordFilterValues lastDualWordFilterValues;
    DualWordFilterDiffs dualWordFilterDiffs;
    QMutex dualWordFilterValuesLock;

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

DualWordFilterValues MVMEEventProcessor::getDualWordFilterValues() const
{
    QMutexLocker locker(&m_d->dualWordFilterValuesLock);

    DualWordFilterValues result;

    // create a deep copy of the values hash
    for (auto it = m_d->currentDualWordFilterValues.begin();
         it != m_d->currentDualWordFilterValues.end();
        ++it)
    {
        const auto &values = it.value();
        auto &dest = result[it.key()];
        std::copy(values.begin(), values.end(), std::back_inserter(dest));
    }

    return result;
}

DualWordFilterDiffs MVMEEventProcessor::getDualWordFilterDiffs() const
{
    QMutexLocker locker(&m_d->dualWordFilterValuesLock);

    DualWordFilterDiffs result;

    // create a deep copy of the values hash
    for (auto it = m_d->dualWordFilterDiffs.begin();
         it != m_d->dualWordFilterDiffs.end();
        ++it)
    {
        const auto &values = it.value();
        auto &dest = result[it.key()];
        std::copy(values.begin(), values.end(), std::back_inserter(dest));
    }

    return result;
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
        // TODO: don't use the configId property here! instead use context object mappings (ObjectToConfig)!
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

    {
        //
        // DualWordDataFilters
        //

        QMutexLocker dualWordLocker(&m_d->dualWordFilterValuesLock);

        m_d->dualWordFilterConfigs = analysisConfig->getDualWordFilters();
        m_d->histogramsByDualWordDataFilterConfig.clear();
        m_d->currentDualWordFilterValues.clear();
        m_d->lastDualWordFilterValues.clear();
        m_d->dualWordFilterDiffs.clear();

        for (auto histoConfig: analysisConfig->findChildren<Hist1DConfig *>())
        {
            auto filterConfig = analysisConfig->findChildById<DualWordDataFilterConfig *>(histoConfig->getFilterId());

            if (filterConfig)
            {
                auto histo = qobject_cast<Hist1D *>(m_d->context->getObjectForConfig(histoConfig));

                if (histo)
                {
                    m_d->histogramsByDualWordDataFilterConfig[filterConfig] = histo;
                }
            }
        }
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

            {
                /* DualWordDataFilters: swap current and last values for each
                 * module for the current eventIndex, then clear the new
                 * current values array.  */
                QMutexLocker locker(&m_d->dualWordFilterValuesLock);
                const auto &filterMap = m_d->dualWordFilterConfigs.value(eventIndex);
                for (auto it=filterMap.begin(); it!=filterMap.end(); ++it)
                {
                    for (const auto filterConfig: it.value())
                    {
                        std::swap(m_d->currentDualWordFilterValues[filterConfig],
                                  m_d->lastDualWordFilterValues[filterConfig]);
                        m_d->currentDualWordFilterValues[filterConfig].clear();
                        m_d->dualWordFilterDiffs[filterConfig].clear();
                    }
                }
            }

            int moduleIndex = 0;

            // start of event section

            while (wordsLeftInSection > 1)
            {
                u8 *oldBufferP = iter.buffp;
                u32 subEventHeader = iter.extractU32();
                u32 subEventSize = (subEventHeader & SubEventSizeMask) >> SubEventSizeShift;
                auto moduleType  = static_cast<VMEModuleType>((subEventHeader & ModuleTypeMask) >> ModuleTypeShift);


                MesytecDiagnostics *diag = nullptr;
                if (m_d->diag && m_d->diag->getEventIndex() == eventIndex && m_d->diag->getModuleIndex() == moduleIndex)
                {
                    diag = m_d->diag;
                    diag->beginEvent();
                }

                const auto filterConfigs = m_d->filterConfigs.value(eventIndex).value(moduleIndex);
                const auto dualWordfilterConfigs = m_d->dualWordFilterConfigs.value(eventIndex).value(moduleIndex);

                if (!m_d->dualWordFilterConfigs.isEmpty())
                {
                    QMutexLocker locker(&m_d->dualWordFilterValuesLock);
                    for (auto filterConfig: dualWordfilterConfigs)
                    {
                        filterConfig->getFilter().clearCompletion();
                    }
                }

                s32 wordIndexInSubEvent = 0;

                for (u32 i=0; i<subEventSize-1; ++i, ++wordIndexInSubEvent)
                {
                    u32 currentWord = iter.extractU32();

                    /* Do not pass BerrMarker words to the analysis if and only
                     * if they are the last word in the subevent.
                     * There is still the possibilty of missing the actual last
                     * word of the readout if that last word is the same as
                     * BerrMarker and the readout did not actually result in a
                     * BERR on the bus. */
                    if ((i == subEventSize-2) && currentWord == BerrMarker)
                        continue;

                    for (auto filterConfig: filterConfigs)
                    {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                        qEPDebug() << __PRETTY_FUNCTION__ << "trying filter" << filterConfig
                            << filterConfig->getFilter().toString()
                            << "current word" << hex << currentWord << dec;
#endif

                        if (filterConfig->getFilter().matches(currentWord, wordIndexInSubEvent))
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

                    for (auto filterConfig: dualWordfilterConfigs)
                    {
                        auto &filter(filterConfig->getFilter());
                        filter.handleDataWord(currentWord, wordIndexInSubEvent);

                        if (filter.isComplete())
                        {
                            QMutexLocker locker(&m_d->dualWordFilterValuesLock);
                            m_d->currentDualWordFilterValues[filterConfig].push_back(filter.getResult());
                        }
                    }

                    if (diag)
                    {
                        diag->handleDataWord(currentWord);
                    }

                    if (moduleType == VMEModuleType::MesytecCounter)
                    {
                        emit logMessage(QString("CounterWord %1: 0x%2")
                                        .arg(wordIndexInSubEvent)
                                        .arg(currentWord, 8, 16, QLatin1Char('0')));

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
                        for (auto i=0; i<xValues.size(); ++i)
                        {
                            auto xValue = xValues[i];
                            auto yValue = yValues[i];

                            if (xValue >= xOffset && yValue >= yOffset)
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

            //
            // Calculate dual word filter differences and fill related 1d histograms
            //
            {
                QMutexLocker locker(&m_d->dualWordFilterValuesLock);

                for (auto it = m_d->histogramsByDualWordDataFilterConfig.begin();
                     it != m_d->histogramsByDualWordDataFilterConfig.end();
                     ++it)
                {
                    const auto &filterConfig = it.key();
                    const auto &histo = it.value();

                    const auto &currentValues(m_d->currentDualWordFilterValues[filterConfig]);
                    const auto &lastValues(m_d->lastDualWordFilterValues[filterConfig]);

                    if (!currentValues.isEmpty() && (currentValues.size() == lastValues.size()))
                    {
                        for (int i=0; i<currentValues.size(); ++i)
                        {
                            // TODO: add a way to handle negative results here!
                            s64 diff = currentValues[i] - lastValues[i];
                            histo->fill(diff);

                            double ddiff = static_cast<double>(currentValues[i]) - static_cast<double>(lastValues[i]);

                            m_d->dualWordFilterDiffs[filterConfig].push_back(ddiff);
                            qDebug() << filterConfig << m_d->dualWordFilterDiffs[filterConfig].size() << currentValues.size() << lastValues.size();
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
