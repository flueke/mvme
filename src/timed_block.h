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
#ifndef __TIMED_BLOCK_H__
#define __TIMED_BLOCK_H__

#include "typedefs.h"

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

extern float gTimedBlockDurations[];
extern u64 gTimedBlockHitCounts[];

struct TimedBlock
{
    using HighResClock = std::chrono::high_resolution_clock;

    enum Mode
    {
        Mode_RecordDuration,
        Mode_SumDuration
    };

    TimedBlock(TimedBlockId id, Mode mode)
        : start(HighResClock::now())
        , id(id)
        , mode(mode)
    { }

    ~TimedBlock()
    {
        auto end = HighResClock::now();
        std::chrono::duration<float, std::nano> diff = end - start;

        switch (mode)
        {
            case Mode_RecordDuration:
                gTimedBlockDurations[id] = diff.count();
                gTimedBlockHitCounts[id] = 1;
                break;

            case Mode_SumDuration:
                gTimedBlockDurations[id] += diff.count();
                ++gTimedBlockHitCounts[id];
                break;
        }

        qDebug() << __PRETTY_FUNCTION__ << id
            << gTimedBlockDurations[id]
            << gTimedBlockHitCounts[id];
    }

    HighResClock::time_point start;
    TimedBlockId id;
    Mode mode;
};

#ifdef MVME_ENABLE_TIMED_BLOCKS
#define TIMED_BLOCK(id)     TimedBlock timed_block_##id(id, TimedBlock::Mode_RecordDuration)
#define TIMED_BLOCK_SUM(id) TimedBlock timed_block_##id(id, TimedBlock::Mode_SumDuration)
#else
#define TIMED_BLOCK(id)
#define TIMED_BLOCK_SUM(id)
#endif

#endif /* __TIMED_BLOCK_H__ */
