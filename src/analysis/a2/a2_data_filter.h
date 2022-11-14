/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __A2_DATA_FILTER__
#define __A2_DATA_FILTER__

#include <mesytec-mvlc/util/data_filter.h>
#include "util/bits.h"

#include <string>
#include <array>

namespace a2
{
namespace data_filter
{

using mesytec::mvlc::util::FilterSize;
using mesytec::mvlc::util::DataFilter;
using mesytec::mvlc::util::CacheEntry;
using mesytec::mvlc::util::make_filter;
using mesytec::mvlc::util::matches;
using mesytec::mvlc::util::make_cache_entry;
using mesytec::mvlc::util::extract;
using mesytec::mvlc::util::get_extract_bits;
using mesytec::mvlc::util::get_extract_mask;
using mesytec::mvlc::util::get_extract_shift;
using mesytec::mvlc::util::to_string;

} // namespace data_filter
} // namespace a2

#endif /* __A2_DATA_FILTER__ */
