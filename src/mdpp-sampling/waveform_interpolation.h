#ifndef C9134348_739A_4F90_B3CA_B790902989BF
#define C9134348_739A_4F90_B3CA_B790902989BF

#include <functional>

#include "libmvme_export.h"
#include "mdpp-sampling/waveform_traces.h"
#include "typedefs.h"

namespace mesytec::mvme::waveforms
{

// Called with an interpolated sample. Can print or store the sample or make
// coffee.
using EmitterFun = std::function<void(double x, double y)>;

// Lowest level interpolation function. Currently hardcoded to use sinc()
// interpolation with a window size of 6. Factor specifies the number of points
// that are interpolated between two samples.
LIBMVME_EXPORT void interpolate_sinc(const span<const double> &xs, const span<const double> &ys,
                                     u32 factor, EmitterFun emitter);

// Reserves temporary memory to construct the xs and ys vectors from the input
LIBMVME_EXPORT void interpolate_sinc(const span<const s16> &samples, double dtSample, u32 factor,
                                     EmitterFun emitter);

// Writes into the output trace. The output trace is cleared before writing.
LIBMVME_EXPORT void interpolate_sinc(const waveforms::Trace &input, waveforms::Trace &output,
                                     u32 factor);

// From raw samples to interpolated trace. The output trace is cleared before writing.
LIBMVME_EXPORT void interpolate_sinc(const span<const s16> &samples, double dtSample, u32 factor,
                                     waveforms::Trace &output);

struct LIBMVME_EXPORT SplineParams
{
    // The spline type. One of "cspline", "cspline_hermite", "linear".
    std::string splineType = "cspline";
    // Enforce monotonicity of the spline if the input is monotonic as well.
    bool makeMonotonic = false;

    // Boundary condition type for the spline end-points.
    // One of "first_deriv", "second_deriv", "not_a_knot".
    std::string boundaryCondLeft = "first_deriv";
    std::string boundaryCondRight = "first_deriv";

    // Target values for the boundary conditions, .e.g "first_deriv", 1.0 means
    // slope will be equal to 1.0 at the boundary.
    double boundaryLeft = 0.0;
    double boundaryRight = 0.0;
};

// Note: This throws if SplineParams contains invalid strings.
LIBMVME_EXPORT void interpolate_spline(const std::vector<double> &xs, const std::vector<double> &ys,
                                       const SplineParams &params, u32 factor, EmitterFun emitter);

LIBMVME_EXPORT void interpolate_spline(const waveforms::Trace &input, waveforms::Trace &output,
                                       const SplineParams &params, u32 factor);

} // namespace mesytec::mvme::waveforms

#endif /* C9134348_739A_4F90_B3CA_B790902989BF */
