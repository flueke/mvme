#include "mvlc/mvlc_readout_parsers.h"

#include "mvlc/mvlc_buffer_validators.h"
#include "mvlc/mvlc_util.h"

#include <QDebug>

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

inline void parser_clear_event_state(ReadoutParser_ETH &state)
{
    state.workBuffer.used = 0;
    clear_readout_data_spans(state.readoutDataSpans);

    state.eventIndex = -1;

    assert(!event_in_progress(state));
}

inline void parser_begin_event(ReadoutParser_ETH &state, int eventIndex)
{
    state.workBuffer.used = 0;
    state.workBufferOffset = 0;

    clear_readout_data_spans(state.readoutDataSpans);

    state.eventIndex = eventIndex;
    state.moduleIndex = 0;
    // Loss from the previous packet does not matter anymore as we
    // just started parsing a new event.
    state.lastPacketNumber = -1;
    state.moduleParseState = ReadoutParser_ETH::Prefix;

    assert(event_in_progress(state));
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

// Find a buffer header matching the given validator starting from the given
// iterators position.
// The precondition is that the iterator is positioned on header0 of an
// ethernet packets data or on a frame_headers::SystemEvent header.
//
// Returns a non-negative offset to the matching data header counted from the
// start of the packet payload data in words (same way nextHeaderPointer works)
// if the header was found, -1 otherwise.
// In case of a SystemEvent which is accepted by the BufferHeaderValidator an
// offset of 0 is returned and the iterator is positioned on the SystemEvents
// header.


// BHV = BufferHeaderValidator
template<typename BHV>
s32 eth_find_buffer_header(BufferIterator &iter, BHV bhv)
{
    while (!iter.atEnd())
    {
        u32 header0 = iter.peekU32(0);

        if (is_system_event(header0))
        {
            if (bhv(header0))
                return 0;

            iter.skip(extract_frame_info(header0).len, sizeof(u32));
            continue;
        }

        u32 header1 = iter.peekU32(1);

        // We are positioned at the first of the two ETH payload header words.
        // Use the header information for further processing.
        eth::PayloadHeaderInfo hdrInfo{ header0, header1 };

        qDebug("%s: h0=0x%08x, h1=0x%08x, isNextHeaderPointerPresent=%d",
               __PRETTY_FUNCTION__, hdrInfo.header0, hdrInfo.header1,
               hdrInfo.isNextHeaderPointerPresent());

        if (!hdrInfo.isNextHeaderPointerPresent())
        {
            // Skip over this packet
            iter.skip(eth::HeaderWords + hdrInfo.dataWordCount(), sizeof(u32));
            continue;
        }

        u16 offset = hdrInfo.nextHeaderPointer();

        // Check all buffer headers within the packets payload.
        while (offset < hdrInfo.dataWordCount())
        {
            qDebug() << "offset=" << offset << ", dataWordCount=" << hdrInfo.dataWordCount();

            u32 header = iter.peekU32(eth::HeaderWords + offset);

            if (bhv(header))
                return offset;

            // Jump forward by the data frames length.
            offset += extract_frame_info(header).len;
        }
    }

    return -1;
}

#if 0
void parse_event(
    ReadoutParser_ETH &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator &iter,
    s32 frameHeaderOffset)
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
            parser_clear_event_state(state);
            return;
        }
    }
    state.lastPacketNumber = ethHdrs.packetNumber();

    u32 frameHeader = iter.peekU32(frameHeaderOffset);
    auto frameHeaderInfo = extract_frame_info(frameHeader);

    if (frameHeaderInfo.stack - 1 != state.eventIndex)
    {
        // stack id changed. return and parse as a new event.
        parser_clear_event_state(state);
        return;
    }

    const auto &moduleReadoutInfos = state.readoutInfo[state.eventIndex];

}
#endif

