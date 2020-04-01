#ifndef __MESYTEC_MVLC_MVLC_READOUT_H__
#define __MESYTEC_MVLC_MVLC_READOUT_H__

#include <chrono>
#include <system_error>

#include "mvlc.h"
#include "mvlc_constants.h"
#include "mvlc_dialog_util.h"
#include "mesytec-mvlc_export.h"
#include "util/string_view.hpp"

using namespace nonstd;

namespace mesytec
{
namespace mvlc
{

enum class MESYTEC_MVLC_EXPORT ReadoutWorkerError
{
    NoError,
    ReadoutRunning,
    ReadoutNotRunning,
    ReadoutPaused,
    ReadoutNotPaused,
};

MESYTEC_MVLC_EXPORT std::error_code make_error_code(ReadoutWorkerError error);

class MESYTEC_MVLC_EXPORT ReadoutBuffer
{
    public:
        ConnectionType type() const { return m_type; }
        void setType(ConnectionType t) { m_type = t; }

        size_t bufferNumber() const { return m_number; }
        void setBufferNumber(size_t number) { m_number = number; }

        size_t capacity() const { return m_buffer.size(); }
        size_t used() const { return m_used; }
        size_t free() const { return capacity() - m_used; }

        void ensureFreeSpace(size_t freeSpace)
        {
            if (free() < freeSpace)
                m_buffer.resize(m_used + freeSpace);
            assert(free() >= freeSpace);
        }

        void clear() { m_used = 0u; }

        void use(size_t bytes)
        {
            assert(m_used + bytes <= capacity());
            m_used += bytes;
        }

        const std::vector<u8> &buffer() const { return m_buffer; }
        std::vector<u8> &buffer() { return m_buffer; }

        basic_string_view<const u8> viewU8() const
        {
            return basic_string_view<const u8>(m_buffer.data(), m_used);
        }

        basic_string_view<const u32> viewU32() const
        {
            return basic_string_view<const u32>(
                reinterpret_cast<const u32 *>(m_buffer.data()),
                m_used / sizeof(u32));
        }

    private:
        ConnectionType m_type;
        size_t m_number;
        std::vector<u8> m_buffer;
        size_t m_used;
};

class MESYTEC_MVLC_EXPORT ReadoutWorker
{
    public:
        enum class State { Idle, Starting, Running, Paused, Stopping };

        struct Counters
        {
            std::chrono::time_point<std::chrono::steady_clock> tStart;
            std::chrono::time_point<std::chrono::steady_clock> tEnd;
            size_t buffersRead;
            size_t bytesRead;
            size_t framingErrors;
            size_t unusedBytes;
            size_t readTimeouts;
            std::array<size_t, stacks::StackCount> stackHits;
        };

        ReadoutWorker(MVLC &mvlc, const std::vector<StackTrigger> &stackTriggers);

        State state() const;
        std::error_code start(const std::chrono::seconds &duration = {});
        std::error_code stop();
        std::error_code pause();
        std::error_code resume();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_H__ */
