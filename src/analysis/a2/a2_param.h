#ifndef __A2_PARAM_H__
#define __A2_PARAM_H__

#include "util/nan.h"

namespace a2
{

/* Bit used as payload of NaN values to identify an invalid parameter.
 * If the bit is not set the NaN was generated as the result of a calculation
 * and the parameter is considered valid.
 */
static const int ParamInvalidBit = 1u << 0;

inline bool is_param_valid(double param)
{
    return !(std::isnan(param) && (get_payload(param) & ParamInvalidBit));
}

inline double invalid_param()
{
    static const double result = make_nan(ParamInvalidBit);
    return result;
}

} // end namespace a2

#endif /* __A2_PARAM_H__ */
