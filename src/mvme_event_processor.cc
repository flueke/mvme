#include "mvme_event_processor.h"
#include "mvme_context.h"
#include "mvme_listfile.h"
#include "histogram.h"

using namespace listfile;

MVMEEventProcessor::MVMEEventProcessor(MVMEContext *context)
    : m_context(context)
{
}

void MVMEEventProcessor::newRun()
{
    m_mod2hist.clear();

    for (auto mod: m_context->getConfig()->getAllModuleConfigs())
    {
        for (auto hist: m_context->getObjects<HistogramCollection *>())
        {
            auto sourceId = QUuid(hist->property("Histogram.sourceModule").toString());
            if (sourceId == mod->getId())
            {
                m_mod2hist[mod] = hist;
            }
        }
    }
}

// Process an event buffer containing one or more events.
void MVMEEventProcessor::processEventBuffer(DataBuffer *buffer)
{
    auto &stats = m_context->getDAQStats();

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

            int eventType = (sectionHeader & EventTypeMask) >> EventTypeShift;
            u32 wordsLeftInSection = sectionSize;
            int subEventIndex = 0;

            auto eventConfig = m_context->getConfig()->getEventConfig(eventType);

            if (eventConfig)
            {
                ++stats.eventCounters[eventConfig].events;
            }

            // subeventindex -> address -> value
            // FIXME: does not work if the section contains multiple events for the
            // same module as values will get overwritten (max_transfer_data > 1).
            // But in that case events do not match up anyways. Not sure what to do...
            //QHash<int, QHash<int, u32>> eventValues;
#define SUBEVENT_MAX 16
#define ADDRESS_MAX 40
            s64 eventValues[SUBEVENT_MAX][ADDRESS_MAX];

            //qDebug() << "sizeof eventvalues =" << sizeof(eventValues);

            for (size_t i=0; i<SUBEVENT_MAX; ++i)
                for (size_t j=0; j<ADDRESS_MAX; ++j)
                    eventValues[i][j] = -1;

            while (wordsLeftInSection > 1)
            {
                u8 *oldBufferP = iter.buffp;

                {

                    u32 subEventHeader = iter.extractU32();
                    u32 subEventSize = (subEventHeader & SubEventSizeMask) >> SubEventSizeShift;
                    auto moduleType  = static_cast<VMEModuleType>((subEventHeader & ModuleTypeMask) >> ModuleTypeShift);
                    ModuleConfig *cfg = m_context->getConfig()->getModuleConfig(eventType, subEventIndex);

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
                                            .arg(eventType)
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

                u8 *newBufferP = iter.buffp;
                wordsLeftInSection -= (newBufferP - oldBufferP) / sizeof(u32);
                ++subEventIndex;
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
                                .arg(eventType)
                               );
                emit bufferProcessed(buffer);
                return;
            }

            //
            // fill 2D Histograms
            //
            for (auto hist2d: m_context->getObjects<Hist2D *>())
            {
                // TODO: store this differently (maybe Pair<ModuleId, Address>)
                bool ok1, ok2, ok3;
                QString sourcePath = hist2d->property("Hist2D.xAxisSource").toString();
                int eventIndex   = sourcePath.section('.', 0, 0).toInt(&ok1);
                int moduleIndex  = sourcePath.section('.', 1, 1).toInt(&ok2);
                int addressValue = sourcePath.section('.', 2, 2).toInt(&ok3);

                if (eventIndex != eventType || !(ok1 && ok2 && ok3))
                    continue;

                auto moduleX = m_context->getConfig()->getModuleConfig(eventIndex, moduleIndex);

                if (!moduleX)
                    continue;

                bool xFound = (moduleIndex < SUBEVENT_MAX
                               && addressValue < ADDRESS_MAX
                               && eventValues[moduleIndex][addressValue] >= 0);

                u32 xValue = xFound ? static_cast<u32>(eventValues[moduleIndex][addressValue]) : 0;

                sourcePath = hist2d->property("Hist2D.yAxisSource").toString();
                eventIndex   = sourcePath.section('.', 0, 0).toInt(&ok1);
                moduleIndex  = sourcePath.section('.', 1, 1).toInt(&ok2);
                addressValue = sourcePath.section('.', 2, 2).toInt(&ok3);

                if (eventIndex != eventType || !(ok1 && ok2 && ok3))
                    continue;

                auto moduleY = m_context->getConfig()->getModuleConfig(eventIndex, moduleIndex);

                if (!moduleY)
                    continue;

                bool yFound = (moduleIndex < SUBEVENT_MAX
                               && addressValue < ADDRESS_MAX
                               && eventValues[moduleIndex][addressValue] >= 0);

                u32 yValue = yFound ? static_cast<u32>(eventValues[moduleIndex][addressValue]) : 0;

                if (!(xFound && yFound))
                    continue;

                //qDebug() << hist2d << hist2d->xAxisResolution() << hist2d->yAxisResolution() << xValue << yValue
                //    << eventIndex << moduleIndex << addressValue;

                // Need to get the values resolution from the module config and
                // calculate the shift using the histograms resolution

                int shiftX = 0;
                int shiftY = 0;

                {
                    int dataBits = moduleX->getDataBits();
                    int histoBits = hist2d->getXBits();
                    shiftX = dataBits - histoBits;
                    if (shiftX < 0)
                        shiftX = 0;

                    //qDebug() << hist2d << "X histoBits, dataBits, shift"
                    //         << histoBits << dataBits << shiftX;
                }

                {
                    int dataBits = moduleY->getDataBits();
                    int histoBits = hist2d->getYBits();
                    shiftY = dataBits - histoBits;
                    if (shiftY < 0)
                        shiftY = 0;

                    //qDebug() << hist2d << "Y histoBits, dataBits, shift"
                    //         << histoBits << dataBits << shiftY;
                }

                //qDebug("x-module=%s: databits=%d, histobits=%d", );

                hist2d->fill(xValue >> shiftX, yValue >> shiftY);
            }
        }
    } catch (const end_of_buffer &)
    {
        emit logMessage(QString("Error: unexpectedly reached end of buffer"));
        ++stats.mvmeBuffersWithErrors;
    }


    emit bufferProcessed(buffer);
}
