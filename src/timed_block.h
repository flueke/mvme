#ifndef __TIMED_BLOCK_H__
#define __TIMED_BLOCK_H__

#include <chrono>
#include <QDebug>

enum TimedBlockId
{
    TimedBlockId_MEP_Analysis_Loop,
    TimedBlockId_MEP_Analysis_BeginEvent,
    TimedBlockId_MEP_Analysis_EndEvent,
    TimedBlockId_MEP_Analysis_ModuleData,

    TimedBlockId_Max
};

extern double gTimedBlockData[];

struct TimedBlock
{
    using HighResClock = std::chrono::high_resolution_clock;

    TimedBlock(TimedBlockId id)
        : id(id)
        , start(HighResClock::now())
    { }

    ~TimedBlock()
    {
        end = HighResClock::now();
        std::chrono::duration<double, std::nano> diff = end - start;
        gTimedBlockData[id] = diff.count();
        qDebug() << "end timed block" << id << gTimedBlockData[id] << "ns";
    }

    TimedBlockId id;
    HighResClock::time_point start;
    HighResClock::time_point end;
};

#endif /* __TIMED_BLOCK_H__ */
