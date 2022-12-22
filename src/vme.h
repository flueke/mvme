#ifndef UUID_ac4de271_77a8_495e_b37b_999535689192
#define UUID_ac4de271_77a8_495e_b37b_999535689192
/* VME constants

 * Copyright (C) 2016-2019 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <mesytec-mvlc/vme_constants.h>
#include "typedefs.h"

// The vme constants are now defined in the mesytec-mvlc library. The namespace
// alias is used to stay compatible with the existing mvme code.
namespace vme_address_modes = mesytec::mvlc::vme_amods;

namespace vme
{
    static const unsigned MinIRQ = 1;
    static const unsigned MaxIRQ = 7;
}

#endif
