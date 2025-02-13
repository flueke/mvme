#include "waveform_traces.h"

#include <algorithm>
#include <numeric>
#include <ostream>

#include <mesytec-mvlc/util/algo.h>
#include <mesytec-mvlc/util/fmt.h>
#include "util/math.h"
#include "mdpp-sampling/mdpp_decode.h"

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

    mvlc::util::for_each(std::begin(trace.xs), std::end(trace.xs), std::begin(trace.ys), sample_printer);

    return out;
}

std::ostream &print_trace_compact(std::ostream &out, const Trace &trace)
{
    auto sample_printer = [&out] (double x, double y)
    {
        out << fmt::format("[{}, {}], ", x, y);
    };

    out << "[ ";
    mvlc::util::for_each(std::begin(trace.xs), std::end(trace.xs), std::begin(trace.ys), sample_printer);
    if (!trace.empty())
    {
        try
        {
            out.seekp(-2, std::ios_base::end); // remove trailing comma to make JSON parsers happy
        }
        catch (const std::ios_base::failure &) {}
    }
    out << " ]\n";

    return out;
}

std::pair<double, double> find_minmax_y(const Trace &trace)
{
    auto minmax = std::minmax_element(std::begin(trace.ys), std::end(trace.ys));

    if (minmax.first != std::end(trace.ys) && minmax.second != std::end(trace.ys))
        return { *minmax.first, *minmax.second };

    return { util::make_quiet_nan(), util::make_quiet_nan() };
}

void scale_x_values(const waveforms::Trace &input, waveforms::Trace &output, double dtSample, double phase)
{
    output.clear();

    std::transform(std::begin(input.xs), std::end(input.xs), std::back_inserter(output.xs),
        [dtSample, phase](double x) { return x * dtSample + dtSample * (1.0 - phase); });

    output.ys = input.ys;
    output.meta = input.meta;
}

void rescale_x_values(waveforms::Trace &input, double dtSample, double phase)
{
    std::for_each(std::begin(input.xs), std::end(input.xs),
        [dtSample, phase, index=0](double &x) mutable { x = index++ * dtSample + dtSample * (1.0 - phase); });
}

std::vector<const Trace *> get_trace_column(const TraceHistories &history, size_t traceIndex)
{
    auto accu = [traceIndex] (std::vector<const waveforms::Trace *> &acc, const waveforms::TraceHistory &history)
    {
        if (0 <= traceIndex && static_cast<size_t>(traceIndex) < history.size())
            acc.push_back(&history[traceIndex]);
        return acc;
    };

    return std::accumulate(std::begin(history), std::end(history), std::vector<const Trace *>{}, accu);
}

double get_trace_dx(const Trace &trace)
{
    if (trace.size() < 2)
        return 0.0;
    return trace.xs[1] - trace.xs[0];
}

std::string trace_meta_to_string(const Trace::MetaMap &meta)
{
    std::vector<std::string> strParts;

    for (const auto &[key, value]: meta)
    {
        std::string valueStr;
        if (key == "config")
        {
            valueStr = fmt::format("value:{}, {}", std::get<u32>(value),
                mesytec::mvme::mdpp_sampling::sampling_config_to_string(std::get<u32>(value)));
        }
        else
        {
            valueStr = std::visit([] (const auto &v) { return fmt::format("{}", v); }, value);
        }
        strParts.emplace_back(fmt::format("{}={}", key, valueStr));
    }

    std::sort(std::begin(strParts), std::end(strParts));
    return fmt::format("{}", fmt::join(strParts, ", "));
}

}