template<typename ParserState>
void parse_frame_contents(
    ParserState &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator &iter,
    s32 frameHeaderOffset)
{
    assert(event_in_progress(state));
    assert(frameHeaderOffset >= 0);

    auto frameHeaderInfo = extract_frame_info(iter.peekU32(frameHeaderOffset));

    BufferIterator frameIter(
        reinterpret_cast<u8 *>(iter.indexU32(frameHeaderOffset + 1)),
        frameHeaderInfo.len * sizeof(u32));

    const auto &moduleReadoutInfos = state.readoutInfo[state.eventIndex];
    auto &readoutSpans = state.readoutDataSpans;

    while (!frameIter.atEnd())
    {
        const auto &moduleParts = moduleReadoutInfos[state.moduleIndex];
        auto &moduleSpans = state.readoutDataSpans[state.moduleIndex];

        switch (state.moduleParseState)
        {
            case ReadoutParser_ETH::Prefix:
                if (moduleSpans.prefixSpan.size < moduleParts.prefixLen)
                {
                    if (moduleSpans.prefixSpan.size == 0)
                        moduleSpans.prefixSpan.offset = state.workBufferOffset;

                    u32 dataWord = frameIter.extractU32();
                    *state.workBuffer.indexU32(state.workBufferOffset) = dataWord;
                    state.workBuffer.used += sizeof(u32);
                    moduleSpans.prefixSpan.size++;
                    state.workBufferOffset++;
                }

                if (moduleSpans.prefixSpan.size >= moduleParts.prefixLen)
                {
                    if (moduleParts.hasDynamic)
                        state.moduleParseState = ReadoutParser_ETH::Dynamic;
                    else
                        state.moduleParseState = ReadoutParser_ETH::Suffix;
                }

                break;

            case ReadoutParser_ETH::Dynamic:
                {
                    assert(moduleParts.hasDynamic);
                    u32 blockHeader = frameIter.extractU32();
                    auto blockInfo = extract_frame_info(blockHeader);

                    qDebug("%s: blockHeader=0x%08x", __PRETTY_FUNCTION__, blockHeader);

                    if (moduleSpans.dynamicSpan.size == 0)
                        moduleSpans.dynamicSpan.offset = state.workBufferOffset;

                    for (u16 i = 0; i < blockInfo.len; i++)
                    {
                        u32 dataWord = frameIter.extractU32();
                        *state.workBuffer.indexU32(state.workBufferOffset) = dataWord;
                        state.workBuffer.used += sizeof(u32);
                        moduleSpans.dynamicSpan.size++;
                        state.workBufferOffset++;
                    }

                    if (!(blockInfo.flags & frame_flags::Continue))
                        state.moduleParseState = ReadoutParser_ETH::Suffix;
                }
                break;

            case ReadoutParser_ETH::Suffix:
                if (moduleSpans.suffixSpan.size < moduleParts.suffixLen)
                {
                    if (moduleSpans.suffixSpan.size == 0)
                        moduleSpans.suffixSpan.offset = state.workBufferOffset;

                    u32 dataWord = frameIter.extractU32();
                    *state.workBuffer.indexU32(state.workBufferOffset) = dataWord;
                    state.workBuffer.used += sizeof(u32);
                    moduleSpans.suffixSpan.size++;
                    state.workBufferOffset++;
                }

                if (moduleSpans.suffixSpan.size >= moduleParts.suffixLen)
                    state.moduleIndex++;

                break;
        }

        if (state.moduleIndex >= static_cast<int>(moduleReadoutInfos.size()))
        {
            callbacks.beginEvent(state.eventIndex);

            for (int mi = 0; mi < static_cast<int>(moduleReadoutInfos.size()); mi++)
            {
                const auto &moduleSpans = state.readoutDataSpans[mi];

                if (moduleSpans.prefixSpan.size)
                {
                    callbacks.modulePrefix(
                        state.eventIndex, mi,
                        state.workBuffer.indexU32(moduleSpans.prefixSpan.offset),
                        moduleSpans.prefixSpan.size);
                }

                if (moduleSpans.dynamicSpan.size)
                {
                    callbacks.moduleDynamic(
                        state.eventIndex, mi,
                        state.workBuffer.indexU32(moduleSpans.dynamicSpan.offset),
                        moduleSpans.dynamicSpan.size);
                }

                if (moduleSpans.suffixSpan.size)
                {
                    callbacks.moduleSuffix(
                        state.eventIndex, mi,
                        state.workBuffer.indexU32(moduleSpans.suffixSpan.offset),
                        moduleSpans.suffixSpan.size);
                }
            }

            callbacks.endEvent(state.eventIndex);
            parser_clear_event_state(state);
        }
    }

    iter.buffp = frameIter.buffp;
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
        parser_clear_event_state(state);
    }

    auto header_validator = [&state] (u32 header)
    {
        const u8 eventCount = state.readoutInfo.size();

        auto headerInfo = extract_frame_info(header);
        bool stackInRange = (1 <= headerInfo.stack && headerInfo.stack <= eventCount);

        if (!event_in_progress(state))
        {
            return headerInfo.type == frame_headers::StackFrame && stackInRange;
        }
        else if (event_in_progress(state))
        {
            return headerInfo.type == frame_headers::StackContinuation && stackInRange;
        }

        return false;
    };

    BufferIterator iter(buffer, bufferSize);

