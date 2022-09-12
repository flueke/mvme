#ifndef __MVME_STREAM_PROCESSOR_COUNTERS_H__
#define __MVME_STREAM_PROCESSOR_COUNTERS_H__

#include <QDateTime>
#include <array>
#include "libmvme_export.h"
#include "typedefs.h"
#include "vme_config_limits.h"

struct LIBMVME_EXPORT MVMEStreamProcessorCounters
{
    QDateTime startTime;
    QDateTime stopTime;

    u64 bytesProcessed = 0;
    u32 buffersProcessed = 0;
    u32 buffersWithErrors = 0;
    u32 totalEvents = 0;
    u32 invalidEventIndices = 0;
    u32 suppressedEmptyEvents = 0;

    using ModuleCounters = std::array<u32, MaxVMEModules>;

    std::array<u32, MaxVMEEvents> eventCounters;
    std::array<ModuleCounters, MaxVMEEvents> moduleCounters;
};

#endif /* __MVME_STREAM_PROCESSOR_COUNTERS_H__ */
