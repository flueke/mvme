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
#include "mvme_stream_iter.h"
#include "util/perf.h"
#include "mvme_listfile.h"

#define MVME_STREAM_ITER_DEBUG 1

namespace
{
#if 0
    QDebug operator<<(QDebug out, const mvme_stream::StreamIterator::ModuleDataOffsets &offsets)
    {
        QDebugStateSaver qdss(out);

        out.nospace().noquote();

        out << (QString("(header=%1, begin=%2, end=%3)")
                .arg(offsets.sectionHeader)
                .arg(offsets.dataBegin)
                .arg(offsets.dataEnd)
               );
        return out;
    }
#endif
} // end anon namespace

namespace mvme_stream
{

StreamIterator::StreamIterator(const StreamInfo &streamInfo)
    : m_streamInfo(streamInfo)
{
}

void StreamIterator::setStreamBuffer(DataBuffer *buffer)
{
    qDebug() << __PRETTY_FUNCTION__ << "buffer id =" << buffer->id;

    m_result = Result(buffer);
    m_result.lfc = listfile_constants(m_streamInfo.version);

    m_bufferIter = BufferIterator(streamBuffer()->data, streamBuffer()->used, BufferIterator::Align32);
    m_eventIter = {};
}

const StreamIterator::Result &StreamIterator::result() const
{
    return m_result;
}

const StreamIterator::Result &StreamIterator::next()
{
    assert(streamBuffer());

    const u64 bufferNumber = streamBuffer()->id;

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
            u32 sectionType = lfc.getSectionType(sectionHeader);
            u32 sectionSize = lfc.getSectionSize(sectionHeader);

            m_result.sectionOffset = iter.current32BitOffset() - 1;

            s32 eventIndex = (sectionType == ListfileSections::SectionType_Event)
                ? lfc.getEventIndex(sectionHeader)
                : -1;

            qDebug() << (QString("%6: got sectionHeader=0x%1, type=%2, size=%3, eventIndex=%4, sectionOffset=%5")
                         .arg(sectionHeader, 8, 16, QLatin1Char('0'))
                         .arg(sectionType)
                         .arg(sectionSize)
                         .arg(eventIndex)
                         .arg(m_result.sectionOffset)
                         .arg(__PRETTY_FUNCTION__)
                        );

            if (unlikely(sectionSize > iter.longwordsLeft()))
            {
#if MVME_STREAM_ITER_DEBUG
                QString msg = (QString("Error (mvme stream, buffer#%1): extracted section size exceeds buffer size!"
                                       " sectionHeader=0x%2, sectionSize=%3, wordsLeftInBuffer=%4")
                               .arg(bufferNumber)
                               .arg(sectionHeader, 8, 16, QLatin1Char('0'))
                               .arg(sectionSize)
                               .arg(iter.longwordsLeft()));
                qDebug() << msg;
#endif
                throw end_of_buffer();
            }

            // Handle the start of a new event section.
            // If multievent processing is enabled for the event multiple calls
            // to next() may be required to completely process the event
            // section.
            if (likely(sectionType == ListfileSections::SectionType_Event))
            {
                // init event iteration
                startEventSectionIteration(sectionHeader, iter.asU32(), sectionSize);

                // perform the first iteration step
                nextEvent();

                // Test if the event was already completely processed. This
                // happens in the non-multievent case.
                if (m_result.flags & Result::EventComplete)
                {
                    qDebug() << __PRETTY_FUNCTION__ << "nextEvent() returned EventComplete, resetting eventIter";
                    iter.skip(sectionSize * sizeof(u32));
                    m_eventIter = {};
                }
            }
            else
            {
                qDebug() << __PRETTY_FUNCTION__ << "skipping bufferIter over non-event section";
                iter.skip(sectionSize * sizeof(u32));
            }
        }

        if (iter.longwordsLeft() == 0)
        {
            qDebug() << "buffer iterator at end, setting EndOfInput flag";
            m_result.flags |= Result::EndOfInput;
            m_eventIter = {};
        }
    }
    catch (const end_of_buffer &)
    {
        qDebug() << "buffer iterator overflow";
        m_result.flags = Result::Error;
        m_eventIter = {};
    }

    return m_result;
}

