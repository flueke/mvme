#include "mvme_event_processor.h"
#include "mvme_context.h"
#include "mvme_listfile.h"
#include "hist1d.h"
#include "hist2d.h"
#include "mesytec_diagnostics.h"

//#define MVME_EVENT_PROCESSOR_DEBUGGING

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
    inline QDebug qEPDebug() { return QDebug(QtDebugMsg); }
#else
    inline QNoDebug qEPDebug() { return QNoDebug(); }
#endif

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
    QHash<DualWordDataFilterConfig *, QPair<Hist1D *, Hist1DConfig *>> histogramsByDualWordDataFilterConfig;

    QHash<DualWordDataFilterConfig *, QPair<u64, bool>> currentDualWordFilterValues;
    QHash<DualWordDataFilterConfig *, QPair<u64, bool>> lastDualWordFilterValues;
    QHash<DualWordDataFilterConfig *, QPair<double, bool>> dualWordFilterDiffs;
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
        const auto &value(it.value());

        if (value.second)
        {
            result[it.key()] = value.first;
        }
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
        const auto &value(it.value());

        if (value.second)
        {
            result[it.key()] = value.first;
        }
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
        if (auto histoConfig = qobject_cast<Hist1DConfig *>(m_d->context->getConfigForObject(histo)))
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
        if (auto histoConfig = qobject_cast<Hist2DConfig *>(m_d->context->getConfigForObject(histo)))
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
                    m_d->histogramsByDualWordDataFilterConfig[filterConfig] = qMakePair(histo, histoConfig);
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
                 * current value.  */
                QMutexLocker locker(&m_d->dualWordFilterValuesLock);
                const auto &filterMap = m_d->dualWordFilterConfigs.value(eventIndex);
                for (auto it=filterMap.begin(); it!=filterMap.end(); ++it)
                {
                    for (const auto filterConfig: it.value())
                    {
                        std::swap(m_d->currentDualWordFilterValues[filterConfig],
                                  m_d->lastDualWordFilterValues[filterConfig]);

                        m_d->currentDualWordFilterValues[filterConfig].second = false;
                        m_d->dualWordFilterDiffs[filterConfig].second = false;
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

                for (auto filterConfig: dualWordfilterConfigs)
                {
                    filterConfig->getFilter().clearCompletion();
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

                    for (auto filterConfig: filterConfigs)
                    {
#if 0 //#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
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
#if 0 //#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                                qEPDebug() << __PRETTY_FUNCTION__ << "fill" << histo << data;
#endif
                                histo->fill(data);
                            }
                            else
                            {
#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                                qEPDebug() << __PRETTY_FUNCTION__ << "filter matched but found no histo!";
#endif
                            }
                            m_d->valuesByFilterConfig[filterConfig][address].push_back(data);
                        }
                    }

                    for (auto filterConfig: dualWordfilterConfigs)
                    {
                        /* Note: This can break if a dual word filter has one
                         * of its' filter words consist only of X'es (meaning
                         * the word does match any input) but the subevent
                         * consists of only one word, then the filter will
                         * never be marked as complete. */
                        auto &filter(filterConfig->getFilter());
                        filter.handleDataWord(currentWord, wordIndexInSubEvent);

                        if (filter.isComplete())
                        {
                            QMutexLocker locker(&m_d->dualWordFilterValuesLock);
                            auto &pair(m_d->currentDualWordFilterValues[filterConfig]);
                            pair.first = filter.getResult();
                            pair.second = true;
                            filter.clearCompletion();
                        }
                    }

                    if (diag)
                    {
                        diag->handleDataWord(currentWord);
                    }

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
                }


#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                for (auto filterConfig: dualWordfilterConfigs)
                {
                    if (m_d->currentDualWordFilterValues.contains(filterConfig))
                    {
                        qDebug() << filterConfig << m_d->currentDualWordFilterValues[filterConfig];
                    }
                }
#endif

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

                const auto &filtersByModule(m_d->dualWordFilterConfigs[eventIndex]);

                for (auto jt = filtersByModule.begin();
                     jt != filtersByModule.end();
                     ++jt)
                {
                    const auto &filters(jt.value());

                    for (auto filterConfig: filters)
                    {
                        auto histoPair = m_d->histogramsByDualWordDataFilterConfig.value(filterConfig);
                        auto histo = histoPair.first;
                        auto histoConfig = histoPair.second;
                        u32  histoShift = histoConfig->getShift();

                        if (histo)
                        {
                            const auto &currentValue(m_d->currentDualWordFilterValues[filterConfig]);
                            const auto &lastValue(m_d->lastDualWordFilterValues[filterConfig]);

                            if (currentValue.second && lastValue.second)
                            {
                                // TODO: add a way to handle negative results here!
                                // also results that are out of range (double values for histos needed)
                                s64 diff = currentValue.first - lastValue.first;

                                if (diff >= 0)
                                {
                                    if (histoShift != 0)
                                    {
                                        diff /= std::pow(2.0, histoShift);
                                    }
                                    histo->fill(diff);

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
                                    qDebug() << "histo fill value =" << diff
                                        << ", #values =" << currentValues.size()
                                        << ", filterConfig =" << filterConfig
                                        << ", event =" << eventIndex
                                        << ", module =" << moduleIndex
                                        ;
#endif
                                }

                                double ddiff = static_cast<double>(currentValue.first) - static_cast<double>(lastValue.first);

                                m_d->dualWordFilterDiffs[filterConfig].first = ddiff;
                                m_d->dualWordFilterDiffs[filterConfig].second = true;
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

#ifdef MVME_EVENT_PROCESSOR_DEBUGGING
    qDebug() << __PRETTY_FUNCTION__ << "end processing" << buffer;
#endif

    emit bufferProcessed(buffer);

    {
        QMutexLocker locker(&m_d->isProcessingMutex);
        m_d->isProcessingBuffer = false;
    }
}
