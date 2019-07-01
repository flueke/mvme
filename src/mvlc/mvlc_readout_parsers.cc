#include "mvlc/mvlc_readout_parsers.h"

#include "mvlc/mvlc_buffer_validators.h"

#include <QDebug>

#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MVLC_READOUT_PARSER_LOG_LEVEL
#define MVLC_READOUT_PARSER_LOG_LEVEL LOG_LEVEL_WARN
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

const char *get_parse_result_name(const ParseResult &pr)
{
    switch (pr)
    {
        case ParseResult::Ok:
            return "Ok";

        case ParseResult::NoHeaderPresent:
            return "NoHeaderPresent";

        case ParseResult::NoStackFrameFound:
            return "NoStackFrameFound";

        case ParseResult::NotAStackFrame:
            return "NotAStackFrame";

        case ParseResult::NotABlockFrame:
            return "NotABlockFrame";

        case ParseResult::NotAStackContinuation:
            return "NotAStackContinuation";

        case ParseResult::StackIndexChanged:
            return "StackIndexChanged";

        case ParseResult::EventIndexOutOfRange:
            return "EventIndexOutOfRange";

        case ParseResult::EmptyStackFrame:
            return "EmptyStackFrame";

        case ParseResult::ParseResultMax:
            break;
    }

    return "UnknownParseResult";
}

static const size_t WorkBufferSize = Megabytes(1);

ReadoutParserState make_readout_parser(const VMEConfReadoutScripts &readoutScripts)
{
    ReadoutParserState result;
    result.workBuffer = DataBuffer(WorkBufferSize);
    result.readoutInfo = parse_vme_readout_info(readoutScripts);

    size_t maxModuleCount = 0;

    for (const auto &eventReadoutParts: result.readoutInfo)
    {
        maxModuleCount = std::max(maxModuleCount, eventReadoutParts.size());
    }

    result.readoutDataSpans.resize(maxModuleCount);

    return result;
}

inline void clear_readout_data_spans(std::vector<ModuleReadoutSpans> &spans)
{
    std::fill(spans.begin(), spans.end(), ModuleReadoutSpans{});
}

inline bool is_event_in_progress(const ReadoutParserState &state)
{
    return state.eventIndex >= 0;
}


inline void parser_clear_event_state(ReadoutParserState &state)
{
    state.eventIndex = -1;
    state.moduleIndex = -1;
    state.curStackFrame = {};
    state.curBlockFrame = {};
    assert(!is_event_in_progress(state));
}

inline ParseResult parser_begin_event(ReadoutParserState &state, u32 frameHeader)
{
    assert(!is_event_in_progress(state));

    auto frameInfo = extract_frame_info(frameHeader);

    if (frameInfo.type != frame_headers::StackFrame)
        return ParseResult::NotAStackFrame;

    int eventIndex = frameInfo.stack - 1;

    if (eventIndex < 0 || static_cast<unsigned>(eventIndex) >= state.readoutInfo.size())
        return ParseResult::EventIndexOutOfRange;

    if (frameInfo.len == 0)
        return ParseResult::EmptyStackFrame;

    state.workBuffer.used = 0;
    state.workBufferOffset = 0;
    clear_readout_data_spans(state.readoutDataSpans);

    state.eventIndex = eventIndex;
    state.moduleIndex = 0;
    state.moduleParseState = ReadoutParserState::Prefix;
    state.curStackFrame = { frameHeader };
    state.curBlockFrame = {};

    // Loss from the previous packet does not matter anymore as we
    // just started parsing a new event.
    state.lastPacketNumber = -1;

    assert(is_event_in_progress(state));
    return ParseResult::Ok;
}

inline s64 calc_buffer_loss(u32 bufferNumber, u32 lastBufferNumber)
{
    s64 diff = bufferNumber - lastBufferNumber;

    if (diff < 1) // overflow
    {
        diff = std::numeric_limits<u32>::max() + diff;
        return diff;
    }
    return diff - 1;
}

