#include "mvme_event_processor.h"
#include "mvme_context.h"
#include "mvme_listfile.h"
#include "histogram.h"

using namespace listfile;

MVMEEventProcessor::MVMEEventProcessor(MVMEContext *context)
    : m_context(context)
{
}

#if 0
// Process an event buffer containing one or more events. Each event contains a
// subevent for each module contributing data for this event.
void MVMEEventProcessor::processEventBuffer(DataBuffer *buffer)
{
    qDebug() << __PRETTY_FUNCTION__;
    BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align32);

    while (iter.longwordsLeft())
    {
        u32 sectionHeader = iter.peekU32();
        int sectionType = (sectionHeader & SectionTypeMask) >> SectionTypeShift;
        u32 sectionSize = (sectionHeader & SectionSizeMask) >> SectionSizeShift;

        if (sectionType != SectionType_Event)
        {
            iter.skip(sectionSize * sizeof(u32) + sizeof(u32));
            continue;
        }

        int eventID = (sectionHeader & EventTypeMask) >> EventTypeShift;
        auto eventConfig = m_context->getConfig()->getEventConfig(eventID);

        if (!eventConfig)
        {
            qDebug() << "no event config found for event id =" << eventID << ", skipping event section";
            iter.skip(sectionSize * sizeof(u32) + sizeof(u32));
            continue;
        }

        auto longWordsLeft = iter.longwordsLeft();

        Section section(iter.asU32(), iter.longwordsLeft());

        if (!section.isValid())
        {
            qDebug("iter=0x%08x, sectionHeader=0x%08x, section.isValid=%d",
                   iter.buffp, sectionHeader, section.isValid());
            int x = 5;
            qDebug() << iter.asU32() + iter.longwordsLeft() + 1;
            Section section2(iter.asU32(), iter.longwordsLeft());
        }

        // for each HistogramCollection that's interested in one of the modules in this event:
        //   get the module subevent from the event
        //   for each channelNumber in the HistogramCollection:
        //     extract the value for the channelNumber from the module subevent
        //     increment the histograms bin for the (channelnumber, value)
        for (auto histCollection: m_context->getHistogramList())
        {
            QString sourceModulePath = histCollection->property("Histogram.sourceModule").toString();
            QString configName = sourceModulePath.section('.', 0, 0);
            QString moduleName = sourceModulePath.section('.', 1, 1);

            if (configName == eventConfig->getName())
            {
                int moduleIndex = eventConfig->getModuleIndexByModuleName(moduleName);

                if (moduleIndex >= 0)
                {
                    for (u32 channelAddress=0;
                         channelAddress < histCollection->m_channels;
                         ++channelAddress)
                    {
                        ModuleValue modValue = section[moduleIndex][channelAddress];

                        if (!modValue.isValid())
                        {
                            qDebug() << "invalid mod value for " << configName << moduleName << moduleIndex;
                        }

#if 0
                            qDebug() << histCollection
                                << "configName" << configName
                                << "moduleName" << moduleName
                                << "moduleIndex" << moduleIndex
                                << "channelAddress" << channelAddress
                                << "modValue.isValid()" << modValue.isValid()
                                << "modValue.getAddress()" << modValue.getAddress()
                                << "modValue.getValue()" << modValue.getValue()
                                ;
#endif

                        if (modValue.isValid())
                        {
                            histCollection->incValue(channelAddress, modValue.getValue());
                        }
                    }
                }
            }
        }

        iter.skip(sectionSize * sizeof(u32) + sizeof(u32));

        // for each Hist2D that's interested in any of the modules in this event:
        //   get (module, channel address) from hist2d.xaxissource
        //   get (module, channel address) from hist2d.yaxissource
        //   get value for xaxis from the event
        //   get value for yaxis from the event
        //   if both values are valid:
        //      hist2d->fill(xValue, yValue)
    }

    emit bufferProcessed(buffer);
}
#else
// Process an event buffer containing one or more events.
void MVMEEventProcessor::processEventBuffer(DataBuffer *buffer)
{

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

        int eventType = (sectionHeader & EventTypeMask) >> EventTypeShift;
        u32 wordsLeftInSection = sectionSize;
        int subEventIndex = 0;

        // subeventindex -> address -> value
        QHash<int, QHash<int, u32>> eventValues;

        while (wordsLeftInSection)
        {
            u8 *oldBufferP = iter.buffp;

            {

                u32 subEventHeader = iter.extractU32();
                u32 subEventSize = (subEventHeader & SubEventSizeMask) >> SubEventSizeShift;
                auto moduleType  = static_cast<VMEModuleType>((subEventHeader & ModuleTypeMask) >> ModuleTypeShift);
                ModuleConfig *cfg = m_context->getConfig()->getModuleConfig(eventType, subEventIndex);

                if (isMesytecModule(moduleType) && cfg)
                {
                    Histogram *histo = 0;

                    for (auto h: m_context->getHistogramList())
                    {
                        if (h->property("Histogram.sourceModule").toString() == cfg->getFullPath())
                        {
                            histo = h;
                            break;
                        }
                    }

                    for (u32 i=0; i<subEventSize; ++i)
                    {
                        u32 currentWord = iter.extractU32();

                        if (currentWord == 0xFFFFFFFF || currentWord == 0x00000000)
                            continue;

                        bool header_found_flag = (currentWord & 0xC0000000) == 0x40000000;
                        bool data_found_flag = ((currentWord & 0xF0000000) == 0x10000000) // MDPP
                            || ((currentWord & 0xFF800000) == 0x04000000); // MxDC
                        bool eoe_found_flag = (currentWord & 0xC0000000) == 0xC0000000;

                        if (data_found_flag)
                        {
                            u16 address = (currentWord & 0x003F0000) >> 16;
                            u32 value   = (currentWord & 0x00001FFF); // FIXME: data width depends on module type and configuration
                            if (histo)
                            {
                                histo->incValue(address, value);
                            }

                            eventValues[subEventIndex][address] = value;
                        }
                    }
                }
                else
                {
                    iter.skip(subEventSize * sizeof(u32));
                }
            }

            u8 *newBufferP = iter.buffp;
            wordsLeftInSection -= (newBufferP - oldBufferP) / sizeof(u32);
            ++subEventIndex;
        }

        //
        // fill 2D Histograms
        //
        auto hist2ds = m_context->get2DHistograms();
        for (auto hist2d: hist2ds)
        {
            bool ok1, ok2, ok3;
            QString sourcePath = hist2d->property("Hist2D.xAxisSource").toString();
            int eventIndex   = sourcePath.section('.', 0, 0).toInt(&ok1);
            int moduleIndex  = sourcePath.section('.', 1, 1).toInt(&ok2);
            int addressValue = sourcePath.section('.', 2, 2).toInt(&ok3);

            if (eventIndex != eventType || !(ok1 && ok2 && ok3))
                continue;

            u32 xValue = eventValues[moduleIndex][addressValue];

            sourcePath = hist2d->property("Hist2D.yAxisSource").toString();
            eventIndex   = sourcePath.section('.', 0, 0).toInt(&ok1);
            moduleIndex  = sourcePath.section('.', 1, 1).toInt(&ok2);
            addressValue = sourcePath.section('.', 2, 2).toInt(&ok3);

            if (eventIndex != eventType || !(ok1 && ok2 && ok3))
                continue;

            u32 yValue = eventValues[moduleIndex][addressValue];

            //qDebug() << hist2d << hist2d->xAxisResolution() << hist2d->yAxisResolution() << xValue << yValue
            //    << eventIndex << moduleIndex << addressValue;

            // FIXME: can't just shift here: need to know the values resolution
            hist2d->fill(xValue >> 2, yValue >> 2);
        }
    }

    emit bufferProcessed(buffer);
}
#endif

