#ifndef __MVME_ROOT_WRITER_PROCESS_H__
#define __MVME_ROOT_WRITER_PROCESS_H__

#include "typedefs.h"

namespace mvme_root
{

enum WriterMessageType: s32
{
    BeginRun,
    EndRun,
    BeginEvent,
    EndEvent,
    ModuleData,
    Timetick,

    Count,
};

} // end namespace mvme_root

#endif /* __MVME_ROOT_WRITER_PROCESS_H__ */