#if 0
    ::logBuffer(iter, [] (const QString &str)
    {
        qDebug() << str;
    });
#endif

    while (!iter.atEnd())
    {
        // Handle system frames. UDP packet data won't pass the frameInfo.type
        // check because the upper two bits of the first header word are always
        // 0.
        if (get_frame_type(iter.peekU32(0)) == frame_headers::SystemEvent)
        {
            auto frameInfo = extract_frame_info(iter.peekU32(0));

            if (iter.longwordsLeft() < frameInfo.len + 1)
                throw end_of_buffer();

            callbacks.systemEvent(iter.indexU32(1), frameInfo.len);
            iter.skip(frameInfo.len + 1, sizeof(u32));
            continue;
        }

        // At this point the buffer iterator is positioned on the first of the
        // two ETH payload header words.

        eth::PayloadHeaderInfo ethHdrs{ iter.peekU32(0), iter.peekU32(1) };

        // Ensure that the extracted packet data length actually fits into the
        // input buffer
        if (iter.longwordsLeft() < eth::HeaderWords + ethHdrs.dataWordCount())
            throw end_of_buffer();

        if (state.lastPacketNumber >= 0)
        {
            // Check for packet loss. If there is loss clear the parsing state
            // and restart parsing at the same iterator position.
            if (auto loss = eth::calc_packet_loss(state.lastPacketNumber,
                                                  ethHdrs.packetNumber()))
            {
                parser_clear_event_state(state);
            }
        }

        state.lastPacketNumber = ethHdrs.packetNumber();

        if (!ethHdrs.isNextHeaderPointerPresent())
        {
            // No frame header inside the packet data. Skip over the packet.
            iter.skip(eth::HeaderWords + ethHdrs.dataWordCount(), sizeof(u32));
            continue;
        }

        // TODO:
        //process_packet_data(state, ethHdrs, iter.buffp, iter.longwordsLeft());







#if 0
        s32 frameHeaderOffset = eth_find_buffer_header(iter, header_validator);

        if (frameHeaderOffset < 0) // no more frames in the buffer
            return;

        eth::PayloadHeaderInfo ethHdrs{ iter.peekU32(0), iter.peekU32(1) };

        if (state.lastPacketNumber >= 0)
        {
            // Check for packet loss. If there is loss clear the parsing state
            // and restart parsing at the same iterator position.
            if (auto loss = eth::calc_packet_loss(state.lastPacketNumber,
                                                  ethHdrs.packetNumber()))
            {
                parser_clear_event_state(state);
                continue;
            }
        }

        u32 frameHeader = iter.peekU32(eth::HeaderWords + frameHeaderOffset);
        auto frameHeaderInfo = extract_frame_info(frameHeader);

        if (frameHeaderInfo.type == frame_headers::StackFrame)
        {
            assert(!event_in_progress(state));
            // Setup parsing for the event identified by the given stack id. By
            // convention stack 0 is reserved for immediate execution, so
            // event 0 is read out using stack 1.
            parser_begin_event(state, frameHeaderInfo.stack - 1);
        }
        else if (frameHeaderInfo.type == frame_headers::StackContinuation)
        {
            assert(event_in_progress(state));
        }
        else
        {
            // FIXME
            InvalidCodePath;
        }

        // check for stack id mismatch (should not happen)
        if (frameHeaderInfo.stack - 1 != state.eventIndex)
        {
            parser_clear_event_state(state);
            continue;
        }

        parse_frame_contents(state, callbacks, iter, frameHeaderOffset);

        const auto &eventReadoutInfo = state.readoutInfo[state.eventIndex];
#endif
    }
}

} // end namespace mesytec
} // end namespace mvlc
