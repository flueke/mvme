#ifndef __MVME_ROOT_H__
#define __MVME_ROOT_H__

#include <TObject.h>
#include "../typedefs.h"

struct ModuleEvent: public TObject
{
    ModuleEvent();

    u16 eventIndex;
    u16 moduleIndex;
    std::vector<double> data;

    //u32 size;
    //u32 *data; // [size]

    ClassDef(ModuleEvent, 1);
};

#endif /* __MVME_ROOT_H__ */
