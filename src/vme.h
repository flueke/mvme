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

#include "typedefs.h"

// Constants for the known privileged and user vme address modifiers.
namespace vme_address_modes
{
    // a16
    static const uint8_t a16User   = 0x29;
    static const uint8_t a16Priv   = 0x2D;

    // a32
    static const u8 a32UserData    = 0x09;
    static const u8 a32UserProgram = 0x0A;
    static const u8 a32UserBlock   = 0x0B;
    static const u8 a32UserBlock64 = 0x08;

    static const u8 a32PrivData    = 0x0D;
    static const u8 a32PrivProgram = 0x0E;
    static const u8 a32PrivBlock   = 0x0F;
    static const u8 a32PrivBlock64 = 0x0C;

    // a24
    static const u8 a24UserData    = 0x39;
    static const u8 a24UserProgram = 0x3A;
    static const u8 a24UserBlock   = 0x3B;

    static const u8 a24PrivData    = 0x3D;
    static const u8 a24PrivProgram = 0x3E;
    static const u8 a24PrivBlock   = 0x3F;

    static const u8 cr             = 0x2F;

    // default modes
    static const u8 A16         = a16User;
    static const u8 A24         = a24UserData;
    static const u8 A32         = a32UserData;
    static const u8 BLT32       = a32UserBlock;
    static const u8 MBLT64      = a32UserBlock64;
    static const u8 Blk2eSST64  = 0x20;

    inline bool is_block_amod(u8 amod)
    {
        switch (amod)
        {
            case a32UserBlock:
            case a32UserBlock64:
            case a32PrivBlock:
            case a32PrivBlock64:
            case a24UserBlock:
            case Blk2eSST64:
                return true;
        }

        return false;
    }

    inline bool is_mblt_mode(u8 amod)
    {
        switch (amod)
        {
            case a32UserBlock64:
            case a32PrivBlock64:
                return true;
        }

        return false;
    }
}

namespace vme
{
    static const unsigned MinIRQ = 1;
    static const unsigned MaxIRQ = 7;
}

#endif
