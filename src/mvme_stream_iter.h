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
#ifndef __MVME_STREAM_ITER_H__
#define __MVME_STREAM_ITER_H__

#include "analysis/a2/a2_data_filter.h"
#include "databuffer.h"
#include "globals.h"
#include "libmvme_export.h"
#include "mvme_listfile_utils.h"

#include <bitset>

namespace mvme_stream
{

struct LIBMVME_EXPORT StreamInfo
{
    /* Required for multievent splitting only:
     * Filter to match a module data header and a corresponding cache entry to
     * extract the module data size (in words) from the matched module data
     * header. */
    struct FilterWithCache
    {
        a2::data_filter::DataFilter filter;
        a2::data_filter::CacheEntry cache;
    };

    using ModuleHeaderFilters = std::array<FilterWithCache, MaxVMEModules>;

    /* The listfile format version of the stream that is going to be iterated. */
    u32 version;

    /* Set bit N to enable multievent processing for event N.
     *
     * Additionally set ModuleHeaderFilters below for event N to match module
     * headers and extract module data sizes. */
    std::bitset<MaxVMEEvents> multiEventEnabled;

    /* Optional data filters used to match module headers and extract module
     * data sizes when doing multievent splitting. */
    std::array<ModuleHeaderFilters, MaxVMEEvents> moduleHeaderFilters;
};

class LIBMVME_EXPORT StreamIterator
{
    public:
        /* Offsets in units of 32-bit words into a stream buffer. */
        struct ModuleDataOffsets
        {
            // points to the listfile module header preceding the module data
            s32 sectionHeader = -1;
            // first word offset of the module event data
            s32 dataBegin = -1;
            // last word offset of the module event data
            s32 dataEnd = -1;

            bool isValid() const
            {
                return (sectionHeader >= 0 && dataBegin >= 0 && dataEnd >= 0);
            }

            u32 dataSize() const
            {
                return dataEnd - dataBegin;
            }
        };

        struct Result
        {
            using ResultFlag = u8;

            static const ResultFlag NotSet          = 0u;
            static const ResultFlag MultiEvent      = 1u << 0;
            static const ResultFlag EventComplete   = 1u << 1;
            static const ResultFlag EndOfInput      = 1u << 2;
            static const ResultFlag Error           = 1u << 3;

            std::array<ModuleDataOffsets, MaxVMEModules> moduleDataOffsets;
            ListfileConstants lfc;
            DataBuffer *buffer = nullptr;
            s32 sectionOffset = -1;
            ResultFlag flags = 0;

            explicit Result(DataBuffer *streamBuffer = nullptr)
                : buffer(streamBuffer)
            {
                resetModuleDataOffsets();
            }

            void resetModuleDataOffsets()
            {
                moduleDataOffsets.fill(ModuleDataOffsets());
            }

            bool atEnd() const
            {
                return (flags & (Result::EndOfInput | Result::Error));
            }

            u32 lastSectionHeader() const
            {
                assert(sectionOffset >= 0);
                assert(buffer);
                return *buffer->indexU32(sectionOffset);
            }

            u32 lastSectionType() const
            {
                return lfc.getSectionType(lastSectionHeader());
            }

            u32 lastSectionSize() const
            {
                return lfc.getSectionSize(lastSectionHeader());
            }
        };

        explicit StreamIterator(const StreamInfo &streamInfo);

        void setStreamBuffer(DataBuffer *streamBuffer);
        const Result &next();
        const Result &result() const;

        DataBuffer *streamBuffer() const { return m_result.buffer; }
        const BufferIterator &bufferIterator() const { return m_bufferIter; }
        const BufferIterator &eventIterator() const { return m_eventIter; }
        bool atEnd() const { return m_result.atEnd(); }

    private:
        Result &nextEvent();
        Result &startEventSectionIteration(u32 sectionHeader, u32 *data, u32 size);

        ModuleDataOffsets &getOffsets(u32 moduleIndex)
        {
            assert(moduleIndex < MaxVMEModules);
            return m_result.moduleDataOffsets[moduleIndex];
        }

        StreamInfo m_streamInfo;
        BufferIterator m_bufferIter;
        BufferIterator m_eventIter;
        Result m_result;
};

} // ns mvme_stream

#endif /* __MVME_STREAM_ITER_H__ */
