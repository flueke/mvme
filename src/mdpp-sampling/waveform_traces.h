#ifndef D5F48B2C_D902_44A9_8EBD_36C71D14399D
#define D5F48B2C_D902_44A9_8EBD_36C71D14399D

#include <cassert>
#include <deque>
#include <unordered_map>
#include <variant>
#include <vector>

#include <mesytec-mvlc/cpp_compat.h>
#include <mesytec-mvlc/util/int_types.h>

namespace mesytec::mvme::waveforms
{

using Sample = std::pair<double, double>;

using mvlc::util::span;
using mvlc::s32;
using mvlc::u32;

struct Trace
{
    using MetaValue = std::variant<s32, u32, double, std::string>;
    using MetaMap = std::unordered_map<std::string, MetaValue>;

    std::vector<double> xs;
    std::vector<double> ys;
    MetaMap meta;

    explicit Trace() = default;

    Trace(const Trace &) = default;
    Trace &operator=(const Trace &) = default;

    Trace(Trace &&) = default;
    Trace &operator=(Trace &&) = default;

    void clear()
    {
        xs.clear();
        ys.clear();
        meta.clear();
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

    bool empty() const
    {
        assert(xs.size() == ys.size());
        return xs.empty();
    }

    bool operator==(const Trace &o) const
    {
        return xs == o.xs && ys == o.ys && meta == o.meta;
    }

    bool operator!=(const Trace &o) const
    {
        return !(*this == o);
    }
};

using TraceHistory = std::deque<Trace>;
using TraceHistories = std::vector<TraceHistory>;

size_t get_used_memory(const Trace &trace);
size_t get_used_memory(const TraceHistory &traceHistory);
size_t get_used_memory(const TraceHistories &traceHistories);
// Produces output compatible with gnuplot's 'plot' command. One xy pair per line.
std::ostream &print_trace(std::ostream &out, const Trace &trace);
// Prints the trace on a single line, using a JSON-like format.
std::ostream &print_trace_compact(std::ostream &out, const Trace &trace);

std::pair<double, double> find_minmax_y(const Trace &trace);

// scale x values by dtSample. Update: now also does x phase correction given a phase value in (0.0, 1.0).
// Could create an extra function for this.
void scale_x_values(const waveforms::Trace &input, waveforms::Trace &output, double dtSample, double phase = 1.0);

// same as scale_x_values but use the index of the sample to calculate the x
// value, ignoring the current input traces x value completely.
void rescale_x_values(waveforms::Trace &input, double dtSample, double phase = 1.0);

// pick a trace from the same column of each row in the trace history
std::vector<const Trace *> get_trace_column(const TraceHistories &traceHistories, size_t traceIndex);

// Returns the delta between x[0] and x[1]. Assumes dx is constant throughout
// the trace! Returns a quiet NaN if the trace contains less than two samples.
double get_trace_dx(const Trace &trace);

std::string trace_meta_to_string(const Trace::MetaMap &meta);

}

#endif /* D5F48B2C_D902_44A9_8EBD_36C71D14399D */
