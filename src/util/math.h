#ifndef __MVME_UTIL_MATH_H__
#define __MVME_UTIL_MATH_H__

#include <limits>

namespace mvme
{
namespace util
{

static constexpr double make_quiet_nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

}
}

#endif /* __MVME_UTIL_MATH_H__ */
