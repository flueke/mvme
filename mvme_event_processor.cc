#include "mvme_event_processor.h"
#include "mvme_context.h"
#include "mvme_listfile.h"
#include "histogram.h"

using namespace listfile;

MVMEEventProcessor::MVMEEventProcessor(MVMEContext *context)
    : m_context(context)
{
}

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
        while (wordsLeftInSection)
        {
            u8 *oldBufferP = iter.buffp;
            processSubEvent(iter, eventType, subEventIndex);
            u8 *newBufferP = iter.buffp;
            wordsLeftInSection -= (newBufferP - oldBufferP) / sizeof(u32);
            ++subEventIndex;
        }
    }

    emit bufferProcessed(buffer);
}

void MVMEEventProcessor::processSubEvent(BufferIterator &iter, int eventType, int subEventIndex)
{
    u32 subEventHeader = iter.peekU32();

    auto moduleType  = static_cast<VMEModuleType>((subEventHeader & ModuleTypeMask) >> ModuleTypeShift);
    u32 subEventSize = (subEventHeader & SubEventSizeMask) >> SubEventSizeShift;
    ModuleConfig *cfg = m_context->getConfig()->getModuleConfig(eventType, subEventIndex);

    if (isMesytecModule(moduleType) && cfg)
    {
        processMesytecEvent(iter, cfg);
    }
    else
    {
        qDebug() << "skipping subevent" << (int)moduleType << cfg;
        iter.skip(subEventSize * sizeof(u32) + sizeof(u32));
    }
}

void MVMEEventProcessor::processMesytecEvent(BufferIterator &iter, ModuleConfig *cfg)
{
    // TODO: implement something like ModuleData moduleData(iter->asU32(), iter->longwordsLeft());

    u32 subEventHeader = iter.extractU32();
    u32 subEventSize = (subEventHeader & SubEventSizeMask) >> SubEventSizeShift;

    auto histo = m_context->getHistogram(cfg->getFullName());

    if (!histo)
    {
        histo = new Histogram(0, 33, 8192); // TODO: size dynamically
        m_context->addHistogram(cfg->getFullName(), histo);
    }

    for (u32 i=0; i<subEventSize; ++i)
    {
        u32 currentWord = iter.extractU32();

        if (currentWord == 0xFFFFFFFF || currentWord == 0x00000000)
            continue;


        bool data_found_flag = ((currentWord & 0xF0000000) == 0x10000000) // MDPP
                || ((currentWord & 0xFF800000) == 0x04000000); // MxDC

        if (data_found_flag)
        {
            u16 channel = (currentWord & 0x003F0000) >> 16;
            u32 value   = (currentWord & 0x00001FFF); // FIXME: data width depends on module type and configuration
            histo->incValue(channel, value);

            // TODO: fill 2d histo (spectrogram) if one is configured
        }
    }
}
