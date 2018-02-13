#include "mvme_stream_iter.h"
#include "util/perf.h"
#include "mvme_listfile.h"

namespace mvme_stream
{

StreamIterator::StreamIterator(const StreamInfo &streamInfo)
    : m_streamInfo(streamInfo)
{
}

void StreamIterator::setStreamBuffer(DataBuffer *buffer)
{
    m_result = Result(buffer);
    m_result.lfc = listfile_constants(m_streamInfo.version);

    m_bufferIter = BufferIterator(streamBuffer()->data, streamBuffer()->used, BufferIterator::Align32);
    m_eventIter = {};
}

const StreamIterator::Result &StreamIterator::lastResult() const
{
    return m_result;
}

const StreamIterator::Result &StreamIterator::next()
{
    assert(streamBuffer());

    try
    {
        if (m_eventIter.data)
        {
            return nextEvent();
        }

        auto &iter(m_bufferIter);
        auto &lfc(m_result.lfc);

        m_result.flags = Result::NotSet;

        if (iter.longwordsLeft())
        {
            u32 sectionHeader = iter.extractU32();
            u32 sectionType = lfc.section_type(sectionHeader);
            u32 sectionSize = lfc.section_size(sectionHeader);

            m_result.sectionOffset = iter.current32BitOffset() - 1;

            if (unlikely(sectionSize > iter.longwordsLeft()))
            {
#ifdef MVME_STREAM_PROCESSOR_DEBUG
                QString msg = (QString("Error (mvme stream, buffer#%1): extracted section size exceeds buffer size!"
                                       " sectionHeader=0x%2, sectionSize=%3, wordsLeftInBuffer=%4")
                               .arg(bufferNumber)
                               .arg(sectionHeader, 8, 16, QLatin1Char('0'))
                               .arg(sectionSize)
                               .arg(iter.longwordsLeft()));
                qDebug() << msg;
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
                if (startEventSectionIteration(sectionHeader, iter.asU32(), sectionSize).flags == Result::Error)
                    return m_result;

                // perform the first step. Updates procState
                nextEvent();

                // Test if the event was already completely processed. This
                // happens in the non-multievent case.
                if (m_result.flags == Result::EventComplete)
                {
                    iter.skip(sectionSize * sizeof(u32));
                    m_eventIter = {};
                    // FIXME: m_d->counters.bytesProcessed += sectionSize * sizeof(u32) + sizeof(u32);
                }
            }
        }
        else
        {
            m_result.flags = Result::EndOfInput;
            m_eventIter = {};
        }
    }
    catch (const end_of_buffer &)
    {
        m_result.flags = Result::Error;
        m_eventIter = {};
    }

    return m_result;
}

StreamIterator::Result &StreamIterator::startEventSectionIteration(u32 sectionHeader, u32 *data, u32 size)
{
    assert(!m_eventIter.data);

    //
    // Step1: collect all module section header offsets
    //
    auto &lfc(m_result.lfc);

    const u32 eventIndex = lfc.event_index(sectionHeader);
    BufferIterator localEventIter(reinterpret_cast<u8 *>(data), size * sizeof(u32));
    m_result.resetModuleDataOffsets();

    for (u32 moduleIndex = 0; moduleIndex < MaxVMEModules; moduleIndex++)
    {
        if (localEventIter.atEnd())
            break;

        if (localEventIter.peekU32() == EndMarker)
            break;

        auto &offsets(m_result.moduleDataOffsets[moduleIndex]);
        offsets.sectionHeader = localEventIter.current32BitOffset();

        // skip to the next subevent
        u32 moduleSectionHeader = localEventIter.extractU32();
        u32 moduleSectionSize   = lfc.module_data_size(moduleSectionHeader);
        localEventIter.skip(sizeof(u32), moduleSectionSize);
    }

    m_eventIter = BufferIterator(reinterpret_cast<u8 *>(data), size * sizeof(u32));
    m_result.flags = Result::NotSet;

    return m_result;
}

StreamIterator::Result &StreamIterator::nextEvent()
{
    assert(m_eventIter.data);

    //FIXME: Q_ASSERT(procState.buffer);
    //FIXME: Q_ASSERT(0 <= procState.lastSectionHeaderOffset);
    //FIXME: Q_ASSERT(procState.lastSectionHeaderOffset < static_cast<s64>(procState.buffer->size / sizeof(u32)));

    auto &lfc(m_result.lfc);

    const u32 sectionHeader = *m_result.buffer->indexU32(m_result.sectionOffset);
    const u32 sectionType = lfc.section_type(sectionHeader);
    const u32 eventIndex  = lfc.event_index(sectionHeader);

    const u32 *ptrToLastWord = reinterpret_cast<const u32 *>(m_eventIter.data + m_eventIter.size);

    if (m_result.moduleDataOffsets[0].sectionHeader < 0)
        return m_result;;


    /* Early test to see if the first module still has a matching header. This
     * is done to avoid looping one additional time and calling
     * beginEvent()/endEvent() without any valid data available for extractors
     * to process. */
    if (m_streamInfo.multiEventEnabled.test(eventIndex)
        && !a2::data_filter::matches(
            m_streamInfo.moduleHeaderFilters[eventIndex][0].filter,
            *m_result.buffer->indexU32(m_result.moduleDataOffsets[0].dataBegin)))
    {
        m_result.flags = Result::EventComplete;
        return m_result;
    }

    //FIXME: this->analysis->beginEvent(eventIndex);

    for (u32 moduleIndex = 0;
         m_result.moduleDataOffsets[moduleIndex].sectionHeader >= 0;
         moduleIndex++)
    {
        auto &filters(m_streamInfo.moduleHeaderFilters[eventIndex][moduleIndex]);
        auto &offsets(m_result.moduleDataOffsets[moduleIndex]);

        // Single event processing as multievent is not enabled
        if (!m_streamInfo.multiEventEnabled.test(eventIndex))
        {
            u32 moduleDataSize = lfc.module_data_size(
                *m_result.buffer->indexU32(offsets.sectionHeader));

            offsets.dataEnd = offsets.dataBegin + moduleDataSize;

            // FIXME:
            //      this->analysis->processModuleData(
            //          eventIndex,
            //          moduleIndex,
            //          mi.moduleHeader,
            //          moduleDataSize);
        }
        // Multievent splitting is enabled. Check for a header match and extract the data size.
        else if (a2::data_filter::matches(filters.filter, *m_result.buffer->indexU32(offsets.dataBegin)))
        {
            u32 *moduleHeader = m_result.buffer->indexU32(offsets.dataBegin);
            u32 moduleEventSize = a2::data_filter::extract(filters.cache, *moduleHeader);

            if (unlikely(moduleHeader + moduleEventSize + 1 > ptrToLastWord))
            {
#if 0 // FIXME
                this->counters.buffersWithErrors++;

                QString msg = (QString("Error (mvme stream, buffer#%1): extracted module event size (%2) exceeds buffer size!"
                                       " eventIndex=%3, moduleIndex=%4, moduleHeader=0x%5, skipping event")
                               .arg(bufferNumber)
                               .arg(moduleEventSize)
                               .arg(eventIndex)
                               .arg(moduleIndex)
                               .arg(*mi.moduleHeader, 8, 16, QLatin1Char('0'))
                              );
                qDebug() << msg;
                logMessage(msg);
#endif

                m_result.flags = Result::Error;
                break;
            }

            offsets.dataEnd = offsets.dataBegin + moduleEventSize + 1;

#if 0 //FIXME
                        this->analysis->processModuleData(
                            eventIndex,
                            moduleIndex,
                            mi.moduleHeader,
                            moduleEventSize + 1);
#endif
        }
        else
        {
            m_result.flags = Result::EventComplete;
        }
    }

    //FIXME: this->analysis->endEvent(eventIndex);

    return m_result;
}

} // ns mvme_stream
