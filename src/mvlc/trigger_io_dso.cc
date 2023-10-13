#include "mvlc/trigger_io_dso.h"
#include "mesytec-mvlc/mvlc_constants.h"
#include "mesytec-mvlc/mvlc_dialog.h"
#include "util/math.h"
#include <chrono>

#include <QDebug>

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

namespace
{

static const unsigned UnitNumber = 48;
static const u32 DSOReadAddress = mvlc::SelfVMEAddress + 4;

bool is_fatal(const std::error_code &ec)
{
    return (ec == mvlc::ErrorType::ConnectionError
            || ec == mvlc::ErrorType::ProtocolError);
}

void add_self_write(mvlc::StackCommandBuilder &sb, u32 address, u16 value)
{
    sb.addVMEWrite(mvlc::SelfVMEAddress + address, value,
        mvlc::vme_amods::A32, mvlc::VMEDataWidth::D16);
}

std::error_code start_dso(mvlc::MVLC &mvlc, DSOSetup setup)
{
    mvlc::StackCommandBuilder sb;
    sb.addWriteMarker(0xabcdef00);
    add_self_write(sb, 0x0200, UnitNumber); // select DSO unit
    add_self_write(sb, 0x0300, setup.preTriggerTime);
    add_self_write(sb, 0x0302, setup.postTriggerTime);
    add_self_write(sb, 0x0304, setup.nimTriggers.to_ulong());
    add_self_write(sb, 0x0308, setup.irqTriggers.to_ulong());
    add_self_write(sb, 0x030A, setup.utilTriggers.to_ulong());
    add_self_write(sb, 0x0306, 0); // stop capturing
    add_self_write(sb, 0x0306, 1); // start capturing
    std::vector<u32> dest;
    return mvlc.stackTransaction(sb, dest);
}

std::error_code stop_dso(mvlc::MVLC &mvlc)
{
    mvlc::StackCommandBuilder sb;
    sb.addWriteMarker(0xabcdef01);
    add_self_write(sb, 0x0200, UnitNumber); // select DSO unit
    add_self_write(sb, 0x0306, 0); // stop the DSO
    std::vector<u32> dest;
    return mvlc.stackTransaction(sb, dest);
}

std::error_code read_dso(mvlc::MVLC &mvlc, std::vector<u32> &dest)
{
    // block read
    return mvlc.vmeBlockRead(
        DSOReadAddress, mvlc::vme_amods::MBLT64,
        std::numeric_limits<u16>::max(), dest);
}
}

CombinedTriggers
get_combined_triggers(const DSOSetup &setup)
{
    CombinedTriggers result;

    assert(result.size() == setup.nimTriggers.size() + setup.irqTriggers.size() + setup.utilTriggers.size());

    size_t bitIndex = 0;

    for (size_t i=0; i<setup.nimTriggers.size(); ++i, ++bitIndex)
        result.set(bitIndex, setup.nimTriggers.test(i));

    for (size_t i=0; i<setup.irqTriggers.size(); ++i, ++bitIndex)
        result.set(bitIndex, setup.irqTriggers.test(i));

    for (size_t i=0; i<setup.utilTriggers.size(); ++i, ++bitIndex)
        result.set(bitIndex, setup.utilTriggers.test(i));

    return result;
}

void
set_combined_triggers(DSOSetup &setup, const CombinedTriggers &combinedTriggers)
{
    size_t cIndex = 0;

    for (size_t i=0; i<setup.nimTriggers.size(); ++i, ++cIndex)
    {
        assert(cIndex < combinedTriggers.size());
        setup.nimTriggers.set(i, combinedTriggers.test(cIndex));
    }

    for (size_t i=0; i<setup.irqTriggers.size(); ++i, ++cIndex)
    {
        assert(cIndex < combinedTriggers.size());
        setup.irqTriggers.set(i, combinedTriggers.test(cIndex));
    }

    for (size_t i=0; i<setup.utilTriggers.size(); ++i, ++cIndex)
    {
        assert(cIndex < combinedTriggers.size());
        setup.utilTriggers.set(i, combinedTriggers.test(cIndex));
    }

    assert(cIndex == combinedTriggers.size());
}