inline void copy_to_workbuffer(ReadoutParserState &state, BufferIterator &iter, u32 wordsToCopy)
{
    assert(wordsToCopy <= iter.longwordsLeft());

    state.workBuffer.ensureFreeSpace(wordsToCopy);

    std::memcpy(state.workBuffer.endPtr(), iter.buffp, wordsToCopy * sizeof(u32));

    iter.buffp += wordsToCopy * sizeof(u32);
    state.workBuffer.used += wordsToCopy * sizeof(u32);
    state.workBufferOffset += wordsToCopy;
    state.curStackFrame.wordsLeft -= wordsToCopy;
}

// Checks if the input iterator points to a system frame header. If true the
// systemEvent callback is invoked with the frame header + frame data and true
// is returned. Also the iterator will be placed on the next word after the
// system frame.
// Otherwise the iterator is left unmodified and false is returned.
// Throws end_of_buffer() if the system frame exceeeds the input buffer size.
inline bool try_handle_system_event(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator &iter)
{
    if (get_frame_type(iter.peekU32(0)) == frame_headers::SystemEvent)
    {
        u32 frameHeader = iter.peekU32();
        auto frameInfo = extract_frame_info(frameHeader);

        // It should be guaranteed that the whole frame fits into the buffer.
        if (iter.longwordsLeft() <= frameInfo.len)
            throw end_of_buffer("SystemEvent frame exceeds input buffer size.");

        u8 subtype = system_event::extract_subtype(frameHeader);
        ++state.counters.systemEventTypes[subtype];

        // Pass the frame header itself and the contents to the system event
        // callback.
        callbacks.systemEvent(iter.indexU32(0), frameInfo.len + 1);
        iter.skipExact(frameInfo.len + 1, sizeof(u32));
        return true;
    }

    return false;
}

