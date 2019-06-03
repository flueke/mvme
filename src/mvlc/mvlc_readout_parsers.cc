#include "mvlc/mvlc_readout_parsers.h"

#include "mvlc/mvlc_buffer_validators.h"
#include "mvlc/mvlc_util.h"

namespace mesytec
{
namespace mvlc
{

VMEConfReadoutInfo parse_event_readout_info(const VMEConfReadoutScripts &rdoScripts)
{
    auto parse_readout_script = [](const vme_script::VMEScript &rdoScript)
    {
        using namespace vme_script;
        enum State { Prefix, Dynamic, Suffix };
        State state = Prefix;
        ModuleReadoutParts modParts = {};

        for (auto &cmd: rdoScript)
        {
            if (cmd.type == CommandType::Read
                || cmd.type == CommandType::Marker)
            {
                switch (state)
                {
                    case Prefix:
                        modParts.prefixLen++;
                        break;
                    case Dynamic:
                        modParts.suffixLen++;
                        state = Suffix;
                        break;
                    case Suffix:
                        modParts.suffixLen++;
                        break;
                }
            }
            else if (is_block_read_command(cmd.type))
            {
                switch (state)
                {
                    case Prefix:
                        modParts.hasDynamic = true;
                        state = Dynamic;
                        break;
                    case Dynamic:
                        throw std::runtime_error("multiple block reads in module readout");
                    case Suffix:
                        throw std::runtime_error("block read after suffix in module readout");
                }
            }
        }

        return modParts;
    };

    VMEConfReadoutInfo result;

    for (const auto &eventScripts: rdoScripts)
    {
        std::vector<ModuleReadoutParts> moduleReadouts;

        for (const auto &moduleScript: eventScripts)
        {
            moduleReadouts.emplace_back(parse_readout_script(moduleScript));
        }

        result.emplace_back(moduleReadouts);
    }

    return result;
}

inline bool event_in_progress(const ReadoutParser_ETH &state)
{
    return state.eventIndex >= 0;
}

ReadoutParser_ETH *make_readout_parser_eth(const VMEConfReadoutScripts &readoutScripts)
{
    auto result = std::make_unique<ReadoutParser_ETH>();
    result->workBuffer = DataBuffer(Megabytes(1));
    result->readoutInfo = parse_event_readout_info(readoutScripts);

    size_t maxModuleCount = 0;

    for (const auto &eventReadoutParts: result->readoutInfo)
    {
        maxModuleCount = std::max(maxModuleCount, eventReadoutParts.size());
    }

    result->readoutDataSpans.resize(maxModuleCount);

    return result.release();
}

inline void clear_readout_data_spans(std::vector<ModuleReadoutSpans> &spans)
{
    std::fill(spans.begin(), spans.end(), ModuleReadoutSpans{});
}

inline void clear_event_parsing_state(ReadoutParser_ETH &state)
{
    state.workBuffer.used = 0;
    clear_readout_data_spans(state.readoutDataSpans);
    state.eventIndex = -1;
    state.moduleIndex = -1;
}

s64 calc_buffer_loss(u32 bufferNumber, u32 lastBufferNumber)
{
    s64 diff = bufferNumber - lastBufferNumber;
    if (diff < 1)
    {
        diff = std::numeric_limits<u32>::max() + diff;
        return diff;
    }
    return diff - 1;
}

// Precondition: iterator points to header0 of the first ETH payload that
// contains a StackBuffer start (0xF3).
// Throws end_of_buffer in case no StackBuffer start is found.




// Find a buffer header matching the given validator starting from the given
// iterators position.
// The precondition is that the iterator is positioned on header0 of an
// ethernet packets data.
//
// Returns a non-negative offset to the matching data header counted from the
// start of the packet payload data in words (same way nextHeaderPointer works)
// if the header was found, -1 otherwise.


s32 eth_find_buffer_header(BufferIterator &iter, BufferHeaderValidator bhv)
{
    while (!iter.atEnd())
    {
        // peek the 2 ETH header words
        eth::PayloadHeaderInfo hdrInfo{ iter.peekU32(0), iter.peekU32(1) };

        if (!hdrInfo.isNextHeaderPointerPresent())
        {
            // skip forward to the next packets header0
            iter.skip(eth::HeaderWords + hdrInfo.dataWordCount(), sizeof(u32));
            continue;
        }

        u16 offset = hdrInfo.nextHeaderPointer();

        // Check all buffer headers within the packets payload.
        while (offset < hdrInfo.dataWordCount())
        {
            u32 header = iter.peekU32(eth::HeaderWords + offset);

            if (bhv(header))
                return offset;

            // Jump forward by the data frames length.
            offset += extract_header_info(header).len;
        }
    }

    return -1;
};

void parse_event(
    ReadoutParser_ETH &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator &iter,
    s32 bufferHeaderOffset)
{
    assert(event_in_progress(state));
    // Event parsing is in progress or has just been started. Read and
    // interpret data according to the parser information and the current
    // state of the output spans and state variables.
    // Once all the modules of the event have been parsed invoke the
    // callbacks then reset the parsing state.

    eth::PayloadHeaderInfo ethHdrs{ iter.peekU32(0), iter.peekU32(1) };

    // Check for packet loss
    if (state.lastPacketNumber >= 0)
    {
        if (auto loss = eth::calc_packet_loss(state.lastPacketNumber,
                                              ethHdrs.packetNumber()))
        {
            // Abort event parsing and skip this packets data.
            clear_event_parsing_state(state);
            iter.skip(eth::HeaderWords + ethHdrs.dataWordCount(), sizeof(u32));
            return;
        }
    }
    state.lastPacketNumber = ethHdrs.packetNumber();

    const auto &moduleReadoutInfos = state.readoutInfo[state.eventIndex];

}

void parse_readout_buffer(
    ReadoutParser_ETH &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, u8 *buffer, size_t bufferSize)
{
    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any data buffered so far is discarded.
        clear_event_parsing_state(state);
    }

    const u8 eventCount = state.readoutInfo.size();

    auto header_validator = [eventCount] (u32 header)
    {
        if (is_stack_buffer(header))
        {
            // Check if the buffer headers stack id is in range of the number
            // of configured vme events.
            u8 stack = extract_header_info(header).stack;
            return (1 <= stack && stack <= eventCount);
        }
        return false;
    };

    BufferIterator iter(buffer, bufferSize);

    while (!iter.atEnd())
    {
        if (!event_in_progress(state))
        {
            s32 offset = eth_find_buffer_header(iter, header_validator);

            if (offset < 0)
                return;

            eth::PayloadHeaderInfo ethHdrs{ iter.peekU32(0), iter.peekU32(1) };
            u32 header = iter.peekU32(eth::HeaderWords + offset);

            // Setup parsing for the event identified by the given stack id. By
            // convention stack 0 is reserved for immediate execution, so
            // event0 is read out using stack1.
            state.eventIndex = extract_header_info(header).stack - 1;
            state.moduleIndex = 0;
            // Loss from the previous packet does not matter anymore as we
            // just started parsing a new event.
            state.lastPacketNumber = -1;
            state.moduleParseState = ReadoutParser_ETH::Prefix;

            assert(event_in_progress(state));

            parse_event(state, callbacks, iter, offset);
        }
        else
        {
            parse_event(state, callbacks, iter, 0);
        }
    }
}

} // end namespace mesytec
} // end namespace mvlc