std::error_code acquire_dso_sample(
    mvlc::MVLC mvlc, DSOSetup setup,
    std::vector<u32> &dest,
    std::atomic<bool> &cancel)
{
    // Minimum size of a DSO buffer containing data.
    // Static data from read_dso(): 0xF3 Stack, StackReferenceMarker, 0xF5 Block
    static const size_t DSOBufferMinSize = 4;

    // start, then read until we get a sample or time out, then stop

    if (auto ec = start_dso(mvlc, setup))
        return ec;

    dest.clear();

    while (!cancel && dest.size() < DSOBufferMinSize)
    {
        dest.clear();
        auto ec = read_dso(mvlc, dest);

        if (is_fatal(ec))
            return ec;
    }

    if (auto ec = stop_dso(mvlc))
        return ec;

    if (dest.size() < DSOBufferMinSize)
    {
        if (cancel)
            return make_error_code(std::errc::operation_canceled);
    }

    return {};
}

Snapshot fill_snapshot_from_dso_buffer(const std::vector<u32> &buffer)
{
    using namespace std::chrono_literals;

    if (buffer.size() < 2 + 2 + 1)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "got a short buffer";
        return {};
    }

    auto dsoHeader = std::find_if(
        std::begin(buffer), std::end(buffer), [] (u32 word) { return word == data_format::Header; });

    auto dsoEnd = std::find_if(
        dsoHeader, std::end(buffer), [] (u32 word) { return word == data_format::EoE; });

    if (dsoHeader == std::end(buffer) || dsoEnd == std::end(buffer))
        return {};

    Snapshot result;
    result.reserve(DSOExpectedSampledTraces);

    for (auto it = dsoHeader+1; it < dsoEnd; ++it)
    {
        const u32 word = *it;
        auto ft = mvlc::get_frame_type(word);

        // Skip over embedded stack and block frames
        if (ft == mvlc::frame_headers::StackFrame
            || ft == mvlc::frame_headers::StackContinuation
            || ft == mvlc::frame_headers::BlockRead)
            continue;

        const auto entry = extract_dso_entry(word);

        //qDebug("entry: addr=%u, time=%u, edge=%s",
        //       entry.address, entry.time, to_string(entry.edge));

        if (entry.address >= result.size())
            result.resize(entry.address + 1);

        assert(entry.address < result.size());

        auto &timeline = result[entry.address];
        timeline.push_back({std::chrono::nanoseconds(entry.time), entry.edge});
    }

    return result;
}

std::vector<bool> remove_trace_overflow_markers(Snapshot &sampledTraces)
{
    std::vector<bool> result;
    result.reserve(sampledTraces.size());

    for (auto &trace: sampledTraces)
    {
        if (has_overflow_marker(trace))
        {
            result.emplace_back(true);
            trace.pop_front();
        }
        else
        {
            result.emplace_back(false);
        }
    }

    assert(result.size() == sampledTraces.size());

    return result;
}

/* Korrektur des Flankenjitters der Triggerflanke:
 * Von der pretrigger_time werden die untersten 3 Bits nicht verwendet. Sie
 * darf aber schon auf jeden Wert gesetzt werden, es spielt keine Rolle.Also
 * für alle Berechnungen die untersten 3 Bits auf 000 setzen.
 * - nach pretrigger_time [15:3] == edge_time[15:3] suchen. (Es muß eine
 *   steigende Flanke sein, sonst sind die Daten kaputt)
 * - Wert der untersten 3Bits dieser gefundenen trigger edge time von allen
 *   edge times des gleichen trails abzienen. Der trigger edge liegt jetzt auf
 *   0, die anderen Flanken sind korrigiert.
 */
