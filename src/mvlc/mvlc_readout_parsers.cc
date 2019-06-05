#include "mvlc/mvlc_readout_parsers.h"

#include "mvlc/mvlc_buffer_validators.h"

#include <QDebug>

#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MVLC_READOUT_PARSER_LOG_LEVEL
#define MVLC_READOUT_PARSER_LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#define LOG_LEVEL_SETTING MVLC_READOUT_PARSER_LOG_LEVEL

#define DO_LOG(level, prefix, fmt, ...)\
do\
{\
    if (LOG_LEVEL_SETTING >= level)\
    {\
        fprintf(stderr, prefix "%s(): " fmt "\n", __FUNCTION__, ##__VA_ARGS__);\
    }\
} while (0);

#define LOG_WARN(fmt, ...)  DO_LOG(LOG_LEVEL_WARN,  "WARN - mvlc_rdop ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DO_LOG(LOG_LEVEL_INFO,  "INFO - mvlc_rdop ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - mvlc_rdop ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DO_LOG(LOG_LEVEL_TRACE, "TRACE - mvlc_rdop ", fmt, ##__VA_ARGS__)

namespace mesytec
{
namespace mvlc
{

ModuleReadoutParts parse_module_readout_script(const vme_script::VMEScript &readoutScript)
{
    using namespace vme_script;
    enum State { Prefix, Dynamic, Suffix };
    State state = Prefix;
    ModuleReadoutParts modParts = {};

    for (auto &cmd: readoutScript)
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
}

VMEConfReadoutInfo parse_vme_readout_info(const VMEConfReadoutScripts &rdoScripts)
{
    VMEConfReadoutInfo result;

    for (const auto &eventScripts: rdoScripts)
    {
        std::vector<ModuleReadoutParts> moduleReadouts;

        for (const auto &moduleScript: eventScripts)
        {
            moduleReadouts.emplace_back(parse_module_readout_script(moduleScript));
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
    result->readoutInfo = parse_vme_readout_info(readoutScripts);

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
    state.eventIndex = -1;
    assert(!event_in_progress(state));
}

inline void parser_begin_event(ReadoutParser_ETH &state, u32 frameHeader)
{
    // Loss from the previous packet does not matter anymore as we
    // just started parsing a new event.
    state.lastPacketNumber = -1; // FIXME: ETH specific

    // FIXME: non ETH specific
    state.workBuffer.used = 0;
    state.workBufferOffset = 0;
    clear_readout_data_spans(state.readoutDataSpans);

    auto frameInfo = extract_frame_info(frameHeader);
    assert(frameInfo.type == frame_headers::StackFrame);

    state.eventIndex = frameInfo.stack - 1;
    state.moduleIndex = 0;
    state.moduleParseState = ReadoutParserCommon::Prefix;
    state.curStackFrame = { frameHeader };
    state.curBlockFrame = {};

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

enum class ParseResult
{
    Ok,
    NoHeaderPresent,
    NoStackFrameFound,

    NotAStackFrame,
    NotABlockFrame,
    NotAStackContinuation,

    StackIndexChanged,
};

//template<typename ParserState>
ParseResult parse_readout_contents(
    //ParserState &state,
    ReadoutParser_ETH &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator &iter)
{
    while (!iter.atEnd())
    {
        if (!state.curStackFrame)
        {
            assert(!state.curBlockFrame);

            u32 frameHeader = iter.extractU32();
            auto frameInfo = extract_frame_info(frameHeader);

            if (event_in_progress(state))
            {
                if (frameInfo.type != frame_headers::StackContinuation)
                    return ParseResult::NotAStackContinuation;

                if (frameInfo.stack - 1 != state.eventIndex)
                    return ParseResult::StackIndexChanged;

                state.curStackFrame = { frameHeader };
            }
            else
            {
                if (frameInfo.type != frame_headers::StackFrame)
                    return ParseResult::NotAStackFrame;

                parser_begin_event(state, frameHeader);
            }
        }

        assert(event_in_progress(state));
        assert(state.curStackFrame);

        const auto &moduleReadoutInfos = state.readoutInfo[state.eventIndex];
        const auto &moduleParts = moduleReadoutInfos[state.moduleIndex];
        auto &moduleSpans = state.readoutDataSpans[state.moduleIndex];

        switch (state.moduleParseState)
        {
            case ReadoutParser_ETH::Prefix:
                if (moduleSpans.prefixSpan.size < moduleParts.prefixLen)
                {
                    if (moduleSpans.prefixSpan.size == 0)
                    {
                        // record the offset of the first word of the prefix
                        moduleSpans.prefixSpan.offset = state.workBufferOffset;
                    }

                    u32 dataWord = iter.extractU32();
                    state.workBuffer.append(dataWord);
                    ++state.workBufferOffset;
                    moduleSpans.prefixSpan.size++;
                    state.workBufferOffset++;
                    state.curStackFrame.consumeWord();
                }

                if (moduleSpans.prefixSpan.size >= moduleParts.prefixLen)
                {
                    if (moduleParts.hasDynamic)
                        state.moduleParseState = ReadoutParser_ETH::Dynamic;
                    else if (moduleParts.suffixLen != 0)
                        state.moduleParseState = ReadoutParser_ETH::Suffix;
                    else
                        state.moduleIndex++; // XXX: explain why (iter.atEnd)!!!!!!
                }

                break;

            case ReadoutParser_ETH::Dynamic:
                {
                    assert(moduleParts.hasDynamic);

                    if (!state.curBlockFrame)
                    {
                        state.curBlockFrame = { iter.extractU32() };
                        state.curStackFrame.consumeWord();

                        if (state.curBlockFrame.info().type != frame_headers::BlockRead)
                        {
                            parser_clear_event_state(state);
                            return ParseResult::NotABlockFrame;
                        }
                    }

                    if (moduleSpans.dynamicSpan.size == 0)
                        moduleSpans.dynamicSpan.offset = state.workBufferOffset;

                    u16 wordCount = std::min(static_cast<u32>(state.curBlockFrame.wordsLeft),
                                             iter.longwordsLeft());

                    for (u16 wi = 0; wi < wordCount; wi++)
                    {
                        u32 dataWord = iter.extractU32();
                        state.workBuffer.append(dataWord);
                        ++state.workBufferOffset;
                        ++moduleSpans.dynamicSpan.size;
                        state.curBlockFrame.consumeWord();
                        state.curStackFrame.consumeWord();
                    }

                    if (state.curBlockFrame.wordsLeft == 0
                        && !(state.curBlockFrame.info().flags & frame_flags::Continue))
                    {

                        if (moduleParts.suffixLen == 0)
                            state.moduleIndex++; // XXX: explain why (iter.atEnd)!!!!!!
                        else
                            state.moduleParseState = ReadoutParser_ETH::Suffix;

                    }
                }
                break;

            case ReadoutParser_ETH::Suffix:
                if (moduleSpans.suffixSpan.size < moduleParts.suffixLen)
                {
                    if (moduleSpans.suffixSpan.size == 0)
                        moduleSpans.suffixSpan.offset = state.workBufferOffset;

                    u32 dataWord = iter.extractU32();
                    state.workBuffer.append(dataWord);
                    ++state.workBufferOffset;
                    moduleSpans.suffixSpan.size++;
                    state.workBufferOffset++;
                    state.curStackFrame.consumeWord();
                }

                if (moduleSpans.suffixSpan.size >= moduleParts.suffixLen)
                    state.moduleIndex++;

                break;
        }

        if (state.moduleIndex >= static_cast<int>(moduleReadoutInfos.size()))
        {
            // flush event

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

    return ParseResult::Ok;
}

inline u32 *find_frame_header(u32 *firstFrameHeader, const u32 *endOfData, u8 frameType)
{
    BufferIterator iter(firstFrameHeader, endOfData-firstFrameHeader);

    try
    {
        while (!iter.atEnd())
        {
            if (get_frame_type(iter.peekU32()) == frameType)
                return iter.indexU32(0);

            iter.skip(extract_frame_info(iter.peekU32()).len + 1, sizeof(u32));
        }
        return nullptr;
    } catch (const end_of_buffer &)
    {
        return nullptr;
    }
};

// IMPORTANT: This function assumes that packet loss is handled on the outside!
// The iterator must be bounded by the packets data.
ParseResult parse_packet(
    ReadoutParser_ETH &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator iter)
{
    eth::PayloadHeaderInfo ethHdrs{ iter.peekU32(0), iter.peekU32(1) };
    const u32 *packetEndPtr = iter.indexU32(eth::HeaderWords + ethHdrs.dataWordCount());
    assert(iter.endp == reinterpret_cast<const u8 *>(packetEndPtr));

    LOG_TRACE("begin parsing packet %u, dataWords=%u", ethHdrs.packetNumber(), ethHdrs.dataWordCount());

#if 0
    ::logBuffer(iter, [](const QString &str)
    {
        LOG_TRACE(" %s", str.toStdString().c_str());
    });
#endif

    // Special case for the ETH readout: find the start of a new event by
    // interpreting the packets nextHeaderPointer value and searching from
    // there.
    if (!event_in_progress(state))
    {
        if (!ethHdrs.isNextHeaderPointerPresent())
        {
            // Not currently parsing an event and no frame header present
            // inside the packet data.
            return ParseResult::NoHeaderPresent;
        }

        // Find the next StackFrame header which is the start of the event data
        u32 *firstFramePtr = iter.indexU32(eth::HeaderWords + ethHdrs.nextHeaderPointer());

        u32 *stackFrame = find_frame_header(
            firstFramePtr, packetEndPtr, frame_headers::StackFrame);

        if (!stackFrame)
            return ParseResult::NoStackFrameFound;

        parser_begin_event(state, *stackFrame);

        // Place the iterator one word past the stackframe header
        iter.buffp = reinterpret_cast<u8 *>(stackFrame + 1);
    }
    else
    {
        // Skip to the first payload contents word, right after the two ETH
        // headers. This can be trailing data words from an already open stack
        // frame or it can be the next frame header.
        iter.skip(eth::HeaderWords, sizeof(u32));
    }

    assert(event_in_progress(state));

    auto ret = parse_readout_contents(state, callbacks, iter);

    LOG_TRACE("end parsing packet %u", ethHdrs.packetNumber());

    return ret;
}

void parse_readout_buffer(
    ReadoutParser_ETH &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, u8 *buffer, size_t bufferSize)
{
    LOG_TRACE("begin parsing buffer %u, size=%lu bytes", bufferNumber, bufferSize);

    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any data buffered so far is discarded.
        parser_clear_event_state(state);
    }

    BufferIterator iter(buffer, bufferSize);

#if 0
    ::logBuffer(iter, [] (const QString &str)
    {
        qDebug() << str;
    });
#endif

    try
    {
        while (!iter.atEnd())
        {
            // Handle system frames. UDP packet data won't pass the frameInfo.type
            // check because the upper two bits of the first header word are always
            // 0.
            if (get_frame_type(iter.peekU32(0)) == frame_headers::SystemEvent)
            {
                auto frameInfo = extract_frame_info(iter.peekU32(0));

                if (iter.longwordsLeft() <= frameInfo.len)
                    throw end_of_buffer();

                callbacks.systemEvent(iter.indexU32(0), frameInfo.len + 1);
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

            ParseResult pr = parse_packet(state, callbacks,
                                          BufferIterator(iter.indexU32(0),
                                                         eth::HeaderWords + ethHdrs.dataWordCount()));

            LOG_TRACE("parse_packet result: %d\n", (int)pr);

            // Skip over the packet ending up either on the next SystemEvent frame
            // or on the next packets header0.
            iter.skip(eth::HeaderWords + ethHdrs.dataWordCount(), sizeof(u32));
        }
    }
    catch (const end_of_buffer &)
    {
        // TODO: count this
        LOG_WARN("parsing buffer %u raised end_of_buffer", bufferNumber);
    }
    catch (const std::runtime_error &e)
    {
        LOG_WARN("parsing buffer %u raised runtime_error: %s", bufferNumber, e.what());
    }

    LOG_TRACE("end parsing buffer %u", bufferNumber);
}

} // end namespace mesytec
} // end namespace mvlc
