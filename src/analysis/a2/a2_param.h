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
