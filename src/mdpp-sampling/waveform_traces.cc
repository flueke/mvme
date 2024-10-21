#include "waveform_traces.h"

#include <numeric>
#include <ostream>

#include <mesytec-mvlc/util/algo.h>
#include <mesytec-mvlc/util/fmt.h>

namespace mesytec::mvme::waveforms
{

using namespace mesytec::mvlc;

namespace detail
{

template<typename T>
size_t get_used_memory(const T &traceHistories)
{
    return std::accumulate(std::begin(traceHistories), std::end(traceHistories), 0u,
        [] (size_t accu, const auto &obj) { return accu + get_used_memory(obj); });
}

}

size_t get_used_memory(const Trace &trace)
{
    return trace.xs.capacity() * sizeof(double) + trace.ys.capacity() * sizeof(double);
}

size_t get_used_memory(const TraceHistory &traceHistory)
{
    return traceHistory.size() * sizeof(traceHistory[0]) + detail::get_used_memory(traceHistory);
}

size_t get_used_memory(const TraceHistories &traceHistories)
{
    return traceHistories.capacity() * sizeof(traceHistories[0]) +  detail::get_used_memory(traceHistories);
}

std::ostream &print_trace(std::ostream &out, const Trace &trace)
{
    auto sample_printer = [&out] (double x, double y)
    {
        out << fmt::format("{} {}\n", x, y);
    };

    util::for_each(std::begin(trace.xs), std::end(trace.xs), std::begin(trace.ys), sample_printer);

    return out;
}

}
