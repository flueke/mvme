#ifndef __MVME_STREAM_ITER_H__
#define __MVME_STREAM_ITER_H__

#include "analysis/a2/data_filter.h"
#include "databuffer.h"
#include "globals.h"

#include <bitset>

namespace mvme_stream
{

using a2::data_filter::DataFilter;
using a2::data_filter::CacheEntry;

struct StreamInfo
{
    /* Required for multievent splitting only:
     * Filter to match a module data header and a corresponding cache entry to
     * extract the module data size (in words) from the matched module data
     * header. */
    struct FilterWithCache
    {
        DataFilter filter;
        CacheEntry cache;
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

class StreamIterator
{
    public:
        /* Offsets in units of 32-bit words into a stream buffer. */
        struct ModuleDataOffsets
        {
            s32 sectionHeader = -1;
            s32 dataBegin = -1;
            s32 dataEnd = -1;
        };

        struct Result
        {
            using ResultFlag = u8;

            static const ResultFlag NonEventSection = 0u;
            static const ResultFlag MultiEvent      = 1u << 0;
            static const ResultFlag EventComplete   = 1u << 1;
            static const ResultFlag EndOfInput      = 1u << 2;
            static const ResultFlag Error           = 1u << 3;

            s32 sectionOffset = -1;
            std::array<ModuleDataOffsets, MaxVMEModules> moduleDataOffsets;
            DataBuffer *buffer = nullptr;
            ResultFlag flags;
        };

        StreamIterator(const StreamInfo &streamInfo, DataBuffer *streamBuffer);

        Result next();

        DataBuffer *buffer() const { return m_buffer; }
        BufferIterator bufferIterator() const { return m_bufferIter; }
        BufferIterator eventIterator() const { return m_eventIter; }

    private:
        StreamInfo m_streamInfo;
        DataBuffer *m_buffer;
        BufferIterator m_bufferIter;
        BufferIterator m_eventIter;
};

} // ns mvme_stream

#endif /* __MVME_STREAM_ITER_H__ */
