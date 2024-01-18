#ifndef __MVME_STREAM_PROCESSOR_COUNTERS_H__
#define __MVME_STREAM_PROCESSOR_COUNTERS_H__

#include <QDateTime>
#include <array>
#include <mesytec-mvlc/mvlc_readout_parser.h>
#include "libmvme_export.h"
#include "typedefs.h"
#include "vme_config_limits.h"

struct LIBMVME_EXPORT MVMEStreamProcessorCounters
{
    using SystemEventCounts = mesytec::mvlc::readout_parser::ReadoutParserCounters::SystemEventCounts;

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
    SystemEventCounts systemEventCounters;

    // [eventIndex, moduleIndex] -> number of times the module data size
    // extracted from the module header exceeds the amount of data in the input
    // buffer. Only used by the MVMEStreamProcessor for the old mvmelst listfile
    // format.
    std::array<ModuleCounters, MaxVMEEvents> moduleEventSizeExceedsBuffer;
};

#endif /* __MVME_STREAM_PROCESSOR_COUNTERS_H__ */