std::pair<unsigned, bool> calculate_jitter_value(const Snapshot &snapshot, const DSOSetup &dsoSetup)
{
    auto combinedTriggers = get_combined_triggers(dsoSetup);

    const unsigned maskedPreTrig = dsoSetup.preTriggerTime & (~0b111);

    for (size_t traceIdx=0;
         traceIdx<snapshot.size() && traceIdx < combinedTriggers.size();
         traceIdx++)
    {
        if (!combinedTriggers.test(traceIdx))
            continue;

        auto &trace = snapshot[traceIdx];

        for (auto &sample: trace)
        {
            if (sample.edge == Edge::Rising)
            {
                unsigned maskedSampleTime = static_cast<unsigned>(sample.time.count()) & (~0b111);

                if (maskedPreTrig == maskedSampleTime)
                {
                    const auto jitter = static_cast<unsigned>(sample.time.count()) & 0b111;
                    //qDebug() << "calculate_jitter_value: preTrig =" << dsoSetup.preTriggerTime
                    //    << ", maskedPreTrig =" << maskedPreTrig
                    //    << ", sampleTime =" << sample.time.count()
                    //    << ", maskedSampleTime =" << maskedSampleTime
                    //    << ", resulting jitter =" << jitter
                    //    ;
                    return std::make_pair(jitter, true);
                }
            }
        }
    }

    return std::make_pair(0u, false);
}

void
jitter_correct_dso_snapshot(
    Snapshot &snapshot,
    const DSOSetup &dsoSetup)
{
    // Jitter correction
    auto jitterResult = calculate_jitter_value(snapshot, dsoSetup);
    unsigned jitter = jitterResult.first;

    if (jitter != 0)
    {
        for (auto &trace: snapshot)
        {
            // Never correct the first sample: it is either 0 or 1 (the latter
            // to indicate overflow).
            //bool isFirstSample = true;

            for (auto &sample: trace)
            {
                //if (!isFirstSample && sample.time != SampleTime::zero())
                if (sample.time.count() >= jitter)
                    sample.time = SampleTime(sample.time.count() - jitter);

                //isFirstSample = false;
            }
        }
    }
}

Edge edge_at(const Trace &trace, const SampleTime &t)
{
    Edge result = Edge::Falling;

    for (const auto &sample: trace)
    {
        if (sample.time <= t)
            result = sample.edge;
        else
            break;
    }

    return result;
}

namespace
{
std::vector<PinAddress> make_trace_index_to_pin_list()
{
    std::vector<PinAddress> result;

    // 14 NIMs
    for (auto i=0u; i<NIM_IO_Count; ++i)
    {
        UnitAddress unit = { 0, i+Level0::NIM_IO_Offset, 0 };
        result.push_back({ unit, PinPosition::Input });
    }

    // 6 IRQs
    for (auto i=0u; i<Level0::IRQ_Inputs_Count; ++i)
    {
        UnitAddress unit = { 0, i+Level0::IRQ_Inputs_Offset, 0 };
        result.push_back({ unit, PinPosition::Input });
    }

    // 16 Utility traces
    for (auto i=0u; i<Level0::UtilityUnitCount; ++i)
    {
        UnitAddress unit = { 0, i, 0 };
        result.push_back({ unit, PinPosition::Input });
    }

    return result;
}
}

const std::vector<PinAddress> &trace_index_to_pin_list()
{
    static const auto result = make_trace_index_to_pin_list();
    return result;
}

int get_trace_index(const PinAddress &pa)
{
    const auto &lst = trace_index_to_pin_list();

    auto it = std::find(std::begin(lst), std::end(lst), pa);

    if (it == std::end(lst))
        return -1;

    return it - std::begin(lst);
}

QString get_trigger_default_name(unsigned combinedTriggerIndex)
{
    const auto &pinList = trace_index_to_pin_list();

    assert(combinedTriggerIndex < pinList.size());

    if (combinedTriggerIndex < pinList.size())
    {
        auto pa = pinList.at(combinedTriggerIndex);

        assert(get_trace_index(pa) == static_cast<int>(combinedTriggerIndex));
        assert(pa.unit[0] == 0);
        assert(pa.unit[1] < Level0::DefaultUnitNames.size());

        if (pa.unit[1] < Level0::DefaultUnitNames.size())
            return Level0::DefaultUnitNames.at(pa.unit[1]);
    }

    return {};
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
