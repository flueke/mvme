#include "listfile_constants.h"
#include "listfile_constants_impl.h"

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

u32 ListfileConstants::section_size(u32 sectionHeader) const
{
    return mask_and_shift(sectionHeader, SectionSizeMask, SectionSizeShift);
}

u32 ListfileConstants::section_type(u32 sectionHeader) const
{
    return mask_and_shift(sectionHeader, SectionTypeMask, SectionTypeShift);
}

u32 ListfileConstants::event_index(u32 eventSectionHeader) const
{
    return mask_and_shift(eventSectionHeader, EventIndexMask, EventIndexShift);
}

u32 ListfileConstants::module_data_size(u32 moduleDataHeader) const
{
    return mask_and_shift(moduleDataHeader, ModuleDataSizeMask, ModuleDataSizeShift);
}

u32 ListfileConstants::module_type(u32 moduleDataHeader) const
{
    return mask_and_shift(moduleDataHeader, ModuleTypeMask, ModuleTypeShift);
}

