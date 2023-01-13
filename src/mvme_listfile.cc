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

#include "mvme_listfile.h"
#include <cassert>

const ListfileConstants ListfileConstantsTable[] =
{
/*  ===== VERSION 0 =====
 *
 *  ------- Section (Event) Header ----------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |ttt         eeeessssssssssssssss|
 * +--------------------------------+
 *
 * t =  3 bit section type
 * e =  4 bit event type (== event number/index) for event sections
 * s = 16 bit size in units of 32 bit words (fillwords added to data if needed) -> 256k section max size
 *
 * Section size is the number of following 32 bit words not including the header word itself.

 * Sections with SectionType_Event contain subevents with the following header:

 *  ------- Subevent (Module) Header --------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |              mmmmmm  ssssssssss|
 * +--------------------------------+
 *
 * m =  6 bit module type (typeId from the module_info.json in the templates directory)
 * s = 10 bit size in units of 32 bit words
 *
 * The last word of each event section is the EndMarker (globals.h)
 *
 */
    // [0]
    {
        .Version = 0,
        .FourCC = { '\0', '\0', '\0', '\0', },
        .FirstSectionOffset = 0,

        .SectionMaxWords  = 0xffff,
        .SectionMaxSize   = 0xffff * sizeof(u32),

        .SectionTypeMask  = 0xe0000000u,            // 3 bit section type
        .SectionTypeShift = 29,
        .SectionSizeMask  = 0xffffu,                // 16 bit section size in 32 bit words
        .SectionSizeShift = 0,

        .CrateIndexMask    = 0u,                    // no crate index
        .CrateIndexShift   = 0,

        .EventIndexMask  = 0xf0000u,                // 4 bit event index
        .EventIndexShift = 16,

        .ModuleDataMaxWords  = 0x3ffu,
        .ModuleDataMaxSize   = 0x3ffu * sizeof(u32),
        .ModuleDataSizeMask  = 0x3ffu,              // 10 bit module data size in 32 bit words
        .ModuleDataSizeShift = 0,
        .ModuleTypeMask  = 0x3f000u,                // 6 bit module type
        .ModuleTypeShift = 12,
    },

/*  ===== VERSION 1 =====
 *
 * Differences to version 0:
 * - Starts with the FourCC "MVME" (without a terminating '\0') followed by a
 *   32 bit word containing the listfile version number.
 * - Larger section and subevent sizes: 16 -> 20 bits for sections and 10 -> 20
 *   bits for subevents.
 * - Module type is now 8 bit instead of 6.
 *
 *  ------- Section (Event) Header ----------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |ttteeee     ssssssssssssssssssss|
 * +--------------------------------+
 *
 * t =  3 bit section type
 * e =  4 bit event type (== event number/index) for event sections
 * s = 20 bit size in units of 32 bit words (fillwords added to data if needed) -> 4096k section max size
 *
 * Section size is the number of following 32 bit words not including the header word itself.

 * Sections with SectionType_Event contain subevents with the following header:

 *  ------- Subevent (Module) Header --------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |mmmmmmmm    ssssssssssssssssssss|
 * +--------------------------------+
 *
 * m =  8 bit module type (typeId from the module_info.json in the templates directory)
 * s = 20 bit size in units of 32 bit words
 *
 * The last word of each event section is the EndMarker (globals.h)
 *
 */
    // [1]
    {
        .Version = 1,
        .FourCC = { 'M', 'V', 'M', 'E', },
        .FirstSectionOffset = 8,

        .SectionMaxWords  = 0xfffffu,
        .SectionMaxSize   = 0xfffffu * sizeof(u32),

        .SectionTypeMask  = 0xe0000000u,            // 3 bit section type
        .SectionTypeShift = 29,
        .SectionSizeMask  = 0x000fffffu,            // 20 bit section size in 32 bit words
        .SectionSizeShift = 0,

        .CrateIndexMask    = 0u,                    // no crate index
        .CrateIndexShift   = 0,

        .EventIndexMask    = 0x1e000000u,           // 4 bit event index
        .EventIndexShift   = 25,

        .ModuleDataMaxWords  = 0xfffffu,
        .ModuleDataMaxSize   = 0xfffffu * sizeof(u32),
        .ModuleDataSizeMask  = 0x000fffff,          // 20 bit module data size in 32 bit words
        .ModuleDataSizeShift = 0,
        .ModuleTypeMask  = 0xff000000u,             // 8 bit module type
        .ModuleTypeShift = 24,
    },

/*  ===== VERSION 2 =====
 *
 *  Adds a 3 bit crate index to the Event Section Header and moves the event
 *  index down by 3 bits. This way crate index plus event index form unique,
 *  increasing values.
 *
 *  Also the new SectionType_Pause was added.
 *
 */
    // [2]
    {
        .Version = 2,
        .FourCC = { 'M', 'V', 'M', 'E', },
        .FirstSectionOffset = 8,

        .SectionMaxWords     = 0xfffffu,
        .SectionMaxSize      = 0xfffffu * sizeof(u32),

        .SectionTypeMask     = 0xe0000000u,         // 3 bit section type
        .SectionTypeShift    = 29,
        .SectionSizeMask     = 0x000fffffu,         // 20 bit section size in 32 bit words
        .SectionSizeShift    = 0,

        .CrateIndexMask      = 0x1c000000u,         // 3 bit crate index
        .CrateIndexShift     = 26,

        .EventIndexMask      = 0x03c00000u,         // 4 bit event index
        .EventIndexShift     = 22,

        .ModuleDataMaxWords  = 0xfffffu,
        .ModuleDataMaxSize   = 0xfffffu * sizeof(u32),
        .ModuleDataSizeMask  = 0x000fffffu,         // 20 bit module data size in 32 bit words
        .ModuleDataSizeShift = 0,
        .ModuleTypeMask      = 0xff000000u,         // 8 bit module type
        .ModuleTypeShift     = 24,
    },
};

