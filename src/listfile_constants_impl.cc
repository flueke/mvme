#include "listfile_constants_impl.h"

const ListfileConstants ListfileConstantsTable[] =
{
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
        .EventIndexMask  = 0xf0000u,                 // 4 bit event index
        .EventIndexShift = 16,

        .ModuleDataMaxWords  = 0x3ff,
        .ModuleDataMaxSize   = 0x3ff * sizeof(u32),
        .ModuleDataSizeMask  = 0x3ffu,              // 10 bit module data size in 32 bit words
        .ModuleDataSizeShift = 0,
        .ModuleTypeMask  = 0x3f000u,                // 6 bit module type
        .ModuleTypeShift = 12,
    },

    // [1]
    {
        .Version = 1,
        .FourCC = { 'M', 'V', 'M', 'E', },
        .FirstSectionOffset = 8,

        .SectionMaxWords  = 0xfffff,
        .SectionMaxSize   = 0xfffff * sizeof(u32),

        .SectionTypeMask  = 0xe0000000u,            // 3 bit section type
        .SectionTypeShift = 29,
        .SectionSizeMask  = 0x000fffffu,            // 20 bit section size in 32 bit words
        .SectionSizeShift = 0,
        .EventIndexMask    = 0x1e000000u,            // 4 bit event index
        .EventIndexShift   = 25,

        .ModuleDataMaxWords  = 0xfffff,
        .ModuleDataMaxSize   = 0xfffff * sizeof(u32),
        .ModuleDataSizeMask  = 0x000fffff,          // 20 bit module data size in 32 bit words
        .ModuleDataSizeShift = 0,
        .ModuleTypeMask  = 0xff000000u,             // 8 bit module type
        .ModuleTypeShift = 24,
    }
};
