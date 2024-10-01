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
#ifndef __A2_UTIL_ASSERT_H__
#define __A2_UTIL_ASSERT_H__

#include <cassert>

#ifndef InvalidCodePath
#define InvalidCodePath assert(!"invalid code path")
#endif
#ifndef InvalidDefaultCase
#define InvalidDefaultCase default: { assert(!"invalid default case"); }
#endif

#ifndef do_and_assert

#ifdef NDEBUG
    #define do_and_assert(x) (x)
#else
    #define do_and_assert(x) assert(x)
#endif

#endif // ifndef do_and_assert

#endif /* __A2_UTIL_ASSERT_H__ */