StreamIterator::Result &StreamIterator::startEventSectionIteration(u32 /*sectionHeader*/, u32 *data, u32 size)
{
    qDebug() << __PRETTY_FUNCTION__;

    assert(!m_eventIter.data);

    //
    // Step1: collect all module section header offsets
    //
    auto &lfc(m_result.lfc);

    //const u32 eventIndex = lfc.getEventIndex(sectionHeader);
    BufferIterator localEventIter(reinterpret_cast<u8 *>(data), size * sizeof(u32));
    m_result.resetModuleDataOffsets();

    for (u32 moduleIndex = 0; moduleIndex < MaxVMEModules; moduleIndex++)
    {
        if (localEventIter.atEnd())
            break;

        if (localEventIter.peekU32() == EndMarker)
            break;

        auto &offsets(m_result.moduleDataOffsets[moduleIndex]);
        offsets.sectionHeader = localEventIter.asU32() - streamBuffer()->asU32(0);
        offsets.dataBegin     = offsets.sectionHeader + 1;

        // skip to the next subevent
        u32 moduleSectionHeader = localEventIter.extractU32();
        u32 moduleSectionSize   = lfc.getModuleDataSize(moduleSectionHeader);
        u32 moduleType          = lfc.getModuleType(moduleSectionHeader);
        localEventIter.skip(sizeof(u32), moduleSectionSize);

        qDebug() << (QString("%1: moduleIndex=%2, offsets.sectionHeader=%3, moduleSectionHeader=0x%4"
                             ", moduleSectionSize=%5, moduleType=%6")
                     .arg(__PRETTY_FUNCTION__)
                     .arg(moduleIndex)
                     .arg(offsets.sectionHeader)
                     .arg(moduleSectionHeader, 8, 16, QLatin1Char('0'))
                     .arg(moduleSectionSize)
                     .arg(moduleType)
                    );
    }

    m_eventIter = BufferIterator(reinterpret_cast<u8 *>(data), size * sizeof(u32));
    m_result.flags = Result::NotSet;

    return m_result;
}

