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
#ifndef __RATE_MONITOR_SAMPLERS_H__
#define __RATE_MONITOR_SAMPLERS_H__

#include "rate_monitor_base.h"
#include "mvme_stream_processor.h"
#include "sis3153_readout_worker.h"

inline RateMonitorNode *add_group(RateMonitorNode *root, const QString &key, const QString &description)
{
    RateMonitorEntry rme;
    rme.type = RateMonitorEntry::Type::Group;
    rme.description = description;
    return root->putBranch(key, rme);
}

inline RateMonitorNode *add_system_rate(RateMonitorNode *root, const QString &key, RateSamplerPtr sampler)
{
    RateMonitorEntry rme;
    rme.type = RateMonitorEntry::Type::SystemRate;
    rme.sampler = sampler;
    return root->putBranch(key, rme);
}

struct SamplerCollection
{
    virtual RateMonitorNode createTree() = 0;
    virtual ~SamplerCollection() {}
};

struct StreamProcessorSampler: public SamplerCollection
{
    RateSamplerPtr bytesProcessed       = std::make_shared<RateSampler>();
    RateSamplerPtr buffersProcessed     = std::make_shared<RateSampler>();
    RateSamplerPtr buffersWithErrors    = std::make_shared<RateSampler>();
    RateSamplerPtr totalEvents        = std::make_shared<RateSampler>();
    RateSamplerPtr invalidEventIndices  = std::make_shared<RateSampler>();

    using ModuleEntries = std::array<RateSamplerPtr, MaxVMEModules>;
    std::array<RateSamplerPtr, MaxVMEEvents> eventEntries;
    std::array<ModuleEntries, MaxVMEEvents> moduleEntries;

    StreamProcessorSampler()
    {
        for (size_t ei = 0; ei < MaxVMEEvents; ei++)
        {
            eventEntries[ei] = std::make_shared<RateSampler>();

            for (size_t mi = 0; mi < MaxVMEModules; mi++)
            {
                moduleEntries[ei][mi] = std::make_shared<RateSampler>();
            }
        }
    };

    void sample(const MVMEStreamProcessorCounters &counters)
    {
        bytesProcessed->sample(counters.bytesProcessed);
        buffersProcessed->sample(counters.buffersProcessed);
        buffersWithErrors->sample(counters.buffersWithErrors);
        totalEvents->sample(counters.totalEvents);
        invalidEventIndices->sample(counters.invalidEventIndices);

        for (size_t ei = 0; ei < MaxVMEEvents; ei++)
        {
            eventEntries[ei]->sample(counters.eventCounters[ei]);

            for (size_t mi = 0; mi < MaxVMEModules; mi++)
            {
                moduleEntries[ei][mi]->sample(counters.moduleCounters[ei][mi]);
            }
        }
    }

    RateMonitorNode createTree() override
    {
        RateMonitorNode root;
        {
            auto rme(root.data());
            rme->type = RateMonitorEntry::Type::Group;
            rme->description = QSL("Internal system rates generated while processing the mvme data stream on the analysis side.");
        }

        add_system_rate(&root, QSL("bytesProcessed"),        bytesProcessed);
        add_system_rate(&root, QSL("buffersProcessed"),      buffersProcessed);
        add_system_rate(&root, QSL("buffersWithErrors"),     buffersWithErrors);
        add_system_rate(&root, QSL("totalEvents"),           totalEvents);
        add_system_rate(&root, QSL("invalidEventIndices"),   invalidEventIndices);

        auto eventRoot  = add_group(&root, QSL("events"), QSL("Event Trigger Rates"));
        auto moduleRoot = add_group(&root, QSL("modules"), QSL("Module Readout Rates"));

        for (size_t ei = 0; ei < MaxVMEEvents; ei++)
        {
            add_system_rate(eventRoot, QString::number(ei), eventEntries[ei]);

            auto modEventRoot = add_group(moduleRoot, QString::number(ei), QString());

            for (size_t mi = 0; mi < MaxVMEModules; mi++)
            {
                add_system_rate(modEventRoot, QString::number(mi), moduleEntries[ei][mi]);
            }
        }

        return root;
    }
};

struct DAQStatsSampler: public SamplerCollection
{
    RateSamplerPtr totalBytesRead           = std::make_shared<RateSampler>();
    RateSamplerPtr totalBuffersRead         = std::make_shared<RateSampler>();
    RateSamplerPtr buffersWithErrors        = std::make_shared<RateSampler>();
    RateSamplerPtr droppedBuffers           = std::make_shared<RateSampler>();
    RateSamplerPtr listFileBytesWritten     = std::make_shared<RateSampler>();