// This is called with an iterator over a full USB buffer or with an iterator
// limited to the payload from a single UDP packet.
ParseResult parse_readout_contents(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator &iter,
    bool is_eth)
{
    while (!iter.atEnd())
    {
        // Find a stack frame matching the current parser state. Return if no
        // matching frame is detected at the current iterator position.
        if (!state.curStackFrame)
        {
            // If there's no open stack frame there should be no open block
            // frame either. Als data from any open blocks must've been
            // consumed previously or the block frame should have been manually
            // invalidated. XXX: ensure the reset is happening when handling errors
            assert(!state.curBlockFrame);

            // USB buffers can contain system frames alongside readout
            // generated frames. For ETH buffers the system frames are handled
            // further up in parse_readout_buffer() and may not be handled here
            // because the iterator can start with data from the last frame
            // right away whereas USB buffer iterators always start on a frame
            // header.
            if (!is_eth && try_handle_system_event(state, callbacks, iter))
                continue;

            if (is_event_in_progress(state))
            {
                // Leave the frame header in the buffer for now. In case of an
                // 'early error return' the caller can modify the state and
                // retry parsing from the same position.
                auto frameInfo = extract_frame_info(iter.peekU32());

                if (frameInfo.type != frame_headers::StackContinuation)
                    return ParseResult::NotAStackContinuation;

                if (frameInfo.stack - 1 != state.eventIndex)
                    return ParseResult::StackIndexChanged;

                if (frameInfo.len == 0)
                    return ParseResult::EmptyStackFrame;

                // The stack frame is ok and can now be extracted from the
                // buffer.
                state.curStackFrame = { iter.extractU32() };
            }
            else
            {
                // No event is in progress. This means the frame header needs
                // to be extracted from the buffer to ensure progress. It's no
                // use for the caller to reset the state and retry as that
                // would lead to the same result.
                auto pr = parser_begin_event(state, iter.extractU32());

                if (pr != ParseResult::Ok)
                    return pr;

                assert(is_event_in_progress(state));
            }
        }

        assert(is_event_in_progress(state));
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
            case ReadoutParserState::Prefix:
                if (moduleSpans.prefixSpan.size < moduleParts.prefixLen)
                {
                    // record the offset of the first word of this span
                    if (moduleSpans.prefixSpan.size == 0)
                        moduleSpans.prefixSpan.offset = state.workBufferOffset;

                    u16 wordsLeftInSpan = moduleParts.prefixLen - moduleSpans.prefixSpan.size;
                    u16 wordsToCopy = std::min({
                        wordsLeftInSpan, state.curStackFrame.wordsLeft,
                        static_cast<u16>(iter.longwordsLeft())});

                    copy_to_workbuffer(state, iter, wordsToCopy);
                    moduleSpans.prefixSpan.size += wordsToCopy;
                }

                assert(moduleSpans.prefixSpan.size <= moduleParts.prefixLen);

                if (moduleSpans.prefixSpan.size == moduleParts.prefixLen)
                {
                    if (moduleParts.hasDynamic)
                        state.moduleParseState = ReadoutParserState::Dynamic;
                    else if (moduleParts.suffixLen != 0)
                        state.moduleParseState = ReadoutParserState::Suffix;
                    else
                    {
                        // We're done with this module as it does have neither
                        // dynamic nor suffix parts.
                        state.moduleIndex++;
                    }
                }

                break;

            case ReadoutParserState::Dynamic:
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
                        {
                            state.moduleParseState = ReadoutParserState::Suffix;
                        }
                    }
                }
                break;

            case ReadoutParserState::Suffix:
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
            assert(!state.curBlockFrame);
            // All modules have been processed and the event can be flushed.

            callbacks.beginEvent(state.eventIndex);

            for (int mi = 0; mi < static_cast<int>(moduleReadoutInfos.size()); mi++)
            {
                const auto &moduleSpans = state.readoutDataSpans[mi];
                bool didInvoke = false;

                if (moduleSpans.prefixSpan.size)
                {
                    callbacks.modulePrefix(
                        state.eventIndex, mi,
                        state.workBuffer.indexU32(moduleSpans.prefixSpan.offset),
                        moduleSpans.prefixSpan.size);
                    didInvoke = true;
                }

                if (moduleSpans.dynamicSpan.size)
                {
                    callbacks.moduleDynamic(
                        state.eventIndex, mi,
                        state.workBuffer.indexU32(moduleSpans.dynamicSpan.offset),
                        moduleSpans.dynamicSpan.size);
                    didInvoke = true;
                }

                if (moduleSpans.suffixSpan.size)
                {
                    callbacks.moduleSuffix(
                        state.eventIndex, mi,
                        state.workBuffer.indexU32(moduleSpans.suffixSpan.offset),
                        moduleSpans.suffixSpan.size);
                    didInvoke = true;
                }

                if (didInvoke)
                {
                    //++state.counters.moduleCounters[state.eventIndex][mi];
                }
            }

            callbacks.endEvent(state.eventIndex);
            //++state.counters.eventCounters[state.eventIndex];
            parser_clear_event_state(state);
        }
    }

    return ParseResult::Ok;
}

inline u32 *find_frame_header(u32 *firstFrameHeader, const u32 *endOfData, u8 wantedFrameType)
{
    auto is_accepted_frame_type = [] (u8 frameType) -> bool
    {
        return (frameType == frame_headers::StackFrame
                || frameType == frame_headers::StackContinuation);
    };

    BufferIterator iter(firstFrameHeader, endOfData - firstFrameHeader);

    try
    {
        while (!iter.atEnd())
        {
            const u8 frameType = get_frame_type(iter.peekU32());

            if (frameType == wantedFrameType)
                return iter.indexU32(0);

            if (!is_accepted_frame_type(frameType))
                return nullptr;

            iter.skipExact(extract_frame_info(iter.peekU32()).len + 1, sizeof(u32));
        }
        return nullptr;
    } catch (const end_of_buffer &)
    {
        return nullptr;
    }
};

inline void count_parse_result(ReadoutParserCounters &counters, const ParseResult &pr)
{
    ++counters.parseResults[static_cast<size_t>(pr)];
}

