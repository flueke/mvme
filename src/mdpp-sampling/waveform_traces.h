#ifndef D5F48B2C_D902_44A9_8EBD_36C71D14399D
#define D5F48B2C_D902_44A9_8EBD_36C71D14399D

#include <cassert>
#include <deque>
#include <vector>

#include <mesytec-mvlc/cpp_compat.h>

namespace mesytec::mvme::waveforms
{

using Sample = std::pair<double, double>;

using mvlc::util::span;

struct Trace
{
    std::vector<double> xs;
    std::vector<double> ys;

    explicit Trace() = default;

    Trace(const Trace &) = default;
    Trace &operator=(const Trace &) = default;

    Trace(Trace &&) = default;
    Trace &operator=(Trace &&) = default;

    void clear()
    {
        xs.clear();
        ys.clear();
    }

    void reserve(size_t capacity)
    {
        xs.reserve(capacity);
        ys.reserve(capacity);
    }

    void push_back(const Sample &sample)
    {
        xs.push_back(sample.first);
        ys.push_back(sample.second);
    }

    void push_back(double x, double y)
    {
        xs.push_back(x);
        ys.push_back(y);
    }

    std::vector<double>::size_type size() const
    {
        assert(xs.size() == ys.size());
        return xs.size();
    }
};

#if 0
struct TraceView
{
    span<const double> xs;
    span<const double> ys;

    void clear()
    {
        xs = {};
        ys = {};
    }

    span<const double>::size_type size() const
    {
        return xs.size();
    }
};
#endif

using TraceHistory = std::deque<Trace>;
using TraceHistories = std::vector<TraceHistory>;

size_t get_used_memory(const Trace &trace);
size_t get_used_memory(const TraceHistory &traceHistory);
size_t get_used_memory(const TraceHistories &traceHistories);
std::ostream &print_trace(std::ostream &out, const Trace &trace);

std::pair<double, double> find_minmax_y(const Trace &trace);

// scale x values by dtSample
void scale_x_values(const waveforms::Trace &input, waveforms::Trace &output, double dtSample);

}

#endif /* D5F48B2C_D902_44A9_8EBD_36C71D14399D */
