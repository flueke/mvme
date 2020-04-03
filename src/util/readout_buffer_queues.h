#ifndef __MESYTEC_MVLC_UTIL_READOUT_BUFFER_QUEUES_H__
#define __MESYTEC_MVLC_UTIL_READOUT_BUFFER_QUEUES_H__

#include "util/readout_buffer.h"
#include "util/threadsafequeue.h"

namespace mesytec
{
namespace mvlc
{

class ReadoutBufferQueues
{
    public:
        using QueueType = ThreadSafeQueue<ReadoutBuffer *>;

        ReadoutBufferQueues(size_t bufferCapacity, size_t bufferCount);

        QueueType &filledBufferQueue() { return m_filledBuffers; }
        QueueType &emptyBufferQueue() { return m_emptyBuffers; }

    private:
        QueueType m_filledBuffers;
        QueueType m_emptyBuffers;
        std::vector<ReadoutBuffer> m_bufferStorage;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_UTIL_READOUT_BUFFER_QUEUES_H__ */
