#include "util/readout_buffer_queues.h"

namespace mesytec
{
namespace mvlc
{

ReadoutBufferQueues::ReadoutBufferQueues(size_t bufferCapacity, size_t bufferCount)
    : m_bufferStorage(bufferCount, ReadoutBuffer(bufferCapacity))
{
    for (auto &buffer: m_bufferStorage)
        m_emptyBuffers.enqueue(&buffer);
}

} // end namespace mvlc
} // end namespace mesytec
