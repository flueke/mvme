#include <gtest/gtest.h>
#include <set>
#include "mvlc/trigger_io_sim.h"

using namespace mesytec::mvme_mvlc::trigger_io;

// Lookup the sampled traces in a default constructed sim. Make sure each
// returned trace is valid and distinct.
TEST(TriggerIOSim, SampledTracesLookup)
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

    ASSERT_EQ(traces.size(), sim.sampledTraces.size());
}