// IMPORTANT: This function assumes that packet loss is handled on the outside!
// The iterator must be bounded by the packets data.
ParseResult parse_eth_packet(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    BufferIterator packetIter)
{
    eth::PayloadHeaderInfo ethHdrs{ packetIter.peekU32(0), packetIter.peekU32(1) };
    const u32 *packetEndPtr = packetIter.indexU32(eth::HeaderWords + ethHdrs.dataWordCount());
    assert(packetIter.endp == reinterpret_cast<const u8 *>(packetEndPtr));

    LOG_TRACE("begin parsing packet %u, dataWords=%u",
              ethHdrs.packetNumber(), ethHdrs.dataWordCount());

    if (!is_event_in_progress(state))
    {
        // Special case for the ETH readout: find the start of a new event by
        // interpreting the packets nextHeaderPointer value and searching from
        // there.

        if (!ethHdrs.isNextHeaderPointerPresent())
        {
            // Not currently parsing an event and no frame header present
            // inside the packet data which means we cannot start a new event
            // using this packets data.
            return ParseResult::NoHeaderPresent;
        }

        // Find the next StackFrame header starting the search from the header
        // word pointed to by the ETH packet header. This StackFrame header
        // will mark the beginning of a new event.
        u32 *firstFramePtr = packetIter.indexU32(eth::HeaderWords + ethHdrs.nextHeaderPointer());

        u32 *stackFrame = find_frame_header(
            firstFramePtr, packetEndPtr, frame_headers::StackFrame);

        if (!stackFrame)
            return ParseResult::NoStackFrameFound;

        // Check postconditions after find_frame_header()
        assert(packetIter.data <= reinterpret_cast<u8 *>(stackFrame));
        assert(reinterpret_cast<u8 *>(stackFrame) <= packetIter.endp);

        // Place the iterator right on the stackframe header.
        // parse_readout_contents() will pick this up and use it for
        // parser_begin_event().
        packetIter.buffp = reinterpret_cast<u8 *>(stackFrame);
    }
    else
    {
        // Skip to the first payload contents word, right after the two ETH
        // headers. This can be trailing data words from an already open stack
        // frame or it can be the next frame continuation header.
        packetIter.skipExact(eth::HeaderWords, sizeof(u32));
    }

    try
    {
        ParseResult retval = {};

        while (!packetIter.atEnd())
        {
            auto pr = parse_readout_contents(
                state, callbacks, packetIter,
                true);
            count_parse_result(state.counters, pr);

            // Keep the last error code in retval to return at the end.
            if (pr != ParseResult::Ok)
                retval = pr;

            LOG_TRACE("end parsing packet %u, dataWords=%u",
                      ethHdrs.packetNumber(), ethHdrs.dataWordCount());
        }

        return retval;
    }
    catch (const std::exception &e)
    {
        LOG_WARN("end parsing packet %u, dataWords=%u, exception=%s",
                  ethHdrs.packetNumber(), ethHdrs.dataWordCount(),
                  e.what());

        //::logBuffer(BufferIterator(iter.data, iter.size),
        //            [] (const QString &str) { qDebug().noquote() << str; });

        throw;
    }

    return ParseResult::Ok;
}

