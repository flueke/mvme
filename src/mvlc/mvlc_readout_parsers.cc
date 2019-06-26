#include "mvlc/mvlc_readout_parsers.h"

#include "mvlc/mvlc_buffer_validators.h"

#include <QDebug>

#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MVLC_READOUT_PARSER_LOG_LEVEL
#define MVLC_READOUT_PARSER_LOG_LEVEL LOG_LEVEL_TRACE
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

#define LOG_WARN(fmt, ...)  DO_LOG(LOG_LEVEL_WARN,  "WARN - mvlc_rdo_parser ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DO_LOG(LOG_LEVEL_INFO,  "INFO - mvlc_rdo_parser ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - mvlc_rdo_parser ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DO_LOG(LOG_LEVEL_TRACE, "TRACE - mvlc_rdo_parser ", fmt, ##__VA_ARGS__)

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

inline bool event_in_progress(const ReadoutParserCommon &state)
{
    return state.eventIndex >= 0;
}

template<typename ParserType>
ParserType *make_readout_parser(const VMEConfReadoutScripts &readoutScripts)
{
    auto result = std::make_unique<ParserType>();
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

ReadoutParser_ETH *make_readout_parser_eth(const VMEConfReadoutScripts &readoutScripts)
{
    return make_readout_parser<ReadoutParser_ETH>(readoutScripts);
}

ReadoutParser_USB *make_readout_parser_usb(const VMEConfReadoutScripts &readoutScripts)
{
    return make_readout_parser<ReadoutParser_USB>(readoutScripts);
}

inline void clear_readout_data_spans(std::vector<ModuleReadoutSpans> &spans)
{
    std::fill(spans.begin(), spans.end(), ModuleReadoutSpans{});
}

inline void parser_clear_event_state(ReadoutParserCommon &state)
{
    state.eventIndex = -1;
    assert(!event_in_progress(state));
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
    EventIndexOutOfRange,

    EndOfBuffer
};

inline ParseResult parser_begin_event(ReadoutParserCommon &state, u32 frameHeader)
{
    auto frameInfo = extract_frame_info(frameHeader);

    if (frameInfo.type != frame_headers::StackFrame)
        return ParseResult::NotAStackFrame;

    int eventIndex = frameInfo.stack - 1;

    if (eventIndex < 0 || static_cast<unsigned>(eventIndex) >= state.readoutInfo.size())
        return ParseResult::EventIndexOutOfRange;

    state.workBuffer.used = 0;
    state.workBufferOffset = 0;
    clear_readout_data_spans(state.readoutDataSpans);

    state.eventIndex = eventIndex;
    state.moduleIndex = 0;
    state.moduleParseState = ReadoutParserCommon::Prefix;
    state.curStackFrame = { frameHeader };
    state.curBlockFrame = {};

    assert(event_in_progress(state));
    return ParseResult::Ok;
}

inline ParseResult parser_begin_event(ReadoutParser_ETH &state, u32 frameHeader)
{
    auto result = parser_begin_event(dynamic_cast<ReadoutParserCommon &>(state), frameHeader);

    if (result == ParseResult::Ok)
    {
        // Loss from the previous packet does not matter anymore as we
        // just started parsing a new event.
        state.lastPacketNumber = -1;
    }

    return result;
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

inline void copy_to_workbuffer(ReadoutParserCommon &state, BufferIterator &iter, u32 wordsToCopy)
{
    state.workBuffer.ensureCapacity(wordsToCopy);

    std::memcpy(state.workBuffer.endPtr(), iter.buffp, wordsToCopy * sizeof(u32));

    iter.buffp += wordsToCopy * sizeof(u32);
    state.workBuffer.used += wordsToCopy * sizeof(u32);
    state.workBufferOffset += wordsToCopy;
    state.curStackFrame.wordsLeft -= wordsToCopy;
}

// Checks if the input iterator points to a system frame header. If true the
// systemEvent callback is invoked with the frame header and data and true is
// returned. Also the iterator will be placed on the next word after the system
// frame.
// Otherwise the iterator is left unmodified and false is returned.
// Throws end_of_buffer() if the system frame exceeeds the input buffer size.
inline bool try_handle_system_event(
    ReadoutParserCommon &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator &iter)
{
    if (get_frame_type(iter.peekU32(0)) == frame_headers::SystemEvent)
    {
        auto frameInfo = extract_frame_info(iter.peekU32(0));

        // It should be guaranteed that the whole frame fits into the buffer.
        if (iter.longwordsLeft() <= frameInfo.len)
            throw end_of_buffer("SystemEvent frame exceeds input buffer size.");

        // Pass the frame header itself and the contents to the system event
        // callback.
        callbacks.systemEvent(iter.indexU32(0), frameInfo.len + 1);
        iter.skip(frameInfo.len + 1, sizeof(u32));
        return true;
    }

    return false;
}

template<typename ParserState>
ParseResult parse_readout_contents(
    ParserState &state,
    //ReadoutParser_ETH &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator &iter)
{
    while (!iter.atEnd())
    {
        if (!state.curStackFrame)
        {
            // USB buffers can contain system frames alongside readout
            // generated frames. For ETH buffers the system frames are handled
            // further up in parse_readout_buffer().
            if (try_handle_system_event(state, callbacks, iter))
                continue;

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

                auto pr = parser_begin_event(state, frameHeader);

                if (pr != ParseResult::Ok)
                    return pr;

                assert(event_in_progress(state));
            }
        }

        assert(state.curStackFrame);
        assert(event_in_progress(state));
        assert(state.curStackFrame);

        const auto &moduleReadoutInfos = state.readoutInfo[state.eventIndex];
        const auto &moduleParts = moduleReadoutInfos[state.moduleIndex];
        auto &moduleSpans = state.readoutDataSpans[state.moduleIndex];

        if (moduleParts.prefixLen == 0 && !moduleParts.hasDynamic && moduleParts.suffixLen == 0)
        {
            // The module does not have any of the three parts, its readout is
            // completely empty.
            ++state.moduleIndex;
            goto maybe_flush_event;
        }

        switch (state.moduleParseState)
        {
            case ReadoutParserCommon::Prefix:
                if (moduleSpans.prefixSpan.size < moduleParts.prefixLen)
                {
                    // record the offset of the first word of this span
                    if (moduleSpans.prefixSpan.size == 0)
                        moduleSpans.prefixSpan.offset = state.workBufferOffset;

                    u16 wordsLeftInSpan = moduleParts.prefixLen - moduleSpans.prefixSpan.size;
                    u16 wordsToCopy = std::min({
                        wordsLeftInSpan, state.curStackFrame.wordsLeft,
                        static_cast<u16>(iter.longwordsLeft())});

                    //if (iter.longwordsLeft() < wordsToCopy)
                    //    throw end_of_buffer("ModulePrefixCopy");

                    copy_to_workbuffer(state, iter, wordsToCopy);
                    moduleSpans.prefixSpan.size += wordsToCopy;
                }

                assert(moduleSpans.prefixSpan.size <= moduleParts.prefixLen);

                if (moduleSpans.prefixSpan.size == moduleParts.prefixLen)
                {
                    if (moduleParts.hasDynamic)
                        state.moduleParseState = ReadoutParser_ETH::Dynamic;
                    else if (moduleParts.suffixLen != 0)
                        state.moduleParseState = ReadoutParser_ETH::Suffix;
                    else
                    {
                        // We're done with this module as it does have neither
                        // dynamic nor suffix parts.
                        state.moduleIndex++;
                    }
                }

                break;

            case ReadoutParserCommon::Dynamic:
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

                    // record the offset of the first word of this span
                    if (moduleSpans.dynamicSpan.size == 0)
                        moduleSpans.dynamicSpan.offset = state.workBufferOffset;

                    u32 wordsToCopy = std::min(static_cast<u32>(state.curBlockFrame.wordsLeft),
                                             iter.longwordsLeft());

                    copy_to_workbuffer(state, iter, wordsToCopy);
                    moduleSpans.dynamicSpan.size += wordsToCopy;
                    state.curBlockFrame.wordsLeft -= wordsToCopy;

                    if (state.curBlockFrame.wordsLeft == 0
                        && !(state.curBlockFrame.info().flags & frame_flags::Continue))
                    {

                        if (moduleParts.suffixLen == 0)
                        {
                            // No suffix, we're done with the module
                            state.moduleIndex++;
                        }
                        else
                            state.moduleParseState = ReadoutParser_ETH::Suffix;

                    }
                }
                break;

            case ReadoutParserCommon::Suffix:
                if (moduleSpans.suffixSpan.size < moduleParts.suffixLen)
                {
                    // record the offset of the first word of this span
                    if (moduleSpans.suffixSpan.size == 0)
                        moduleSpans.suffixSpan.offset = state.workBufferOffset;

                    u16 wordsLeftInSpan = moduleParts.suffixLen - moduleSpans.suffixSpan.size;
                    u16 wordsToCopy = std::min({
                        wordsLeftInSpan, state.curStackFrame.wordsLeft,
                        static_cast<u16>(iter.longwordsLeft())});

                    if (iter.longwordsLeft() < wordsToCopy)
                        throw end_of_buffer("ModuleSuffixCopy");

                    copy_to_workbuffer(state, iter, wordsToCopy);
                    moduleSpans.suffixSpan.size += wordsToCopy;
                }

                if (moduleSpans.suffixSpan.size >= moduleParts.suffixLen)
                {
                    // Done with the module
                    state.moduleIndex++;
                }

                break;
        }

maybe_flush_event:

        if (state.moduleIndex >= static_cast<int>(moduleReadoutInfos.size()))
        {
            // All modules have been processed and the event can be flushed.

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
    BufferIterator iter(firstFrameHeader, endOfData - firstFrameHeader);

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

    LOG_TRACE("begin parsing packet %u, dataWords=%u",
              ethHdrs.packetNumber(), ethHdrs.dataWordCount());

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

        // Place the iterator right on the stackframe header.
        // parse_readout_contents() will use this to Move the iterator to the
        // stackframe header just found. parse_readout_contents() will pick
        // this up and use it for parser_begin_event().
        iter.buffp = reinterpret_cast<u8 *>(stackFrame);
    }
    else
    {
        // Skip to the first payload contents word, right after the two ETH
        // headers. This can be trailing data words from an already open stack
        // frame or it can be the next frame continuation header.
        iter.skip(eth::HeaderWords, sizeof(u32));
    }

    try
    {
        auto ret = parse_readout_contents(state, callbacks, iter);

        LOG_TRACE("end parsing packet %u", ethHdrs.packetNumber());

        return ret;
    }
    catch (const end_of_buffer &e)
    {
        LOG_WARN("parsing  packet %u raised end_of_buffer: %s",
                 ethHdrs.packetNumber(), e.what());

        ::logBuffer(BufferIterator(iter.data, iter.size),
                    [] (const QString &str) { qDebug().noquote() << str; });

        throw;
    }

    return ParseResult::Ok;
}

void parse_readout_buffer(
    ReadoutParser_ETH &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, u8 *buffer, size_t bufferSize)
{
    LOG_TRACE("begin parsing ETH buffer %u, size=%lu bytes", bufferNumber, bufferSize);

    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any output data prepared so far is discarded.
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
            if (try_handle_system_event(state, callbacks, iter))
                continue;

            // At this point the buffer iterator is positioned on the first of the
            // two ETH payload header words.
            eth::PayloadHeaderInfo ethHdrs{ iter.peekU32(0), iter.peekU32(1) };

            // Ensure that the extracted packet data length actually fits into the
            // input buffer
            if (iter.longwordsLeft() < eth::HeaderWords + ethHdrs.dataWordCount())
            {
                assert(!"packet data exceeds buffer");
                throw end_of_buffer();
            }

            if (state.lastPacketNumber >= 0)
            {
                // Check for packet loss. If there is loss clear the parsing state
                // and restart parsing at the same iterator position.
                if (auto loss = eth::calc_packet_loss(state.lastPacketNumber,
                                                      ethHdrs.packetNumber()))
                {
                    // TODO: count loss
                    parser_clear_event_state(state);
                }
            }

            state.lastPacketNumber = ethHdrs.packetNumber();

            ParseResult pr = parse_packet(
                state, callbacks,
                BufferIterator(iter.indexU32(0),
                               eth::HeaderWords + ethHdrs.dataWordCount()));

            LOG_TRACE("parse_packet result: %d\n", (int)pr);

            // Skip over the packet ending up either on the next SystemEvent frame
            // or on the next packets header0.
            iter.skip(eth::HeaderWords + ethHdrs.dataWordCount(), sizeof(u32));
        }
    }
    catch (const end_of_buffer &e)
    {
        // TODO: count this
        LOG_WARN("parsing buffer %u raised end_of_buffer: %s", bufferNumber, e.what());
#if 0
        ::logBuffer(BufferIterator(buffer, bufferSize) , [] (const QString &str)
        {
            qDebug().noquote() << str;
        });
#endif
        parser_clear_event_state(state);
    }
    catch (const std::runtime_error &e)
    {
        // TODO: count this
        LOG_WARN("parsing buffer %u raised runtime_error: %s", bufferNumber, e.what());
#if 0
        ::logBuffer(BufferIterator(buffer, bufferSize) , [] (const QString &str)
        {
            qDebug().noquote() << str;
        });
#endif
        parser_clear_event_state(state);
    }

    if (!iter.atEnd())
    {
        // TODO: count bytes left in the iterator
        LOG_WARN("buffer %u: %u words left in buffer after parsing!",
                 bufferNumber, iter.longwordsLeft());
    }

    LOG_TRACE("end parsing ETH buffer %u", bufferNumber);
}

void parse_readout_buffer(
    ReadoutParser_USB &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, u8 *buffer, size_t bufferSize)
{
    LOG_TRACE("begin parsing USB buffer %u, size=%lu bytes", bufferNumber, bufferSize);

    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any output data prepared so far is discarded.
        parser_clear_event_state(state);
    }

    BufferIterator iter(buffer, bufferSize);

    try
    {
        while (!iter.atEnd())
        {
            parse_readout_contents(state, callbacks, iter);
        }
    }
    catch (const end_of_buffer &)
    {
        // TODO: count this
        LOG_WARN("parsing buffer %u raised end_of_buffer", bufferNumber);
        parser_clear_event_state(state);
    }
    catch (const std::runtime_error &e)
    {
        // TODO: count this
        LOG_WARN("parsing buffer %u raised runtime_error: %s", bufferNumber, e.what());
        parser_clear_event_state(state);
    }

    if (!iter.atEnd())
    {
        // TODO: count bytes left in the iterator
    }

    LOG_TRACE("end parsing USB buffer %u", bufferNumber);
}

} // end namespace mesytec
} // end namespace mvlc
