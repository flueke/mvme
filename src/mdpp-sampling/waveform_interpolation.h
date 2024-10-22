#ifndef C9134348_739A_4F90_B3CA_B790902989BF
#define C9134348_739A_4F90_B3CA_B790902989BF

#include <functional>

#include "mdpp-sampling/waveform_traces.h"
#include "typedefs.h"


namespace mesytec::mvme::waveforms
{

// Called with an interpolated sample. Can print or store the sample or make
// coffee.
using EmitterFun = std::function<void (double x, double y)>;

// Lowest level interpolation function. Currently hardcoded to use sinc()
// interpolation with a window size of 6. Factor specifies the number of points
// that are interpolated between two samples.
void interpolate(const span<const double> &xs, const span<const double> &ys, u32 factor, EmitterFun emitter);

// Reserves temporary memory to construct the xs and ys vectors from the input
void interpolate(const mvlc::util::span<const s16> &samples, double dtSample, u32 factor, EmitterFun emitter);

void interpolate(const waveforms::Trace &input, waveforms::Trace &output, u32 factor);

}

#endif /* C9134348_739A_4F90_B3CA_B790902989BF */
