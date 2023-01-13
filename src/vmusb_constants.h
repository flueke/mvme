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
#ifndef UUID_09e438bb_1fac_49bb_b2d5_5c514a191e48
#define UUID_09e438bb_1fac_49bb_b2d5_5c514a191e48

#include "typedefs.h"

namespace vmusb_constants
{

static const std::size_t BufferMaxSize = 27 * 1024;

namespace Buffer
{
    // Header word 1
    static const int LastBufferMask     = (1 << 15);
    static const int IsScalerBufferMask = (1 << 14);
    static const int ContinuousModeMask = (1 << 13);
    static const int MultiBufferMask    = (1 << 12);
    static const int NumberOfEventsMask = 0xfff;

    // Optional 2nd header word
    static const int NumberOfWordsMask  = 0xffff; // it's 16 bits, not 12 as the manual says

    // Event header word
    static const int StackIDShift       = 13;
    static const int StackIDMask        = 7;
    static const int ContinuationMask   = (1 << 12);
    static const int EventLengthMask    = 0xfff;

    /* This appears in the documentation but is not configurable and does not
     * actually appear in the output data stream. */
    //static const int EventTerminator    = 0xaaaa;

    static const u16 BufferTerminator   = 0xffff;
}

namespace GlobalMode
{
    static const int Align32Mask        = (1 << 7);
    static const int HeaderOptMask      = (1 << 8);
}

static const int StackIDMin = 0;
static const int StackIDMax = 7;

// Manual Section 4.5.13
static const u32 BLTMaxTransferCount    = 1u << 23;
static const u32 MBLTMaxTransferCount   = 1u << 22;

namespace NIMO1
{
    namespace Shifts
    {
        static const u32 Code   = 0u;
        static const u32 Invert = 3u;
        static const u32 Latch  = 4u;
    }

    namespace Codes
    {
        static const u32 Busy               = 0u;
        static const u32 EventTrigger       = 1u;
        static const u32 BusRequest         = 2u;
        static const u32 XferToDataBuffer   = 3u;
        static const u32 DGG_A              = 4u;
        static const u32 DGG_B              = 5u;
        static const u32 EndOfEvent         = 6u;
        static const u32 USBTrigger         = 7u;
    }
}

namespace NIMO2
{
    namespace Shifts
    {
        static const u32 Code   =  8u;
        static const u32 Invert = 11u;
        static const u32 Latch  = 12u;
    }

    namespace Codes
    {
        static const u32 USBTrigger         = 0u;
        static const u32 ExecutingVMECmd    = 1u;
        static const u32 VMEAddressStrobe   = 2u;
        static const u32 XferToUSBFIFO      = 3u;
        static const u32 DGG_A              = 4u;
        static const u32 DGG_B              = 5u;
        static const u32 EndOfEvent         = 6u;
        static const u32 USBTrigger2        = 7u;
    }
}

} // end namespace vmusb_constants

#endif