StreamIterator::Result &StreamIterator::nextEvent()
{
    qDebug() << __PRETTY_FUNCTION__;

    auto &lfc(m_result.lfc);
    const u32 sectionHeader = *streamBuffer()->indexU32(m_result.sectionOffset);
    [[maybe_unused]] const u32 sectionType   = lfc.getSectionType(sectionHeader);
    const u32 eventIndex    = lfc.getEventIndex(sectionHeader);
    const u32 *ptrToLastWord = reinterpret_cast<const u32 *>(m_eventIter.data + m_eventIter.size);

    assert(sectionType == ListfileSections::SectionType_Event);

    // single event case
    if (!m_streamInfo.multiEventEnabled.test(eventIndex))
    {
        for (u32 moduleIndex = 0;
             getOffsets(moduleIndex).sectionHeader >= 0;
             moduleIndex++)
        {
            auto &offsets(getOffsets(moduleIndex));
            assert(offsets.dataEnd < 0);

            u32 moduleSectionHeader = *streamBuffer()->indexU32(offsets.sectionHeader);
            u32 moduleDataSize      = lfc.getModuleDataSize(moduleSectionHeader);

            // includes adjustment for EndMarker
            offsets.dataEnd = offsets.dataBegin + moduleDataSize - 2;
        }

        m_result.flags = Result::EventComplete;
    }
    // multi event processing
    // TODO: add a check if the possible next module header still matches (and
    // is inside the module section). If it does not match anymore set the
    // EventComplete flag. This way the caller would get EventComplete |
    // MultiEvent and would not have to call next() and additional time
    else
    {
        qDebug() << __PRETTY_FUNCTION__ << "multi event processing for event" << eventIndex;

        for (u32 moduleIndex = 0;
             getOffsets(moduleIndex).sectionHeader >= 0;
             moduleIndex++)
        {
            auto &offsets(getOffsets(moduleIndex));

            // Test to see if we already yielded some of the events for this
            // module (startEventSectionIteration() initially sets dataEnd to
            // -1).
            if (offsets.dataEnd >= 0)
            {
                offsets.dataBegin = offsets.dataEnd + 1; // next module data word comes right after the last dataEnd
                offsets.dataEnd = -1; // to be updated using the module header filter

                qDebug() << __PRETTY_FUNCTION__ << "moduleIndex =" << moduleIndex << ", new dataBegin =" << offsets.dataBegin;
            }

            auto &filterAndCache = m_streamInfo.moduleHeaderFilters[eventIndex][moduleIndex];
            u32 *moduleHeader = streamBuffer()->indexU32(offsets.dataBegin); // throws if out of range

            // match filter, extract size, adjust dataEnd, yield after all modules done
            if (a2::data_filter::matches(filterAndCache.filter, *moduleHeader))
            {
                // The filter matched, extract data size and adjust dataEnd offset

                u32 moduleEventSize = a2::data_filter::extract(filterAndCache.cache, *moduleHeader);

                qDebug() << __PRETTY_FUNCTION__ << "moduleIndex =" << moduleIndex
                    << ", filter did match, extracted moduleEventSize =" << moduleEventSize;

                if (unlikely(moduleHeader + moduleEventSize + 1 > ptrToLastWord))
                {
                    QString msg = (QString("Error (mvme stream, buffer#%1): extracted module event size (%2) exceeds buffer size!"
                                           " eventIndex=%3, moduleIndex=%4, moduleHeader=0x%5, skipping event")
                                   .arg(streamBuffer()->id)
                                   .arg(moduleEventSize)
                                   .arg(eventIndex)
                                   .arg(moduleIndex)
                                   .arg(*moduleHeader, 8, 16, QLatin1Char('0'))
                                  );
                    qDebug() << msg;
                    m_result.flags |= Result::Error;
                    return m_result;
                }

                offsets.dataEnd = offsets.dataBegin + moduleEventSize;
                m_result.flags |= Result::MultiEvent;
            }
            else
            {
                qDebug() << __PRETTY_FUNCTION__ << "moduleIndex =" << moduleIndex
                    << ", filter did not match anymore -> EventComplete";

                // The module header filter did not match -> we're done
                // If this cases occurs the caller will get EventComplete but
                // the module data offsets are invalid so there's no actual
                // data to process.
                m_result.flags |= Result::EventComplete;
                break;
            }
        }

        // Check if the first module would yield another event if nextEvent()
        // was to be called again. If it's not the case add the EventComplete
        // flag to the result flags. -> The caller should get result.flags =
        // (MultiEvent | EventComplete) as it was intended.
        //{
        //    u32 nextBeginOffset = getOffsets(0).dataEnd + 1; // FIXME: leftoff
        //}
    }

    return m_result;

#if 0

    assert(m_eventIter.data);

    //FIXME: Q_ASSERT(procState.buffer);
    //FIXME: Q_ASSERT(0 <= procState.lastSectionHeaderOffset);
    //FIXME: Q_ASSERT(procState.lastSectionHeaderOffset < static_cast<s64>(procState.buffer->size / sizeof(u32)));

    auto &lfc(m_result.lfc);

    const u32 sectionHeader = *m_result.buffer->indexU32(m_result.sectionOffset);
    const u32 sectionType = lfc.getSectionType(sectionHeader);
    const u32 eventIndex  = lfc.getEventIndex(sectionHeader);

    const u32 *ptrToLastWord = reinterpret_cast<const u32 *>(m_eventIter.data + m_eventIter.size);

    if (m_result.moduleDataOffsets[0].sectionHeader < 0)
    {
        qDebug() << __PRETTY_FUNCTION__ << "no section header for first module. returning";
        return m_result;
    }

    /* Early test to see if the first module still has a matching header. This
     * is done to avoid looping one additional time and calling
     * beginEvent()/endEvent() without any valid data available for extractors
     * to process. */
    if (m_streamInfo.multiEventEnabled.test(eventIndex)
        && !a2::data_filter::matches(
            m_streamInfo.moduleHeaderFilters[eventIndex][0].filter,
            *m_result.buffer->indexU32(m_result.moduleDataOffsets[0].dataBegin)))
    {
        qDebug() << __PRETTY_FUNCTION__ << "multi event enabled for" << eventIndex << " but header filter did not match"
            ", offsets.dataBegin =" << m_result.moduleDataOffsets[0].dataBegin;
        m_result.flags = Result::EventComplete;
        return m_result;
    }

    for (u32 moduleIndex = 0;
         m_result.moduleDataOffsets[moduleIndex].sectionHeader >= 0;
         moduleIndex++)
    {
        auto &filters(m_streamInfo.moduleHeaderFilters[eventIndex][moduleIndex]);
        auto &offsets(getOffsets(moduleIndex));

        qDebug() << "yield module data loop, moduleIndex =" << moduleIndex << ", offsets=" << offsets;

        // advance offsets
        if (offsets.dataEnd >= 0)
        {
            offsets.dataBegin = offsets.dataEnd + 1;
            offsets.dataEnd   = -1;
        }

        // Single event processing as multievent is not enabled
        if (!m_streamInfo.multiEventEnabled.test(eventIndex))
        {
            u32 moduleDataSize = lfc.getModuleDataSize(
                *streamBuffer()->indexU32(offsets.sectionHeader));

            offsets.dataEnd = offsets.dataBegin + moduleDataSize;

            qDebug() << "multievent not enabled => offsets =" << offsets;
        }
        // Multievent splitting is enabled. Check for a header match and extract the data size.
        else if (a2::data_filter::matches(filters.filter, *streamBuffer()->indexU32(offsets.dataBegin)))
        {
            qDebug() << "multievent enabled and filter match";

            u32 *moduleHeader = streamBuffer()->indexU32(offsets.dataBegin);
            u32 moduleEventSize = a2::data_filter::extract(filters.cache, *moduleHeader);

            if (unlikely(moduleHeader + moduleEventSize + 1 > ptrToLastWord))
            {
#if MVME_STREAM_ITER_DEBUG
                QString msg = (QString("Error (mvme stream, buffer#%1): extracted module event size (%2) exceeds buffer size!"
                                       " eventIndex=%3, moduleIndex=%4, moduleHeader=0x%5, skipping event")
                               .arg(streamBuffer()->id)
                               .arg(moduleEventSize)
                               .arg(eventIndex)
                               .arg(moduleIndex)
                               .arg(*moduleHeader, 8, 16, QLatin1Char('0'))
                              );
                qDebug() << msg;
#endif

                m_result.flags = Result::Error;
                break;
            }

            offsets.dataEnd = offsets.dataBegin + moduleEventSize + 1;
        }
        else
        {
            m_result.flags = Result::EventComplete;
        }
    }

    //FIXME: this->analysis->endEvent(eventIndex);

    return m_result;
#endif
}

} // ns mvme_stream