const ListfileConstants &listfile_constants(u32 version)
{
    if (version >= (sizeof(ListfileConstantsTable) / sizeof(*ListfileConstantsTable)))
        version = CurrentListfileVersion;

    return ListfileConstantsTable[version];
}

namespace
{
    inline u32 mask_and_shift(u32 value, u32 mask, int shift)
    {
        return (value & mask) >> shift;
    }
}

u32 ListfileConstants::getSectionType(u32 sectionHeader) const
{
    return mask_and_shift(sectionHeader, SectionTypeMask, SectionTypeShift);
}

u32 ListfileConstants::getSectionSize(u32 sectionHeader) const
{
    return mask_and_shift(sectionHeader, SectionSizeMask, SectionSizeShift);
}

u32 ListfileConstants::getCrateIndex(u32 eventSectionHeader) const
{
    // added in listfile version 2, return 0 for older versions
    if (!hasCrateIndex())
        return 0u;

    return mask_and_shift(eventSectionHeader, CrateIndexMask, CrateIndexShift);
}

u32 ListfileConstants::getEventIndex(u32 eventSectionHeader) const
{
    return mask_and_shift(eventSectionHeader, EventIndexMask, EventIndexShift);
}

u32 ListfileConstants::getModuleDataSize(u32 moduleDataHeader) const
{
    return mask_and_shift(moduleDataHeader, ModuleDataSizeMask, ModuleDataSizeShift);
}

u32 ListfileConstants::getModuleType(u32 moduleDataHeader) const
{
    return mask_and_shift(moduleDataHeader, ModuleTypeMask, ModuleTypeShift);
}

u32 ListfileConstants::makeEventSectionHeader(u32 eventIndex, u32 crateIndex) const
{
    u32 result = (ListfileSections::SectionType_Event << SectionTypeShift) & SectionTypeMask;

    result |= (eventIndex << EventIndexShift) & EventIndexMask;

    if (hasCrateIndex())
    {
        result |= (crateIndex << CrateIndexShift) & CrateIndexMask;
    }

    assert(getEventIndex(result) == eventIndex);
    if (hasCrateIndex())
        assert(getCrateIndex(result) == crateIndex);

    return result;
}

bool ListfileConstants::hasCrateIndex() const
{
    return CrateIndexMask != 0u;
}
