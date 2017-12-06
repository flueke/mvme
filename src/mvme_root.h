#ifndef __MVME_ROOT_H__
#define __MVME_ROOT_H__

#include <TObject.h>
#include "typedefs.h"

struct ModuleEvent: public TObject
{
    ModuleEvent();

    u16 eventIndex;
    u16 moduleIndex;

    /* Can use an embedded vector. ROOT knows how to serialize this. */
    //std::vector<double> data;

    /* Another way: use a special comment to tell ROOT about the data size. */
    u32 size;
    u32 *data; // [size]

    ClassDef(ModuleEvent, 1);
};

#endif /* __MVME_ROOT_H__ */
