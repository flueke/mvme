#include <gtest/gtest.h>
#include <set>
#include "mvlc/trigger_io_sim.h"

using namespace mesytec::mvme_mvlc::trigger_io;

// Lookup the sampled traces in a default constructed sim. Make sure each
// returned trace is valid and distinct.
TEST(TriggerIOSim, LookupSampledTraces)
{
    std::set<Trace *> traces;

    Sim sim;

    for (unsigned i=0; i<NIM_IO_Count; i++)
    {
        PinAddress pa({ 0u, i + Level0::NIM_IO_Offset, 0u}, PinPosition::Input);
        auto trace = lookup_trace(sim, pa);
        ASSERT_NE(trace, nullptr);
        ASSERT_EQ(traces.find(trace), std::end(traces));
        traces.insert(trace);
    }

    for (unsigned i=0; i<Level0::IRQ_Inputs_Count; i++)
    {
        PinAddress pa({ 0, i + Level0::IRQ_Inputs_Offset, 0}, PinPosition::Input);
        auto trace = lookup_trace(sim, pa);
        ASSERT_NE(trace, nullptr);
        ASSERT_EQ(traces.find(trace), std::end(traces));
        traces.insert(trace);
    }

    for (unsigned i=0; i<Level0::UtilityUnitCount; i++)
    {
        PinAddress pa({ 0, i, 0}, PinPosition::Input);
        auto trace = lookup_trace(sim, pa);
        ASSERT_NE(trace, nullptr);
        ASSERT_EQ(traces.find(trace), std::end(traces));
        traces.insert(trace);
    }

    ASSERT_EQ(traces.size(), sim.sampledTraces.size());
}

// Lookup the simulated L0 traces (output side)
TEST(TriggerIOSim, LookupL0Traces)
{
    std::set<Trace *> outputTraces;

    Sim sim;

    for (unsigned ui=0; ui<Level0::OutputCount; ui++)
    {
        PinAddress pa({ 0, ui, 0}, PinPosition::Output);
        auto trace = lookup_trace(sim, pa);
        ASSERT_NE(trace, nullptr);
        ASSERT_EQ(outputTraces.find(trace), std::end(outputTraces));
        outputTraces.insert(trace);
    }
}

TEST(TriggerIOSim, LookupL1Traces)
{
    std::set<Trace *> outputTraces;

    Sim sim;

    for (unsigned lutIdx=0; lutIdx<Level1::LUTCount; ++lutIdx)
    {
        for (unsigned ii=0; ii<LUT::InputBits; ++ii)
        {
            PinAddress pa({ 1u, lutIdx, ii}, PinPosition::Input);
            auto trace = lookup_trace(sim, pa);
            ASSERT_NE(trace, nullptr);
        }

        for (unsigned oi=0; oi<LUT::OutputBits; ++oi)
        {
            PinAddress pa({ 1u, lutIdx, oi}, PinPosition::Output);
            auto trace = lookup_trace(sim, pa);
            ASSERT_NE(trace, nullptr);
            ASSERT_EQ(outputTraces.find(trace), std::end(outputTraces));
            outputTraces.insert(trace);
        }
    }
}

TEST(TriggerIOSim, LookupL2Traces)
{
    std::set<Trace *> outputTraces;

    Sim sim;

    for (unsigned lutIdx=0; lutIdx<Level2::LUTCount; ++lutIdx)
    {
        for (unsigned ii=0; ii<LUT::InputBits; ++ii)
        {
            PinAddress pa({ 2u, lutIdx, ii}, PinPosition::Input);
            auto trace = lookup_trace(sim, pa);
            ASSERT_NE(trace, nullptr);
        }

        // strobe in
        {
            PinAddress pa({ 2u, lutIdx, LUT::StrobeGGInput}, PinPosition::Input);
            auto trace = lookup_trace(sim, pa);
            ASSERT_NE(trace, nullptr);
        }

        for (unsigned oi=0; oi<LUT::OutputBits; ++oi)
        {
            PinAddress pa({ 2u, lutIdx, oi}, PinPosition::Output);
            auto trace = lookup_trace(sim, pa);
            ASSERT_NE(trace, nullptr);
            ASSERT_EQ(outputTraces.find(trace), std::end(outputTraces));
            outputTraces.insert(trace);
        }

        // strobe out
        {
            PinAddress pa({ 2u, lutIdx, LUT::StrobeGGOutput}, PinPosition::Output);
            auto trace = lookup_trace(sim, pa);
            ASSERT_NE(trace, nullptr);
            ASSERT_EQ(outputTraces.find(trace), std::end(outputTraces));
            outputTraces.insert(trace);
        }
    }
}

TEST(TriggerIOSim, LookupL3InputTraces)
{
    Sim sim;

    for (unsigned ui=0; ui<Level3::UnitCount; ui++)
    {
        // input0
        {
            PinAddress pa({ 3u, ui, 0}, PinPosition::Input);
            auto trace = lookup_trace(sim, pa);
            ASSERT_NE(trace, nullptr);
        }

        // Counters latch input
        if (Level3::CountersOffset <= ui && ui < Level3::CountersOffset + Level3::CountersCount)
        {
            // The latch is not connected by default so the trace must be null.
            PinAddress pa({ 3u, ui, 1}, PinPosition::Input);
            auto trace = lookup_trace(sim, pa);
            ASSERT_EQ(trace, nullptr);

            // Connect the latch using the first choice
            sim.trigIO.l3.connections[ui][1] = 0;

            // Lookup and test again
            trace = lookup_trace(sim, pa);
            ASSERT_NE(trace, nullptr);
        }
    }
}

TEST(TriggerIOSim, LookupL3OutputTraces)
{
    std::set<Trace *> outputTraces;

    Sim sim;

    for (unsigned i=0; i<NIM_IO_Count; i++)
    {
        PinAddress pa({ 3u, i + Level3::NIM_IO_Unit_Offset, 0u}, PinPosition::Output);
        auto trace = lookup_trace(sim, pa);
        ASSERT_NE(trace, nullptr);
        ASSERT_EQ(outputTraces.find(trace), std::end(outputTraces));
        outputTraces.insert(trace);
    }

    for (unsigned i=0; i<ECL_OUT_Count; i++)
    {
        PinAddress pa({ 3u, i + Level3::ECL_Unit_Offset, 0u}, PinPosition::Output);
        auto trace = lookup_trace(sim, pa);
        ASSERT_NE(trace, nullptr);
        ASSERT_EQ(outputTraces.find(trace), std::end(outputTraces));
        outputTraces.insert(trace);
    }
}
