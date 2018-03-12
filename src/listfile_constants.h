#ifndef __LISTFILE_CONSTANTS_H__
#define __LISTFILE_CONSTANTS_H__

#include "libmvme_export.h"
#include "listfile_version.h"

struct LIBMVME_EXPORT ListfileConstants
{
    u32 Version;
    char FourCC[4];

    int FirstSectionOffset;

    int SectionMaxWords;
    u32 SectionMaxSize;

    u32 SectionTypeMask;
    int SectionTypeShift;
    u32 SectionSizeMask;
    int SectionSizeShift;
    int EventIndexMask ;
    int EventIndexShift;

    int ModuleDataMaxWords;
    u32 ModuleDataMaxSize;
    u32 ModuleDataSizeMask;
    int ModuleDataSizeShift;
    u32 ModuleTypeMask;
    int ModuleTypeShift;

    // Mask and shift operations for section and module data header information

    u32 section_size(u32 sectionHeader) const;
    u32 section_type(u32 sectionHeader) const;
    u32 event_index(u32 eventSectionHeader) const;

    u32 module_data_size(u32 moduleDataHeader) const;
    u32 module_type(u32 moduleDataHeader) const;
};

LIBMVME_EXPORT const ListfileConstants &listfile_constants(u32 version = CurrentListfileVersion);

#endif /* __LISTFILE_CONSTANTS_H__ */
