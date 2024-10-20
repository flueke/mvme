#ifndef C9134348_739A_4F90_B3CA_B790902989BF
#define C9134348_739A_4F90_B3CA_B790902989BF

#include <cmath>
#include <deque>
#include <QVector>
#include <mesytec-mvlc/cpp_compat.h>
#include "typedefs.h"

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
        return xs.size();
    }
};

using TraceHistory = std::deque<Trace>;
using TraceHistories = std::vector<TraceHistory>;

// Called with an interpolated sample. Can print or store the sample or make
// coffee.
using EmitterFun = std::function<void (double x, double y)>;

// Lowest level interpolation function. Currently hardcoded to use sinc()
// interpolation with a window size of 6.
void interpolate(const span<const double> &xs, const span<const double> &ys, u32 factor, EmitterFun emitter);

// Reserves temporary memory to construct the xs and ys vectors.
void interpolate(const mvlc::util::span<const s16> &samples, double dtSample, u32 factor, EmitterFun emitter);

}

#endif /* C9134348_739A_4F90_B3CA_B790902989BF */
