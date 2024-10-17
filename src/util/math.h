/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __MVME_UTIL_MATH_H__
#define __MVME_UTIL_MATH_H__

#include <limits>

namespace mesytec::mvme::util
{

static constexpr double make_quiet_nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

// https://stackoverflow.com/a/4609795
template <typename T> int sgn(T val)
{
    return (T(0) < val) - (val < T(0));
}

template<typename T>
bool equals(T a, T b, T epsilon = std::numeric_limits<T>::epsilon())
{
    return std::abs(a - b) < epsilon;
}

}

#endif /* __MVME_UTIL_MATH_H__ */
