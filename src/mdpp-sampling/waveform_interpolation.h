#ifndef C9134348_739A_4F90_B3CA_B790902989BF
#define C9134348_739A_4F90_B3CA_B790902989BF

#include <cmath>
#include <QVector>
#include <mesytec-mvlc/cpp_compat.h>
#include "typedefs.h"

namespace mesytec::mvme::waveforms
{

using Sample = std::pair<double, double>;

// Called with an interpolated sample. Can print or store the sample or make
// coffee.
using EmitterFun = std::function<void (const Sample &)>;

void interpolate(const mvlc::util::span<const Sample> &samples, u32 factor, EmitterFun emitter);
void interpolate(const mvlc::util::span<const s16> &samples, double dtSample, u32 factor, EmitterFun emitter);

}

#endif /* C9134348_739A_4F90_B3CA_B790902989BF */
