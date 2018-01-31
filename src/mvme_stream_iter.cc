#include "mvme_stream_iter.h"

namespace mvme_stream
{

StreamIterator::StreamIterator(const StreamInfo &streamInfo, DataBuffer *streamBuffer)
    : m_streamInfo(streamInfo)
    , m_buffer(streamBuffer)
{ }

StreamIterator::Result
StreamIterator::next()
{
    Result result;

    return result;
}

} // ns mvme_stream