ParseResult parse_readout_buffer_eth(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, u8 *buffer, size_t bufferSize)
{
    LOG_TRACE("begin parsing ETH buffer %u, size=%lu bytes", bufferNumber, bufferSize);

    ParseResult retval = {};
    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any output data prepared so far is discarded.
        parser_clear_event_state(state);
        state.counters.internalBufferLoss += bufferLoss;
    }

    BufferIterator iter(buffer, bufferSize);

    try
    {
        while (!iter.atEnd())
        {
            // ETH readout data consists of a mix of SystemEvent frames and raw
            // packet data starting with ETH header0.

            if (try_handle_system_event(state, callbacks, iter))
                continue;

            // At this point the buffer iterator is positioned on the first of the
            // two ETH payload header words.
            eth::PayloadHeaderInfo ethHdrs{ iter.peekU32(0), iter.peekU32(1) };

            // Ensure that the packet is fully contained in the input buffer.
            // This is a requirement for the buffer producer.
            if (iter.longwordsLeft() < eth::HeaderWords + ethHdrs.dataWordCount())
                throw end_of_buffer("ETH packet data exceeds input buffer size");

            if (state.lastPacketNumber >= 0)
            {
                // Check for packet loss. If there is loss clear the parsing state
                // and restart parsing at the same iterator position.
                if (auto loss = eth::calc_packet_loss(state.lastPacketNumber,
                                                      ethHdrs.packetNumber()))
                {
                    parser_clear_event_state(state);
                    state.counters.ethPacketLoss += loss;
                }
            }

            state.lastPacketNumber = ethHdrs.packetNumber();

            BufferIterator packetIter(
                iter.indexU32(0),
                eth::HeaderWords + ethHdrs.dataWordCount());

            ParseResult pr = parse_eth_packet(state, callbacks, packetIter);

            // Keep the last error code in retval to return at the end.
            if (pr != ParseResult::Ok)
                retval = pr;

            // XXX: reparsing of packets after state reset
#if 0
            if (pr != ParseResult::Ok && pr != ParseResult::NoHeaderPresent)
            {
                // Parsing did not succeed. Throw away the current state and
                // try again. If that still fails the packet will be skipped.
                packetIter.rewind();
                parser_clear_event_state(state);
                pr = parse_eth_packet(state, callbacks, packetIter);
                ++state.counters.ethPacketsReparsed;

                if (pr != ParseResult::Ok)
                {
                    // Again a parse error. The packet will be skipped completely.
                    ++state.counters.ethPacketsSkipped;
                }

                // FIXME: Handle the pr values. Counters, etc?
            }
#elif 0
            if (pr != ParseResult::Ok)
            {
                ++state.counters.ethPacketsSkipped;
            }
#endif
            ++state.counters.ethPacketsProcessed;

            LOG_TRACE("parse_packet result: %d\n", (int)pr);

            // Skip over the packet ending up either on the next SystemEvent
            // frame or on the next packets header0.
            iter.skipExact(eth::HeaderWords + ethHdrs.dataWordCount(), sizeof(u32));
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN("end parsing ETH buffer %u, size=%lu bytes, exception=%s",
                 bufferNumber, bufferSize, e.what());

        parser_clear_event_state(state);
        throw;
    }
    catch (...)
    {
        LOG_WARN("end parsing ETH buffer %u, size=%lu bytes, unknown exception",
                 bufferNumber, bufferSize);

        parser_clear_event_state(state);
        throw;
    }

    ++state.counters.buffersProcessed;
    LOG_TRACE("end parsing ETH buffer %u, size=%lu bytes", bufferNumber, bufferSize);

#if 0
    catch (const end_of_buffer &e)
    {
        // TODO: count this
        LOG_WARN("parsing buffer %u raised end_of_buffer: %s", bufferNumber, e.what());
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
        LOG_WARN("buffer %u: %u words left in buffer after parsing!",
                 bufferNumber, iter.longwordsLeft());
    }
#endif
    return retval;
}

ParseResult parse_readout_buffer_usb(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, u8 *buffer, size_t bufferSize)
{
    LOG_TRACE("begin parsing USB buffer %u, size=%lu bytes", bufferNumber, bufferSize);

    ParseResult retval = {};
    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any output data prepared so far will be discarded.
        parser_clear_event_state(state);
        state.counters.internalBufferLoss += bufferLoss;
    }

    BufferIterator iter(buffer, bufferSize);

    try
    {
        while (!iter.atEnd())
        {
            auto pr = parse_readout_contents(state, callbacks, iter, false);

            count_parse_result(state.counters, pr);

            if (pr != ParseResult::Ok)
            {
                parser_clear_event_state(state);
                retval = pr;
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN("end parsing USB buffer %u, size=%lu bytes, exception=%s",
                 bufferNumber, bufferSize, e.what());

        parser_clear_event_state(state);
        throw;
    }
    catch (...)
    {
        LOG_WARN("end parsing USB buffer %u, size=%lu bytes, unknown exception",
                 bufferNumber, bufferSize);

        parser_clear_event_state(state);
        throw;
    }

    ++state.counters.buffersProcessed;
    LOG_TRACE("end parsing USB buffer %u, size=%lu bytes", bufferNumber, bufferSize);

    return retval;
}

} // end namespace mesytec
} // end namespace mvlc