    void sample(const DAQStats &counters)
    {
        totalBytesRead->sample(counters.totalBytesRead);
        totalBuffersRead->sample(counters.totalBuffersRead);
        buffersWithErrors->sample(counters.buffersWithErrors);
        droppedBuffers->sample(counters.droppedBuffers);
        listFileBytesWritten->sample(counters.listFileBytesWritten);
    }

    RateMonitorNode createTree() override
    {
        RateMonitorNode root;
        {
            auto rme(root.data());
            rme->type = RateMonitorEntry::Type::Group;
            rme->description = QSL("VME readout rates");
        }

        add_system_rate(&root, QSL("totalBytesRead"),       totalBytesRead);
        add_system_rate(&root, QSL("totalBuffersRead"),     totalBuffersRead);
        add_system_rate(&root, QSL("buffersWithErrors"),    buffersWithErrors);
        add_system_rate(&root, QSL("droppedBuffers"),       droppedBuffers);
        add_system_rate(&root, QSL("listFileBytesWritten"), listFileBytesWritten);

        return root;
    }
};

struct SIS3153Sampler: public SamplerCollection
{
    using StackListCountEntries = std::array<RateSamplerPtr, SIS3153Constants::NumberOfStackLists>;

    RateSamplerPtr lostEvents;
    RateSamplerPtr multiEventPackets;
    StackListCountEntries stackListCounts;
    StackListCountEntries stackListBerrCounts_Block;
    StackListCountEntries stackListBerrCounts_Read;
    StackListCountEntries stackListBerrCounts_Write;
    StackListCountEntries embeddedEvents;
    StackListCountEntries partialFragments;
    StackListCountEntries reassembledPartials;

    SIS3153Sampler()
    {
        lostEvents = std::make_shared<RateSampler>();
        multiEventPackets = std::make_shared<RateSampler>();

        for (size_t i = 0; i < std::tuple_size<StackListCountEntries>::value; i++)
        {
            stackListCounts[i]              = std::make_shared<RateSampler>();
            stackListBerrCounts_Block[i]    = std::make_shared<RateSampler>();
            stackListBerrCounts_Read[i]     = std::make_shared<RateSampler>();
            stackListBerrCounts_Write[i]    = std::make_shared<RateSampler>();
            embeddedEvents[i]               = std::make_shared<RateSampler>();
            partialFragments[i]             = std::make_shared<RateSampler>();
            reassembledPartials[i]          = std::make_shared<RateSampler>();
        }
    }

    void sample(const SIS3153ReadoutWorker::Counters &counters)
    {
        lostEvents->sample(counters.lostEvents);
        multiEventPackets->sample(counters.multiEventPackets);

        for (size_t i = 0; i < std::tuple_size<StackListCountEntries>::value; i++)
        {
            stackListCounts[i]->sample(counters.stackListCounts[i]);
            stackListBerrCounts_Block[i]->sample(counters.stackListBerrCounts_Block[i]);
            stackListBerrCounts_Read[i]->sample(counters.stackListBerrCounts_Read[i]);
            stackListBerrCounts_Write[i]->sample(counters.stackListBerrCounts_Write[i]);
            embeddedEvents[i]->sample(counters.embeddedEvents[i]);
            partialFragments[i]->sample(counters.partialFragments[i]);
            reassembledPartials[i]->sample(counters.reassembledPartials[i]);
        }
    }

    RateMonitorNode createTree() override
    {
        RateMonitorNode root;
        {
            auto rme(root.data());
            rme->type = RateMonitorEntry::Type::Group;
            rme->description = QSL("SIS3153 specific counters");
        }

        add_system_rate(&root, QSL("lostEvents"), lostEvents);
        add_system_rate(&root, QSL("multiEventPackets"), multiEventPackets);

        auto stackListRoot = add_group(&root, QSL("stacklists"), QSL("stacklist specific counters"));

        for (size_t si = 0; si < SIS3153Constants::NumberOfStackLists; si++)
        {
            auto groupRoot = add_group(stackListRoot, QString::number(si), QString("stacklist %1").arg(si));

            add_system_rate(groupRoot, QSL("hits"), stackListCounts[si]);
            add_system_rate(groupRoot, QSL("berr_block"), stackListBerrCounts_Block[si]);
            add_system_rate(groupRoot, QSL("berr_read"), stackListBerrCounts_Read[si]);
            add_system_rate(groupRoot, QSL("berr_write"), stackListBerrCounts_Write[si]);
            add_system_rate(groupRoot, QSL("embeddedEvents"), embeddedEvents[si]);
            add_system_rate(groupRoot, QSL("partialFragments"), partialFragments[si]);
            add_system_rate(groupRoot, QSL("reassembledPartials"), reassembledPartials[si]);
        }

        return root;
    }
};

#endif /* __RATE_MONITOR_SAMPLERS_H__ */
