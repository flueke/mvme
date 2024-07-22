#include "multi_crate_nng.h"

#include <mesytec-mvlc/mesytec-mvlc.h>

#include "mvlc_daq.h"

using namespace mesytec::mvlc;
using namespace mesytec::nng;
using namespace mesytec::util;

namespace mesytec::mvme::multi_crate
{

static constexpr std::chrono::milliseconds FlushBufferTimeout(500);
static const size_t DefaultOutputMessageReserve = mvlc::util::Megabytes(1) + sizeof(multi_crate::ReadoutDataMessageHeader);

std::string LoopResult::toString() const
{
    if (ec)
        return fmt::format("ec={} ({})", ec.message(), ec.category().name());

    if (nngError)
        return fmt::format("nngError={}", nng_strerror(nngError));

    if (exception)
    {
        try
        {
            std::rethrow_exception(exception);
        }
        catch(const std::exception& e)
        {
            return fmt::format("exception={}", e.what());
        }
        catch (...)
        {
            return fmt::format("unknown exception");
        }
    }

    return "Ok";
}

std::unique_ptr<ReadoutParserContext> make_readout_parser_context(const mvlc::CrateConfig &crateConfig)
{
    auto res = std::make_unique<ReadoutParserContext>();

    res->crateId = crateConfig.crateId;
    res->inputFormat = crateConfig.connectionType;
    auto stacks = mvme_mvlc::sanitize_readout_stacks(crateConfig.stacks);
    res->parserState = mvlc::readout_parser::make_readout_parser(stacks, res.get());

    return res;
}

// Allocates and prepares a new ParsedEventsMessageHeader message if there isn't
// one in the context object.
template<typename T>
inline bool parser_maybe_alloc_output(T &ctx)
{
    auto &msg = ctx.outputMessage;

    if (msg)
        return false;

    msg = nng::allocate_reserve_message(DefaultOutputMessageReserve);

    if (!msg)
        return false;

    multi_crate::ParsedEventsMessageHeader header{};
    header.messageType = multi_crate::MessageType::ParsedEvents;
    header.messageNumber = ++ctx.outputMessageNumber;
    header.crateId = ctx.crateId;

    nng_msg_append(msg.get(), &header, sizeof(header));
    assert(nng_msg_len(msg.get()) == sizeof(header));

    return true;
}

inline bool flush_output_message(ReadoutParserContext &ctx)
{
    assert(ctx.outputMessage);

    if (!ctx.outputMessage)
        return false;

    const auto msgSize = nng_msg_len(ctx.outputMessage.get());

    Stopwatch stopWatch;

    // Take ownership of the current output message => ctx.outputMessage becomes null.
    auto msg = std::move(ctx.outputMessage);
    assert(!ctx.outputMessage);

    if (int res = ctx.outputWriter()->writeMessage(std::move(msg)))
    {
        // Note: if msg was not released in writeMessage() it is still alive
        // here. It will be destroyed when leaving this scope.
        assert(!ctx.outputMessage);
        spdlog::warn("readout_parser (crate{}): error writing output message: {}", ctx.crateId, nng_strerror(res));
        return false;
    }

    assert(!msg);
    assert(!ctx.outputMessage);

    {
        auto a = ctx.writerCounters().access();
        auto &c = a.ref();
        c.tSend += stopWatch.interval();
        c.tTotal += stopWatch.end();
        c.messagesSent++;
        c.bytesSent += msgSize;
        ctx.flushTimer.interval();
    }

    spdlog::debug("readout_parser (crate{}): sent message {} of size {}",
        ctx.crateId, ctx.outputMessageNumber, msgSize);

    return true;
}

struct ReadoutParserNngMessageWriter: public ParsedEventsMessageWriter
{
    ReadoutParserNngMessageWriter(ReadoutParserContext &ctx_)
        : ctx(ctx_)
    {
    }

    nng_msg *getOutputMessage() override
    {
        parser_maybe_alloc_output(ctx);
        return ctx.outputMessage.get();
    }

    bool flushOutputMessage() override
    {
        spdlog::debug("readout_parser_loop (crateId={}): flushing full output message #{}",
            ctx.crateId, ctx.outputMessageNumber-1);
        return flush_output_message(ctx);
    }

    bool hasDynamic(int crateIndex, int eventIndex, int moduleIndex) override
    {
        (void) crateIndex;
        if (0 <= eventIndex && static_cast<size_t>(eventIndex) < ctx.parserState.readoutStructure.size())
        {
            if (0 <= moduleIndex && static_cast<size_t>(moduleIndex) < ctx.parserState.readoutStructure[eventIndex].size())
                return ctx.parserState.readoutStructure[eventIndex][moduleIndex].hasDynamic;
        }

        return false;
    }

    ReadoutParserContext &ctx;
};

// The event data callback for the readout parser. Serializes the parsed module
// data list into the current output message.
inline void readout_parser_eventdata_callback(void *ctx_, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());
    assert(eventIndex >= 0 && eventIndex <= std::numeric_limits<u8>::max());
    assert(moduleCount < std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<ReadoutParserContext *>(ctx_);
    ++ctx.totalReadoutEvents;

    ReadoutParserNngMessageWriter writer(ctx);
    writer.consumeReadoutEventData(crateIndex, eventIndex, moduleDataList, moduleCount);
}

inline void readout_parser_systemevent_callback(void *ctx_, int crateIndex, const u32 *header, u32 size)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<ReadoutParserContext *>(ctx_);
    ++ctx.totalSystemEvents;

    ReadoutParserNngMessageWriter writer(ctx);
    writer.consumeSystemEventData(crateIndex, header, size);
}

LoopResult readout_parser_loop(ReadoutParserContext &context)
{
    LoopResult result;
    const auto crateId = context.crateId;

    set_thread_name(fmt::format("readout_parser_loop{}", crateId).c_str());

    spdlog::info("entering readout_parser_loop, crateId={}", crateId);

    context.outputMessageNumber = 0;
    u32 lastInputMessageNumber = 0u;
    size_t inputBuffersLost = 0;

    // Local, non-protected readout parser counters. The protected version in
    // the context struct will be updated after parsing a full input buffer.
    mvlc::readout_parser::ReadoutParserCounters parserCounters = {};

    mvlc::readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        readout_parser_eventdata_callback,
        readout_parser_systemevent_callback,
    };

    context.flushTimer.start();
    SocketWorkPerformanceCounters counters;
    counters.start();

    while (!context.shouldQuit())
    {
        Stopwatch sw;

        auto [inputMsg, res] = context.inputReader()->readMessage();

        if (res && res != NNG_ETIMEDOUT)
        {
            spdlog::error("readout_parser_loop (crateId={}) - receive_message: {}", crateId, nng_strerror(res));
            result.nngError = res;
            break;
        }
        else if (res)
        {
            spdlog::trace("readout_parser_loop (crateId={}) - receive_message: timeout", crateId);
            continue;
        }

        assert(inputMsg);

        const auto msgLen = nng_msg_len(inputMsg.get());

        if (is_shutdown_message(inputMsg.get()))
        {
            spdlog::info("readout_parser_loop (crateId={}): Received shutdown message, leaving loop", crateId);
            break;
        }

        if (msgLen < sizeof(multi_crate::ReadoutDataMessageHeader))
        {
            spdlog::warn("reaodut_parser_loop (crateId={}): Incoming message is too short (len={})!", crateId, msgLen);
            continue;
        }

        auto inputHeader = nng::msg_trim_read<multi_crate::ReadoutDataMessageHeader>(inputMsg.get()).value();

        if (inputHeader.messageType != multi_crate::MessageType::ReadoutData)
        {
            spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ReadoutData));
            continue;
        }

        auto tReceive = sw.interval();

        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastInputMessageNumber);
        inputBuffersLost += bufferLoss;
        lastInputMessageNumber = inputHeader.messageNumber;
        spdlog::debug("readout_parser_loop (crateId={}): received message {} of size {}",
            crateId, lastInputMessageNumber, msgLen);

        auto inputData = reinterpret_cast<const u32 *>(nng_msg_body(inputMsg.get()));
        size_t inputLen = nng_msg_len(inputMsg.get()) / sizeof(u32);

        // Invoke the parser. The output message is flushed from within the callbacks when needed.
        readout_parser::parse_readout_buffer(
            context.inputFormat,
            context.parserState,
            parserCallbacks,
            parserCounters,
            inputHeader.messageNumber,
            inputData,
            inputLen);

        context.parserCounters.access().ref() = parserCounters;

        auto tProcess = sw.interval();

        {
            counters.bytesReceived += msgLen;
            counters.messagesReceived++;
            counters.tReceive += tReceive;
            counters.tProcess += tProcess;
            counters.tTotal += sw.end();
            counters.messagesLost = inputBuffersLost;
            context.readerCounters().access().ref() = counters;
        }

        if (context.outputMessage && context.flushTimer.get_interval() >= FlushBufferTimeout)
        {
            spdlog::debug("readout_parser_loop (crateId={}): flushing output message #{} due to timeout", context.crateId, context.outputMessageNumber-1);
            flush_output_message(context);
        }
    }

    if (context.outputMessage)
        flush_output_message(context);

    assert(!context.outputMessage);

    //log_socket_work_counters(context.counters.access().ref(),
    //    fmt::format("readout_parser_loop (crateId={})", context.crateId));

    {
        std::ostringstream ss;
        readout_parser::print_counters(ss, parserCounters);
        spdlog::info("readout_parser_loop (crateId={}): parser counters:\n{}", crateId, ss.str());
    }

    spdlog::info("leaving readout_parser_loop, crateId={} (shouldQuit={})", crateId, context.shouldQuit());

    return result;
}

inline bool flush_output_message(MultiEventSplitterContext &ctx)
{
    assert(ctx.outputMessage);

    if (!ctx.outputMessage)
        return false;

    const auto msgSize = nng_msg_len(ctx.outputMessage.get());

    Stopwatch stopWatch;

    // Take ownership of the current output message => ctx.outputMessage becomes null.
    auto msg = std::move(ctx.outputMessage);
    assert(!ctx.outputMessage);

    if (int res = ctx.outputWriter()->writeMessage(std::move(msg)))
    {
        // Note: if msg was not released in writeMessage() it is still alive
        // here. It will be destroyed when leaving this scope.
        assert(!ctx.outputMessage);
        spdlog::warn("multievent_splitter_loop (crate{}): error writing output message: {}", ctx.crateId, nng_strerror(res));
        return false;
    }

    assert(!msg);
    assert(!ctx.outputMessage);

    {
        auto a = ctx.writerCounters().access();
        auto &c = a.ref();
        c.tSend += stopWatch.interval();
        c.tTotal += stopWatch.end();
        c.messagesSent++;
        c.bytesSent += msgSize;
        ctx.flushTimer.interval();
    }

    spdlog::debug("multievent_splitter_loop (crate{}): sent message {} of size {}",
        ctx.crateId, ctx.outputMessageNumber, msgSize);

    return true;
}

struct MultieventSplitterNngMessageWriter: public ParsedEventsMessageWriter
{
    MultieventSplitterNngMessageWriter(MultiEventSplitterContext &ctx_)
        : ctx(ctx_)
    {
    }

    nng_msg *getOutputMessage() override
    {
        parser_maybe_alloc_output(ctx);
        return ctx.outputMessage.get();
    }

    bool flushOutputMessage() override
    {
        spdlog::debug("multievent_splitter_loop (crateId={}): flushing full output message #{}",
            ctx.crateId, ctx.outputMessageNumber-1);
        return flush_output_message(ctx);
    }

    bool hasDynamic(int crateIndex, int eventIndex, int moduleIndex) override
    {
        return true;
    }

    MultiEventSplitterContext &ctx;
};

// The event data callback for the multievent_splitter. Serializes the parsed module
// data list into the current output message.
inline void multievent_splitter_eventdata_callback(void *ctx_, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());
    assert(eventIndex >= 0 && eventIndex <= std::numeric_limits<u8>::max());
    assert(moduleCount < std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<MultiEventSplitterContext *>(ctx_);

    MultieventSplitterNngMessageWriter writer(ctx);
    writer.consumeReadoutEventData(crateIndex, eventIndex, moduleDataList, moduleCount);
}

inline void multievent_splitter_systemevent_callback(void *ctx_, int crateIndex, const u32 *header, u32 size)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<MultiEventSplitterContext *>(ctx_);

    MultieventSplitterNngMessageWriter writer(ctx);
    writer.consumeSystemEventData(crateIndex, header, size);
}

LoopResult multievent_splitter_loop(MultiEventSplitterContext &context)
{
    LoopResult result;
    const auto crateId = context.crateId;

    set_thread_name(fmt::format("multievent_splitter_loop{}", crateId).c_str());

    spdlog::info("entering multievent_splitter_loop, crateId={}", crateId);

    context.outputMessageNumber = 0;
    u32 lastInputMessageNumber = 0u;
    size_t inputBuffersLost = 0;

    multi_event_splitter::Callbacks splitterCallbacks =
    {
        multievent_splitter_eventdata_callback,
    };

    context.flushTimer.start();
    SocketWorkPerformanceCounters counters;
    counters.start();

    while (!context.shouldQuit())
    {
        Stopwatch sw;

        auto [inputMsg, res] = context.inputReader()->readMessage();

        if (res && res != NNG_ETIMEDOUT)
        {
            spdlog::error("multievent_splitter_loop (crateId={}) - receive_message: {}", crateId, nng_strerror(res));
            result.nngError = res;
            break;
        }
        else if (res)
        {
            spdlog::trace("multievent_splitter_loop (crateId={}) - receive_message: timeout", crateId);
            continue;
        }

        assert(inputMsg);

        const auto msgLen = nng_msg_len(inputMsg.get());

        if (is_shutdown_message(inputMsg.get()))
        {
            spdlog::info("multievent_splitter_loop (crateId={}): Received shutdown message, leaving loop", crateId);
            break;
        }

        if (msgLen < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            spdlog::warn("multievent_splitter_loop (crateId={}): incoming message too short (len={})", crateId, msgLen);
            continue;
        }

        auto inputHeader = nng::msg_trim_read<multi_crate::ParsedEventsMessageHeader>(inputMsg.get()).value();

        if (inputHeader.messageType != multi_crate::MessageType::ParsedEvents)
        {
            spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ParsedEvents));
            continue;
        }

        auto tReceive = sw.interval();

        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastInputMessageNumber);
        inputBuffersLost += bufferLoss;
        lastInputMessageNumber = inputHeader.messageNumber;
        spdlog::debug("multievent_splitter_loop (crateId={}): received message {} of size {}",
            crateId, lastInputMessageNumber, msgLen);

        ParsedEventMessageIterator messageIter(inputMsg.get());

        for (auto eventData = next_event(messageIter);
                eventData.type != EventContainer::Type::None;
                eventData = next_event(messageIter))
        {
            if (eventData.type == EventContainer::Type::Readout)
            {
                multi_event_splitter::event_data(
                    context.state,
                    splitterCallbacks,
                    nullptr,
                    eventData.readout.eventIndex,
                    eventData.readout.moduleDataList,
                    eventData.readout.moduleCount);
            }
            else if (eventData.type == EventContainer::Type::System && eventData.system.size)
            {
                auto frameInfo = mvlc::extract_frame_info(eventData.system.header[0]);
                assert(frameInfo.type == mvlc::frame_headers::SystemEvent);
                // Directly copy the system event to the output message. No processing needed.
                MultieventSplitterNngMessageWriter writer(context);
                writer.consumeSystemEventData(eventData.crateId, eventData.system.header, eventData.system.size);
            }
            else if (nng_msg_len(inputMsg.get()))
            {
                spdlog::warn("multievent_splitter_loop (crateId={}): incoming message contains unknown subsection '{}'",
                    crateId, *reinterpret_cast<const u8 *>(nng_msg_body(inputMsg.get())));
                break;
            }
        }

        auto tProcess = sw.interval();

        {
            counters.bytesReceived += msgLen;
            counters.messagesReceived++;
            counters.tReceive += tReceive;
            counters.tProcess += tProcess;
            counters.tTotal += sw.end();
            counters.messagesLost = inputBuffersLost;
            context.readerCounters().access().ref() = counters;
        }

        if (context.outputMessage && context.flushTimer.get_interval() >= FlushBufferTimeout)
        {
            spdlog::debug("multievent_splitter_loop (crateId={}): flushing output message #{} due to timeout", context.crateId, context.outputMessageNumber-1);
            flush_output_message(context);
        }
    }

    if (context.outputMessage)
        flush_output_message(context);

    assert(!context.outputMessage);
    spdlog::info("leaving multievent_splitter_loop, crateId={}", crateId);
    return result;
}

inline bool flush_output_message(EventBuilderContext &ctx)
{
    assert(ctx.outputMessage);

    if (!ctx.outputMessage)
        return false;

    const auto msgSize = nng_msg_len(ctx.outputMessage.get());

    Stopwatch stopWatch;

    // Take ownership of the current output message => ctx.outputMessage becomes null.
    auto msg = std::move(ctx.outputMessage);
    assert(!ctx.outputMessage);

    if (int res = ctx.outputWriter()->writeMessage(std::move(msg)))
    {
        // Note: if msg was not released in writeMessage() it is still alive
        // here. It will be destroyed when leaving this scope.
        assert(!ctx.outputMessage);
        spdlog::warn("event_builder_loop (crate{}): error writing output message: {}", ctx.crateId, nng_strerror(res));
        return false;
    }

    assert(!msg);
    assert(!ctx.outputMessage);

    {
        auto a = ctx.writerCounters().access();
        auto &c = a.ref();
        c.tSend += stopWatch.interval();
        c.tTotal += stopWatch.end();
        c.messagesSent++;
        c.bytesSent += msgSize;
        ctx.flushTimer.interval();
    }

    spdlog::debug("event_builder_loop (crate{}): sent message {} of size {}",
        ctx.crateId, ctx.outputMessageNumber, msgSize);

    return true;
}

struct EventBuilderMessageWriter: public ParsedEventsMessageWriter
{
    EventBuilderMessageWriter(EventBuilderContext &ctx_)
        : ctx(ctx_)
    {
    }

    nng_msg *getOutputMessage() override
    {
        parser_maybe_alloc_output(ctx);
        return ctx.outputMessage.get();
    }

    bool flushOutputMessage() override
    {
        spdlog::debug("event_builder_loop (crateId={}): flushing full output message #{}",
            ctx.crateId, ctx.outputMessageNumber-1);
        return flush_output_message(ctx);
    }

    bool hasDynamic(int crateIndex, int eventIndex, int moduleIndex) override
    {
        (void) crateIndex; (void) eventIndex; (void) moduleIndex;
        return true;
    }

    EventBuilderContext &ctx;
};

inline void event_builder_eventdata_callback(void *ctx_, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());
    assert(eventIndex >= 0 && eventIndex <= std::numeric_limits<u8>::max());
    assert(moduleCount < std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<EventBuilderContext *>(ctx_);

    auto outputCrateId = ctx.outputCrateMappings[crateIndex];

    EventBuilderMessageWriter writer(ctx);
    writer.consumeReadoutEventData(outputCrateId, eventIndex, moduleDataList, moduleCount);
}

inline void event_builder_systemevent_callback(void *ctx_, int crateIndex, const u32 *header, u32 size)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<EventBuilderContext *>(ctx_);

    auto outputCrateId = ctx.outputCrateMappings[crateIndex];

    EventBuilderMessageWriter writer(ctx);
    writer.consumeSystemEventData(outputCrateId, header, size);
}

LoopResult event_builder_loop(EventBuilderContext &context)
{
    LoopResult result;
    const auto crateId = context.crateId;
    // Last received message number per crate.
    std::unordered_map<u8, u32> lastMessageNumbers;

    set_thread_name(fmt::format("event_builder_loop{}", crateId).c_str());

    spdlog::info("entering event_builder_loop, crateId={}", crateId);

    readout_parser::ReadoutParserCallbacks callbacks;
    callbacks.eventData = event_builder_eventdata_callback;
    callbacks.systemEvent = event_builder_systemevent_callback;

    context.flushTimer.start();
    SocketWorkPerformanceCounters counters;
    counters.start();

    while (!context.shouldQuit())
    {
        Stopwatch sw;

        auto [inputMsg, res] = context.inputReader()->readMessage();

        if (res && res != NNG_ETIMEDOUT)
        {
            spdlog::error("event_builder_loop (crateId={}) - receive_message: {}", crateId, nng_strerror(res));
            result.nngError = res;
            break;
        }
        else if (res)
        {
            spdlog::trace("event_builder_loop (crateId={}) - receive_message: timeout", crateId);
            continue;
        }

        assert(inputMsg);

        const auto msgLen = nng_msg_len(inputMsg.get());

        if (is_shutdown_message(inputMsg.get()))
        {
            spdlog::info("event_builder_loop (crateId={}): Received shutdown message, leaving loop", crateId);
            break;
        }

        if (msgLen < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            spdlog::warn("event_builder_loop (crateId={}): incoming message too short (len={})", crateId, msgLen);
            continue;
        }

        auto inputHeader = nng::msg_trim_read<multi_crate::ParsedEventsMessageHeader>(inputMsg.get()).value();

        if (inputHeader.messageType != multi_crate::MessageType::ParsedEvents)
        {
            spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ParsedEvents));
            continue;
        }

        auto tReceive = sw.interval();

        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastMessageNumbers[inputHeader.crateId]);
        lastMessageNumbers[inputHeader.crateId] = inputHeader.messageNumber;

        ParsedEventMessageIterator messageIter(inputMsg.get());

        for (auto eventData = next_event(messageIter);
                eventData.type != EventContainer::Type::None;
                eventData = next_event(messageIter))
        {
            if (eventData.type == EventContainer::Type::Readout)
            {
                auto mappedCrateId = context.inputCrateMappings[eventData.crateId];
                spdlog::trace("event_builder_loop (crateId={}) - readout event: input crateId={} mapped to crateId={}",
                    context.crateId, eventData.crateId, mappedCrateId);
                context.eventBuilder->recordEventData(mappedCrateId, eventData.readout.eventIndex,
                    eventData.readout.moduleDataList, eventData.readout.moduleCount);
            }
            else if (eventData.type == EventContainer::Type::System && eventData.system.size)
            {
                auto mappedCrateId = context.inputCrateMappings[eventData.crateId];
                spdlog::trace("event_builder_loop (crateId={}) - system event: input crateId={} mapped to crateId={}",
                    context.crateId, eventData.crateId, mappedCrateId);
                context.eventBuilder->recordSystemEvent(mappedCrateId, eventData.system.header, eventData.system.size);
            }
            else if (nng_msg_len(inputMsg.get()))
            {
                spdlog::warn("event_builder_loop: incoming message contains unknown subsection '{}'",
                    *reinterpret_cast<const u8 *>(nng_msg_body(inputMsg.get())));
                break;
            }
        }

        auto nEvents = context.eventBuilder->buildEvents(callbacks);
        if (nEvents)
            spdlog::debug("event_builder_loop{}: built {} events", context.crateId, nEvents);

        auto tProcess = sw.interval();

        {
            counters.bytesReceived += msgLen;
            counters.messagesReceived++;
            counters.tReceive += tReceive;
            counters.tProcess += tProcess;
            counters.tTotal += sw.end();
            // Note: this accumulates losses from different crate streams.
            // Per-crate loss details are not available here.
            counters.messagesLost += bufferLoss;
            context.readerCounters().access().ref() = counters;
        }

        if (context.outputMessage && context.flushTimer.get_interval() >= FlushBufferTimeout)
        {
            spdlog::debug("event_builder_loop (crateId={}): flushing output message #{} due to timeout", context.crateId, context.outputMessageNumber-1);
            flush_output_message(context);
        }
    }

    if (context.outputMessage)
        flush_output_message(context);

    assert(!context.outputMessage);
    spdlog::info("leaving event_builder_loop, crateId={}", crateId);
    return result;
}

std::unique_ptr<AnalysisProcessingContext> make_analysis_context(const std::shared_ptr<analysis::Analysis> &analysis, VMEConfig *vmeConfig)
{
    auto res = std::make_unique<AnalysisProcessingContext>();
    auto asp = std::make_unique<multi_crate::MinimalAnalysisServiceProvider>();
    auto widgetRegistry = std::make_shared<WidgetRegistry>();

    res->analysis = analysis;
    asp->vmeConfig_ = vmeConfig;
    asp->analysis_ = analysis;
    asp->widgetRegistry_ = widgetRegistry;
    res->asp = std::move(asp);

    return res;
}

std::unique_ptr<AnalysisProcessingContext> make_analysis_context(const std::string &filename, VMEConfig *vmeConfig)
{
    std::shared_ptr<analysis::Analysis> analysis;

    if (!filename.empty())
    {
        auto [ana, errorString] = analysis::read_analysis_config_from_file(filename.c_str());

        if (!ana)
        {
            std::cerr << fmt::format("Error reading mvme analysis config from '{}': {}\n",
                filename, errorString.toStdString());
            return {};
        }

        //analysis::generate_new_object_ids(ana.get()); // FIXME: figure out object id handling
    //vme_analysis_common::auto_assign_vme_modules(mergedVmeConfig.get(), analysis.get());
        analysis = ana;
    }

    return make_analysis_context(analysis, vmeConfig);
}

LoopResult analysis_loop(AnalysisProcessingContext &context)
{
    set_thread_name("analysis_loop");

    LoopResult result;

    // Last received message number per crate.
    std::unordered_map<u8, u32> lastMessageNumbers;
    size_t inputBuffersLost = 0;

    bool error = false;
    vme_analysis_common::TimetickGenerator timetickGen;
    const auto crateId = context.crateId;

    spdlog::info("entering analysis_loop (crateId={})", crateId);

    while (!error && !context.shouldQuit())
    {
        if (!context.runInfo.isReplay)
        {
            int elapsedSeconds = timetickGen.generateElapsedSeconds();

            while (elapsedSeconds >= 1)
            {
                context.analysis->processTimetick();
                elapsedSeconds--;
            }
        }

        Stopwatch sw;

        auto [inputMsg, res] = context.inputReader()->readMessage();

        if (res && res != NNG_ETIMEDOUT)
        {
            spdlog::error("analysis_loop (crateId={}) - receive_message: {}",
                crateId, nng_strerror(res));
            result.nngError = res;
            break;
        }
        else if (res)
        {
            spdlog::trace("analysis_loop (crateId={}) - receive_message: timeout", crateId);
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg.get());

        if (is_shutdown_message(inputMsg.get()))
        {
            spdlog::info("analysis_loop (crateId={}): Received shutdown message, leaving loop", crateId);
            break;
        }

        if (msgLen < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            spdlog::warn("analysis_loop (crateId={}): incoming message too short (len={})", crateId, msgLen);
            continue;
        }

        auto inputHeader = nng::msg_trim_read<multi_crate::ParsedEventsMessageHeader>(inputMsg.get()).value();

        if (inputHeader.messageType != multi_crate::MessageType::ParsedEvents)
        {
            spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ParsedEvents));
            continue;
        }

        auto tReceive = sw.interval();

        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastMessageNumbers[inputHeader.crateId]);
        inputBuffersLost += bufferLoss;
        lastMessageNumbers[inputHeader.crateId] = inputHeader.messageNumber;

        spdlog::debug("analysis_loop (crateId={}): received message #{} of size {} with header crateId={}",
            crateId, lastMessageNumbers[inputHeader.crateId], msgLen, inputHeader.crateId);

        if (context.asp)
        {
            auto a = context.asp->daqStats_.access();
            a->droppedBuffers = inputBuffersLost;
            a->totalBuffersRead += 1;
        }

        ParsedEventMessageIterator messageIter(inputMsg.get());

        for (auto eventData = next_event(messageIter);
                eventData.type != EventContainer::Type::None;
                eventData = next_event(messageIter))
        {
            if (eventData.type == EventContainer::Type::Readout)
            {
                context.analysis->beginEvent(eventData.readout.eventIndex);

                context.analysis->processModuleData(crateId, eventData.readout.eventIndex,
                    eventData.readout.moduleDataList, eventData.readout.moduleCount);

                context.analysis->endEvent(eventData.readout.eventIndex);
            }
            else if (eventData.type == EventContainer::Type::System && eventData.system.size)
            {
                auto frameInfo = mvlc::extract_frame_info(eventData.system.header[0]);
                assert(frameInfo.type == mvlc::frame_headers::SystemEvent);

                if (frameInfo.sysEventSubType == mvlc::system_event::subtype::UnixTimetick)
                {
                    if (context.analysis && context.runInfo.isReplay)
                        context.analysis->processTimetick();
                }
            }
            else if (nng_msg_len(inputMsg.get()))
            {
                spdlog::warn("analysis_loop (crateId={}): incoming message contains unknown subsection '{}'",
                    crateId, *reinterpret_cast<const u8 *>(nng_msg_body(inputMsg.get())));
                break;
            }
        }

        auto tProcess = sw.interval();

        {
            auto ta = context.readerCounters().access();
            ta->bytesReceived += msgLen;
            ta->messagesReceived++;
            ta->tReceive += tReceive;
            ta->tProcess += tProcess;
            ta->tTotal += sw.end();
            ta->messagesLost = inputBuffersLost;
        }
    }

    //spdlog::info("analysis_nng: lastInputMessageNumber={}, inputBuffersLost={}, totalInput={:.2f} MiB",
    //    lastInputMessageNumber, inputBuffersLost, 1.0 * totalInputBytes / mvlc::util::Megabytes(1));
    spdlog::info("leaving analysis_loop, crateId={} (shouldQuit={})", crateId, context.shouldQuit());
    return result;
}

std::tuple<unsigned, size_t, u32> extract_part_info(const u32 *it, const u32 *end)
{
    unsigned crateId = 0;
    size_t partWords = 0;
    u32 bufferType = 0;

    if (mvlc::is_known_frame_header(*it))
    {
        auto frameInfo = mvlc::extract_frame_info(*it);
        crateId = frameInfo.ctrl;
        partWords = frameInfo.len + 1;
        bufferType = static_cast<u32>(ConnectionType::USB);
        spdlog::trace("replay_loop: crateId {} - found frame header 0x{:08x} (@{})", crateId, *it, fmt::ptr(it));
    }
    else if (it + 1 < end)
    {
        u32 header1 = *(it+1);
        mvlc::eth::PayloadHeaderInfo ethInfo{ *it, header1 };
        crateId = ethInfo.controllerId();
        partWords = ethInfo.dataWordCount() + 2;
        bufferType = static_cast<u32>(ConnectionType::ETH);
        spdlog::trace("replay_loop: crateId {} - found ETH header 0x{:08x} 0x{:08x} (@{})", crateId, *it, header1, fmt::ptr(it));
    }
    else if (it + 1 >= end)
    {
        spdlog::warn("replay_loop: possible first eth header word: 0x{:08x}, second word not in input buffer", *it);
    }
    else
    {
        spdlog::warn("replay_loop: unknown header 0x{:08x}", *it);
    }

    return { crateId, partWords, bufferType };
};

LoopResult replay_loop(ReplayJobContext &context)
{
    set_thread_name("replay_loop");

    spdlog::info("entering replay_loop");
    LoopResult result;

    assert(context.lfh);

    struct Output
    {
        unique_msg msg = make_unique_msg();
        size_t messageNumber = 0;
    };

    auto writers = context.outputWritersByCrate;
    std::vector<Output> outputs(writers.size());
    std::vector<SocketWorkPerformanceCounters> counters(writers.size());

    // algorithm:
    // - if the read buffer is full: increase the buffer size
    // - read 1 MB from input file
    // - iterate over input data
    // - extract crate id and part size
    // - if part is contained in the input data:
    //    - append part to output message for the crate
    // - else:
    //    - move remaining data to the front of the buffer

    mvlc::ReadoutBuffer mainBuf(mvlc::util::Megabytes(1));

    auto prepare_output_message = [&] (unsigned crateId, u32 bufferType)
    {
        if (crateId < outputs.size() && writers[crateId] && !outputs[crateId].msg)
        {
            auto &output = outputs[crateId];
            ReadoutDataMessageHeader messageHeader;
            messageHeader.bufferType = bufferType;
            messageHeader.crateId = crateId;
            messageHeader.messageNumber = ++output.messageNumber;
            output.msg = allocate_prepare_message(messageHeader);

            spdlog::debug("replay_loop: crateId {} - created new output message #{} @ {}",
                crateId, output.messageNumber, fmt::ptr(output.msg.get()));
            spdlog::debug("replay_loop (crateId={}): mainBuf={} words", crateId, mainBuf.viewU32().size());
        }
    };

    auto flush_output = [&] (unsigned crateId)
    {
        if (crateId < outputs.size() && writers[crateId])
        {
            auto &output = outputs[crateId];

            if (output.msg)
            {
                const size_t msgLen = nng_msg_len(output.msg.get());
                spdlog::debug("replay_loop: crateId {} - flushing message {} of size {}, sent so far={}",
                    crateId, output.messageNumber, msgLen, counters[crateId].messagesSent);
                Stopwatch sw;
                int res = writers[crateId]->writeMessage(std::move(output.msg));
                counters[crateId].tSend += sw.interval();
                counters[crateId].bytesSent += msgLen;
                counters[crateId].messagesSent++;
                if (res)
                {
                    spdlog::warn("replay_loop: crateId {} - error writing output message: {}", crateId, nng_strerror(res));
                }
            }
        }
    };

    auto append_to_output = [&] (unsigned crateId, const u32 *it, size_t partWords, u32 bufferType)
    {
        if (crateId < outputs.size() && writers[crateId])
        {
            auto &output = outputs[crateId];

            if (!output.msg || allocated_free_space(output.msg.get()) < partWords * sizeof(u32))
            {
                flush_output(crateId);
                prepare_output_message(crateId, bufferType);

                if (!output.msg)
                {
                    spdlog::error("replay_loop: crateId {} - failed to allocate message",
                        crateId);
                    return; // XXX
                }
            }

            nng_msg_append(output.msg.get(), reinterpret_cast<const u8 *>(it), partWords * sizeof(u32));
        }
    };

    auto process_input_data = [&] (auto input)
    {
        auto nextHeader = std::begin(input);
        auto cratePartsBegin = std::begin(input);
        const auto end = std::end(input);

        auto [crateId, partWords, bufferType] = extract_part_info(nextHeader, end);
        // if partsWords == 0  -> no header info could be extrated

        while (partWords > 0 && nextHeader + partWords <= end)
        {
            if (nextHeader + partWords >= end)
            {
                spdlog::trace("replay_loop: process input data: cannot advanced nextHeader, leaving loop");
                break;
            }

            nextHeader += partWords;
            auto [nextCrateId, nextPartWords, nextBufferType] = extract_part_info(nextHeader, std::end(input));
            partWords = nextPartWords;

            if (nextHeader + nextPartWords > end)
            {
                spdlog::trace("replay_loop: process_input_data: next frame is not fully contained in input (nextHeader={:08x}, nextPartWords={}, nextPartEnd={}, end={}",
                    *nextHeader, nextPartWords, fmt::ptr(nextHeader + nextPartWords), fmt::ptr(end));
                break;
            }

            if (nextCrateId == crateId)
            {
            }
            else
            {
                auto cratePartsEnd = nextHeader;
                auto cratePartWords = std::distance(cratePartsBegin, cratePartsEnd);
                spdlog::trace("replay_loop: flushing to output: crateId={}, cratePartWords={}", crateId, cratePartWords);
                append_to_output(crateId, cratePartsBegin, cratePartWords, bufferType);
                crateId = nextCrateId;
                bufferType = nextBufferType;
                cratePartsBegin = cratePartsEnd;
            }
        }

        if (nextHeader + partWords <= std::end(input))
            nextHeader += partWords;
        auto cratePartsEnd = nextHeader;
        auto cratePartWords = std::distance(cratePartsBegin, cratePartsEnd);

        if (cratePartWords)
        {
            spdlog::trace("replay_loop: process_input_data: end of input reached (partWords={}, cratePartWords={})", partWords, cratePartWords);
            spdlog::trace("replay_loop: flushing to output: crateId={}, cratePartWords={}", crateId, cratePartWords);
            append_to_output(crateId, cratePartsBegin, cratePartWords, bufferType);
            cratePartsBegin = cratePartsEnd;
        }

        return cratePartsBegin;
    };

    auto update_counters = [&]
    {
        for (size_t crateId=0; crateId<counters.size(); ++crateId)
        {
            if (auto cc = context.writerCountersByCrate[crateId].get())
                cc->access().ref() = counters[crateId];
        }
    };

    for (auto &c: counters)
        c.start();

    while (!context.shouldQuit())
    {
        if (mainBuf.free() < mvlc::util::Megabytes(1) * 0.5)
        {
            spdlog::warn("replay_loop: mainBuf is full (used={}, capacity={}), increasing size!", mainBuf.used(), mainBuf.capacity());
            mainBuf.ensureFreeSpace(mvlc::util::Megabytes(1) * 0.5);
        }

        size_t bytesToRead = std::min(mainBuf.free(), mvlc::util::Megabytes(1));
        assert(bytesToRead >= mvlc::util::Megabytes(1) * 0.5);
        size_t bytesRead = 0;

        try
        {
            spdlog::debug("replay_loop: read from file - bytesToRead={}, mainBuf={} words", bytesToRead, mainBuf.viewU32().size());
            //mvlc::util::log_buffer(std::cout, mainBuf.viewU32(), "mainBuf");
            bytesRead = context.lfh->read(mainBuf.data() + mainBuf.used(), bytesToRead);
            mainBuf.use(bytesRead);
            spdlog::debug("replay_loop: read from file - mainBuf.use({}), mainBuf.used()={}", bytesRead, mainBuf.used());
        }
        catch (const std::runtime_error &e)
        {
            spdlog::warn("replay_loop: runtime_error while reading from input file: {}, requestedBytes={}", e.what(), bytesToRead);
            result.exception = std::current_exception();
            break;
        }
        catch (...)
        {
            spdlog::warn("replay_loop: exception while reading from input file");
            result.exception = std::current_exception();
            break;
        }

        auto input = mainBuf.viewU32();
        auto it = process_input_data(input);

        {
            auto wordsConsumed = std::distance(std::begin(input), it);
            auto bytesConsumed = wordsConsumed * sizeof(u32);
            auto bytesToMove = std::distance(mainBuf.data() + bytesConsumed, mainBuf.data() + mainBuf.used());
            spdlog::debug("replay_loop: input consumed: words={}, bytes={}, bytesToMove={}", wordsConsumed, bytesConsumed, bytesToMove);
            assert(mainBuf.data() + bytesConsumed + bytesToMove <= mainBuf.data() + mainBuf.used());
            if (bytesToMove)
            {
                std::memcpy(mainBuf.data(), mainBuf.data() + bytesConsumed, bytesToMove);
                //mvlc::util::log_buffer(std::cout, mainBuf.viewU32(), "partial data left in mainBuf");
            }
            mainBuf.setUsed(bytesToMove);
        }

        update_counters();

        if (bytesRead == 0)
        {
            spdlog::warn("replay_loop: EOF reached");
            break;
        }
    }

    #if 1
    if (mainBuf.used())
    {
        spdlog::warn("replay_loop: {} bytes remaining in main buffer, discarding (shouldQuit={})",
            mainBuf.used(), context.shouldQuit());
        //mvlc::util::log_buffer(std::cout, mainBuf.viewU32(), "mainBuf");
    }
    #endif

    spdlog::debug("replay_loop: flushing remaining messages");

    // Flush remaining messages.
    for (size_t crateId = 0; crateId < outputs.size(); ++crateId)
    {
        flush_output(crateId);
    }

    // Final counters update!
    update_counters();

    spdlog::info("leaving replay_loop (shouldQuit={})", context.shouldQuit());

    return result;
}

LoopResult test_consumer_loop(TestConsumerContext &context)
{
    set_thread_name("test_consumer_loop");

    spdlog::info("entering test_consumer_loop");

    LoopResult result{};

    while (!context.shouldQuit())
    {
        Stopwatch sw;

        auto [inputMsg, res] = context.inputReader()->readMessage();

        if (res && res != NNG_ETIMEDOUT)
        {
            spdlog::error("test_consumer_loop ({}) - receive_message: {}",
                context.name(), nng_strerror(res));
            result.nngError = res;
            break;
        }
        else if (res)
        {
            spdlog::trace("test_consumer_loop ({}) - receive_message: timeout", context.name());
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg.get());

        if (is_shutdown_message(inputMsg.get()))
        {
            spdlog::info("test_consumer_loop ({}): Received shutdown message, leaving loop", context.name());
            break;
        }

        if (msgLen < sizeof(multi_crate::BaseMessageHeader))
        {
            spdlog::warn("test_consumer_loop ({}): incoming message too short (len={})", context.name(), msgLen);
            continue;
        }

        auto tReceive = sw.interval();

        auto baseHeader = *reinterpret_cast<multi_crate::BaseMessageHeader *>(nng_msg_body(inputMsg.get()));

        switch (baseHeader.messageType)
        {
            case MessageType::ReadoutData:
            {
                auto inputHeader = nng::msg_trim_read<multi_crate::ReadoutDataMessageHeader>(inputMsg.get()).value();
                auto messageNumber = inputHeader.messageNumber;
                spdlog::debug("test_consumer_loop ({}): received ReadoutData: crateId={}, messageNumber={}, messageSize={}",
                    context.name(), inputHeader.crateId, messageNumber, msgLen);

                auto inputData = reinterpret_cast<const u32 *>(nng_msg_body(inputMsg.get()));
                size_t inputLen = nng_msg_len(inputMsg.get()) / sizeof(u32);
                std::basic_string_view<u32> view(inputData, inputLen);
                //mvlc::util::log_buffer(std::cout, view, fmt::format("test_consumer_loop ({}): ReadoutData", context.name()));

            } break;
            case MessageType::ParsedEvents:
            {
            } break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // simulate long processing time per buffer

        auto tProcess = sw.interval();

        {
            auto ta = context.readerCounters().access();
            ta->bytesReceived += msgLen;
            ta->messagesReceived++;
            ta->tReceive += tReceive;
            ta->tProcess += tProcess;
            ta->tTotal += sw.end();
            //ta->messagesLost = inputBuffersLost;
        }
    }

    spdlog::info("leaving test_consumer_loop (shouldQuit={})", context.shouldQuit());

    return result;
}

inline nng::unique_msg new_readout_data_message(u8 crateId, u32 messageNumber, u32 bufferType, const std::vector<u8> &data)
{
    multi_crate::ReadoutDataMessageHeader header{};
    header.messageType = multi_crate::MessageType::ReadoutData;
    header.crateId = crateId;
    header.messageNumber = messageNumber;
    header.bufferType = bufferType;

    auto msg = nng::allocate_reserve_message(DefaultOutputMessageReserve);
    assert(nng::allocated_free_space(msg.get()) == DefaultOutputMessageReserve);

    nng_msg_append(msg.get(), &header, sizeof(header));
    assert(nng_msg_len(msg.get()) == sizeof(header));
    assert(nng::allocated_free_space(msg.get()) == DefaultOutputMessageReserve - sizeof(header));

    nng_msg_append(msg.get(), data.data(), data.size());
    assert(nng::allocated_free_space(msg.get()) >= eth::JumboFrameMaxSize);
    assert(nng_msg_len(msg.get()) == sizeof(header) + data.size());

    return msg;
}

inline size_t fixup_readout_data_message(nng_msg *msg, std::vector<u8> &tmpBuf)
{
    auto header = *reinterpret_cast<multi_crate::ReadoutDataMessageHeader *>(nng_msg_body(msg));
    return multi_crate::fixup_listfile_buffer_message(static_cast<mvlc::ConnectionType>(header.bufferType), msg, tmpBuf);
}

// TODO: add try/catch. At least the NngMsgWriteHandle throws
LoopResult readout_loop(MvlcInstanceReadoutContext &context)
{
    set_thread_name(fmt::format("readout_loop{}", context.crateId).c_str());
    spdlog::info(fmt::format("entering readout_loop{}", context.crateId));

    LoopResult result{};
    multi_crate::NngMsgWriteHandle lfh; // Listfile handle to pass to ReadoutLoopPlugins.
    u32 messageNumber = 1u;
    s32 lastPacketNumber = -1;
    std::vector<u8> previousData;
    u32 connectionType = static_cast<u32>(context.mvlc.connectionType());
    eth::MVLC_ETH_Interface *mvlcEth = nullptr;
    usb::MVLC_USB_Interface *mvlcUsb = nullptr;

    auto new_output_message = [&] () -> nng::unique_msg
    {
        auto msg = new_readout_data_message(context.crateId, messageNumber++, connectionType, {});
        lfh.setMessage(msg.get()); // important: set the new output message on the NngMsgWriteHandle
        return msg;
    };

    auto flush_output_message = [&] (unique_msg &&msg)
    {
        const auto msgSize = nng_msg_len(msg.get());
        Stopwatch stopWatch;
        context.outputWriter()->writeMessage(std::move(msg));

        // Important: clear the output message of the NngMsgWriteHandle,
        // otherwise it tries to write to the stale message handle.
        lfh.setMessage(nullptr);

        {
            auto ta = context.writerCounters().access();
            ta->tSend += stopWatch.interval();
            ta->tTotal += stopWatch.end();
            ta->messagesSent++;
            ta->bytesSent += msgSize;
        }

        spdlog::debug("readout_loop{}: flushed output message #{} of size={}",
            context.crateId, messageNumber-1, msgSize);
    };

    auto readout_eth = [&] (nng_msg *dest, std::error_code *ecp = nullptr) -> size_t
    {
        size_t totalBytesRead = 0;
        std::error_code ec_;
        if (!ecp)
            ecp = &ec_;

        *ecp = {};

        auto tStart = std::chrono::steady_clock::now();
        auto msgUsed = nng_msg_len(dest);
        auto msgFree = nng::allocated_free_space(dest);
        auto dataGuard = context.mvlc.getLocks().lockData();

        while (msgFree >= eth::JumboFrameMaxSize)
        {
            auto readResult = mvlcEth->read_packet(
                Pipe::Data,
                reinterpret_cast<u8 *>(nng_msg_body(dest)) + msgUsed,
                msgFree);
            *ecp = readResult.ec;

            if (readResult.bytesTransferred > 0)
            {
                nng_msg_realloc(dest, msgUsed + readResult.bytesTransferred);
                msgUsed = nng_msg_len(dest);
                msgFree = nng::allocated_free_space(dest);
                totalBytesRead += readResult.bytesTransferred;
            }

            if (*ecp)
                break;

            if (auto elapsed = std::chrono::steady_clock::now() - tStart;
                elapsed >= FlushBufferTimeout)
                break;
        }

        return totalBytesRead;
    };

    auto readout_usb = [&] (nng_msg *dest, std::error_code *ecp = nullptr) -> size_t
    {
        // Append leftover data from a previous read to the current message
        // before reading more data.
        if (!previousData.empty())
        {
            auto prevBytes = previousData.size();
            nng_msg_append(dest, previousData.data(), previousData.size());
            previousData.clear();
            spdlog::trace("readout_usb: moved {} bytes from previousData to output message #{}", prevBytes, messageNumber-1);
        }

        size_t totalBytesRead = 0;
        std::error_code ec_;
        *ecp = {};

        auto tStart = std::chrono::steady_clock::now();
        auto msgUsed = nng_msg_len(dest);
        auto msgFree = nng::allocated_free_space(dest);
        auto dataGuard = context.mvlc.getLocks().lockData();

        while (msgFree >= 4)
        {
            size_t bytesTransferred = 0u;

            *ecp = mvlcUsb->read_unbuffered(
                Pipe::Data,
                reinterpret_cast<u8 *>(nng_msg_body(dest)) + msgUsed,
                msgFree,
                bytesTransferred);

            if (bytesTransferred > 0)
            {
                nng_msg_realloc(dest, msgUsed + bytesTransferred);
                msgUsed = nng_msg_len(dest);
                msgFree = nng::allocated_free_space(dest);
                totalBytesRead += bytesTransferred;
                spdlog::debug("readout_loop (readout_usb): read {} bytes from USB", bytesTransferred);
            }

            if (*ecp)
                break;

            if (auto elapsed = std::chrono::steady_clock::now() - tStart;
                elapsed >= FlushBufferTimeout)
                break;
        }

        assert(previousData.empty());

        if (auto bytesMoved = fixup_readout_data_message(dest, previousData))
            spdlog::debug("moved {} bytes from the output message to a temporary buffer (msg #{})", bytesMoved, messageNumber-1);

        return totalBytesRead;
    };

    ReadoutLoopPlugin::Arguments pluginArgs{};
    pluginArgs.crateId = context.crateId;
    pluginArgs.listfileHandle = &lfh;

    std::vector<std::unique_ptr<ReadoutLoopPlugin>> readoutLoopPlugins;
    readoutLoopPlugins.emplace_back(std::make_unique<TimetickPlugin>());

    switch (context.mvlc.connectionType())
    {
        case ConnectionType::ETH:
            mvlcEth = dynamic_cast<eth::MVLC_ETH_Interface *>(context.mvlc.getImpl());
            mvlcEth->resetPipeAndChannelStats(); // reset packet loss counters
            assert(mvlcEth);

            // Send an initial empty frame to the UDP data pipe port so that
            // the MVLC knows where to send the readout data.
            if (auto ec = redirect_eth_data_stream(context.mvlc))
            {
                result.ec = ec;
                return result;
            }
            break;

        case ConnectionType::USB:
            mvlcUsb = dynamic_cast<usb::MVLC_USB_Interface *>(context.mvlc.getImpl());
            assert(mvlcUsb);
            break;
    }

    assert(mvlcEth || mvlcUsb);

    auto msg = new_output_message();
    auto tLastFlush = std::chrono::steady_clock::now();
    context.writerCounters().access()->start();
    context.mvlc.resetStackErrorCounters(); // Reset the MVLC-wide stack error counters

    for (auto &plugin: readoutLoopPlugins)
        plugin->readoutStart(pluginArgs);

    while (!context.shouldQuit())
    {
        if (!msg)
        {
            msg = new_output_message();
            if (!msg)
                break; // TODO: store some error indicator in result (OOM)
        }

        // Run plugins for timetick generation and run duration checks.
        bool stopReadout = false;
        for (const auto &plugin: readoutLoopPlugins)
        {
            if (auto pluginResult = plugin->operator()(pluginArgs);
                pluginResult == ReadoutLoopPlugin::Result::StopReadout)
            {
                stopReadout = true;
            }
        }

        if (stopReadout)
        {
            spdlog::warn("crate{}: readout loop requested to stop by plugin", context.crateId);
            break;
        }

        std::error_code ec;
        size_t bytesTransferred = 0;

        if (mvlcEth)
        {
            bytesTransferred = readout_eth(msg.get(), &ec);
        }
        else if (mvlcUsb)
        {
            bytesTransferred = readout_usb(msg.get(), &ec);
        }

        if (ec == ErrorType::ConnectionError)
        {
            spdlog::error("connection error from mvlc readout: {}", ec.message());
            break;
        }

        spdlog::trace("readout_loop (crate{}): read {} bytes from MVLC", context.crateId, bytesTransferred);

        // Check if either the flush timeout elapsed or there is no more space
        // for packets in the output message.

        auto msgFree = nng::allocated_free_space(msg.get());

        if ((mvlcEth && msgFree < eth::JumboFrameMaxSize)
            || (mvlcUsb && msgFree < 4))
        {
            flush_output_message(std::move(msg));
            tLastFlush = std::chrono::steady_clock::now();
            msg = new_output_message();
        }
        else if (auto elapsed = std::chrono::steady_clock::now() - tLastFlush;
            elapsed >= FlushBufferTimeout || nng::allocated_free_space(msg.get()) < eth::JumboFrameMaxSize)
        {
            if (nng::allocated_free_space(msg.get()) < eth::JumboFrameMaxSize)
                spdlog::debug("crate{}: flushing full output message #{}", context.crateId, messageNumber - 1);
            else
                spdlog::debug("crate{}: flushing output message #{} due to timeout", context.crateId, messageNumber - 1);

            flush_output_message(std::move(msg));
            tLastFlush = std::chrono::steady_clock::now();
            msg = new_output_message();
        }
    }

    for (auto &plugin: readoutLoopPlugins)
        plugin->readoutStop(pluginArgs);

    flush_output_message(std::move(msg));

    spdlog::info(fmt::format("leaving readout_loop{} (shouldQuit={})", context.crateId, context.shouldQuit()));
    return result;
}

LoopResult listfile_writer_loop(ListfileWriterContext &context)
{
    set_thread_name("listfile_writer_loop");

    spdlog::info("entering listfile_writer_loop");

    LoopResult result;
    // Last received message number per crate.
    std::unordered_map<u8, u32> lastMessageNumbers;

    // Per crate counters, specific to the listfile_writer_loop.
    {
        auto ca = context.dataInputCounters.access();
        for (auto &counters: ca.ref())
            counters.start();
    }
    // Standard reader counters from the JobContextInterface. These are used to
    // track the sum of the counters from all crates.
    context.readerCounters().access()->start();

    while (!context.shouldQuit())
    {
        Stopwatch sw;

        auto [inputMsg, res] = context.inputReader()->readMessage();

        if (res && res != NNG_ETIMEDOUT)
        {
            spdlog::error("listfile_writer_loop - receive_message: {}", nng_strerror(res));
            result.nngError = res;
            break;
        }
        else if (res)
        {
            spdlog::trace("listfile_writer_loop - receive_message: timeout");
            continue;
        }

        assert(inputMsg);

        if (is_shutdown_message(inputMsg.get()))
        {
            spdlog::info("listfile_writer_loop: Received shutdown message, leaving loop");
            break;
        }

        const auto msgLen = nng_msg_len(inputMsg.get());

        if (msgLen < sizeof(multi_crate::ReadoutDataMessageHeader))
        {
            spdlog::warn("listfile_writer_loop: Incoming message too short (len={})", msgLen);
            continue;
        }

        auto inputHeader = nng::msg_trim_read<multi_crate::ReadoutDataMessageHeader>(inputMsg.get()).value();

        if (inputHeader.messageType != multi_crate::MessageType::ReadoutData)
        {
            spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ReadoutData));
            continue;
        }

        spdlog::debug("listfile_writer_loop : received message {} of size {} from crate{}",
            (u32)inputHeader.messageNumber, msgLen, inputHeader.crateId);

        auto tReceive = sw.interval();

        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastMessageNumbers[inputHeader.crateId]);

        if (bufferLoss > 0)
        {
            spdlog::warn("listfile_writer_loop: Lost {} messages from crate{}! (header.messageNumber={}, lastMessageNumber={}",
                bufferLoss, inputHeader.crateId, (u32)inputHeader.messageNumber, lastMessageNumbers[inputHeader.crateId]);
        }

        lastMessageNumbers[inputHeader.crateId] = inputHeader.messageNumber;

        //auto dataPtr = reinterpret_cast<const u32 *>(nng_msg_body(inputMsg.get()));
        //size_t dataSize = nng_msg_len(inputMsg.get()) / sizeof(u32);
        //std::basic_string_view<u32> dataView(dataPtr, dataSize);

        if (context.lfh)
        {
            try
            {
                context.lfh->write(reinterpret_cast<const u8 *>(nng_msg_body(inputMsg.get())), nng_msg_len(inputMsg.get()));
            }
            catch(const std::exception& e)
            {
                spdlog::warn("listfile_writer_loop: Error writing to output listfile: {}", e.what());
            }
        }

        auto tProcess = sw.interval();
        auto tTotal = sw.end();

        {
            auto ca = context.dataInputCounters.access();
            auto &counters = ca.ref()[inputHeader.crateId];
            counters.bytesReceived += msgLen;
            counters.messagesLost += bufferLoss;
            counters.messagesReceived++;
            counters.tReceive += tReceive;
            counters.tProcess += tProcess;
            counters.tTotal += tTotal;
        }
        {
            auto ca = context.readerCounters().access();
            auto &counters = ca.ref();
            counters.bytesReceived += msgLen;
            counters.messagesLost += bufferLoss;
            counters.messagesReceived++;
            counters.tReceive += tReceive;
            counters.tProcess += tProcess;
            counters.tTotal += tTotal;
        }
    }

    spdlog::info("leaving listfile_writer_loop (shouldQuit={})", context.shouldQuit());

    return result;
}

std::vector<LoopResult> shutdown_pipeline(CratePipeline &pipeline)
{
    spdlog::info("leave shutdown_pipeline");
    std::vector<LoopResult> results;

    for (auto &step: pipeline)
    {
        LoopResult result;

        if (step.context->jobRuntime().isRunning())
        {
            spdlog::info("waiting for job {} to finish", step.context->name());
            auto result = step.context->jobRuntime().wait();
            spdlog::info("job {} finished: {}", step.context->name(), result.toString());
            step.context->setLastResult(result);
        }
        else
        {
            spdlog::info("job {} already finished", step.context->name());
            result = step.context->lastResult().value_or(LoopResult{});
        }

        results.emplace_back(std::move(result));

        step.context->readerCounters().access()->stop();
        step.context->writerCounters().access()->stop();
        step.context->setQuit(false); // important, otherwise output writers will not send shutdown messages due to the retryPredicate failing

        if (step.writer)
        {
            if (int res = send_shutdown_message(*step.writer))
                spdlog::warn("error sending shutdown message from {}: {}", step.context->name(), nng_strerror(res));
            else
                spdlog::info("shutdown messages sent from {}, waiting for jobs to finish", step.context->name());
        }
    }

    spdlog::info("leave shutdown_pipeline");
    return results;
}

std::vector<LoopResult> quit_pipeline(CratePipeline &pipeline)
{
    spdlog::info("enter quit_pipeline");
    std::vector<LoopResult> results;

    for (auto &step: pipeline)
    {
        LoopResult result = {};

        if (step.context->jobRuntime().isRunning())
        {
            spdlog::info("waiting for job {} to finish", step.context->name());
            step.context->quit();
            auto result = step.context->jobRuntime().wait();
            spdlog::info("job {} finished: {}", step.context->name(), result.toString());
            step.context->setLastResult(result);
        }
        else
        {
            spdlog::warn("job {} already finished", step.context->name());
            result = step.context->lastResult().value_or(LoopResult{});
        }

        results.emplace_back(std::move(result));

        step.context->readerCounters().access()->stop();
        step.context->writerCounters().access()->stop();
        step.context->setQuit(false); // important, otherwise output writers will not send shutdown messages due to the retryPredicate failing
    }

    spdlog::info("leave quit_pipeline");
    return results;
}

int close_pipeline(CratePipeline &pipeline)
{
    int ret = 0;
    for (auto &step: pipeline)
    {
        if (!step.inputLink.url.empty())
        {
            spdlog::debug("closing inputLink dialer {} (context={})", step.inputLink.url, step.context->name());
            if (int res = nng_close(step.inputLink.dialer))
            {
                spdlog::warn("close inputLink.dialer {}: {}", step.inputLink.url, nng_strerror(res));
                ret = res;
            }
        }

        if (!step.outputLink.url.empty())
        {
            spdlog::debug("closing outputLink listener {} (context={})", step.outputLink.url, step.context->name());
            if (int res = nng_close(step.outputLink.listener))
            {
                spdlog::warn("close outputLink.listener {}: {}", step.outputLink.url, nng_strerror(res));
                ret = res;
            }
        }
    }
    return ret;
}

size_t empty_pipeline_inputs(CratePipeline &pipeline)
{
    size_t messages = 0;
    for (auto &step: pipeline)
    {
        int res = 0;
        do
        {
            nng_msg *msg = nullptr;
            // Using  NNG_FLAG_NONBLOCK here doesn't catch all remaining
            // messages so we have to run into read timeouts I guess.
            res = nng_recvmsg(step.inputLink.dialer, &msg, 0);

            if (msg)
            {
                ++messages;
                nng_msg_free(msg);
            }
        } while (res == 0);
    }
    return messages;
}

std::vector<std::shared_ptr<JobContextInterface>> get_all_pipeline_jobs(const CratePipeline &pipeline)
{
    return std::accumulate(std::begin(pipeline), std::end(pipeline), std::vector<std::shared_ptr<JobContextInterface>>{},
        [] (auto &acc, const auto &step) { acc.push_back(step.context); return acc; });
}

CratePipelineStep make_replay_step(const std::shared_ptr<ReplayJobContext> &replayContext, u8 crateId, nng::SocketLink outputLink)
{
    auto writer = std::make_unique<nng::SocketOutputWriter>(outputLink.listener);
    writer->debugInfo = fmt::format("replay_loop (crateId={})", crateId);
    writer->retryPredicate = [ctx=replayContext.get()] { return !ctx->shouldQuit(); };

    auto writerWrapper = std::make_shared<nng::MultiOutputWriter>();
    writerWrapper->addWriter(std::move(writer));

    // Use a wrapper context here so that we have a distinct context per crate stream.
    auto contextWrapper = std::make_shared<CrateReplayWrapperContext>();
    contextWrapper->crateId = crateId;
    contextWrapper->replayContext = replayContext;
    contextWrapper->setOutputWriter(writerWrapper.get());

    CratePipelineStep result;
    result.outputLink = outputLink;
    result.writer = writerWrapper;
    result.context = contextWrapper;
    return result;
}

CratePipelineStep make_readout_step(const std::shared_ptr<MvlcInstanceReadoutContext> &ctx, nng::SocketLink outputLink)
{
    auto writer = std::make_unique<nng::SocketOutputWriter>(outputLink.listener);
    writer->debugInfo = fmt::format("readout_loop (crateId={})", ctx->crateId);
    writer->retryPredicate = [ctx=ctx.get()] { return !ctx->shouldQuit(); };

    auto writerWrapper = std::make_shared<nng::MultiOutputWriter>();
    writerWrapper->addWriter(std::move(writer));

    ctx->setOutputWriter(writerWrapper.get());

    CratePipelineStep result;
    result.outputLink = outputLink;
    result.writer = writerWrapper;
    result.context = ctx;
    return result;
}

// Standard single input, single output processing step.
static CratePipelineStep make_processing_step(const std::shared_ptr<JobContextInterface> &context, nng::SocketLink inputLink, nng::SocketLink outputLink)
{
    auto reader = std::make_shared<nng::SocketInputReader>(inputLink.dialer);
    reader->debugInfo = context->name();
    context->setInputReader(reader.get());

    auto writer = std::make_unique<nng::SocketOutputWriter>(outputLink.listener);
    writer->debugInfo = context->name();
    writer->retryPredicate = [ctx=context.get()] { return !ctx->shouldQuit(); };

    auto writerWrapper = std::make_shared<nng::MultiOutputWriter>();
    writerWrapper->addWriter(std::move(writer));

    context->setOutputWriter(writerWrapper.get());

    CratePipelineStep result;
    result.inputLink = inputLink;
    result.outputLink = outputLink;
    result.reader = reader;
    result.writer = writerWrapper;
    result.context = context;
    return result;
}

CratePipelineStep make_readout_parser_step(const std::shared_ptr<ReadoutParserContext> &context, nng::SocketLink inputLink, nng::SocketLink outputLink)
{
    return make_processing_step(context, inputLink, outputLink);
}

CratePipelineStep make_multievent_splitter_step(const std::shared_ptr<MultiEventSplitterContext> &context, nng::SocketLink inputLink, nng::SocketLink outputLink)
{
    return make_processing_step(context, inputLink, outputLink);
}

CratePipelineStep make_analysis_step(const std::shared_ptr<AnalysisProcessingContext> &context, nng::SocketLink inputLink)
{
    auto reader = std::make_shared<nng::SocketInputReader>(inputLink.dialer);
    reader->debugInfo = context->name();
    context->setInputReader(reader.get());

    CratePipelineStep result;
    result.inputLink = inputLink;
    result.reader = reader;
    result.context = context;
    return result;
}

CratePipelineStep make_test_consumer_step(const std::shared_ptr<TestConsumerContext> &context, nng::SocketLink inputLink)
{
    auto reader = std::make_shared<nng::SocketInputReader>(inputLink.dialer);
    reader->debugInfo = context->name();
    context->setInputReader(reader.get());

    CratePipelineStep result;
    result.inputLink = inputLink;
    result.reader = reader;
    result.context = context;
    return result;
}

CratePipelineStep make_listfile_writer_step(const std::shared_ptr<ListfileWriterContext> &context, nng::SocketLink inputLink)
{
    auto reader = std::make_shared<nng::SocketInputReader>(inputLink.dialer);
    reader->debugInfo = context->name();
    context->setInputReader(reader.get());

    CratePipelineStep result;
    result.inputLink = inputLink;
    result.reader = reader;
    result.context = context;
    return result;
}

void Executor::startJob(const std::shared_ptr<JobContextInterface> &context)
{
    auto fWrap = [context, this] () -> LoopResult
    {
        assert(!context->lastResult().has_value());
        std::unique_lock<std::mutex> lock(mutex_);
        for (auto &observer : observers_)
            observer->onJobStarted(context);
        lock.unlock();

        LoopResult result;

        try
        {
            result = context->function()();
        }
        catch (const std::exception &e)
        {
            result.exception = std::current_exception();
        }

        context->setQuit(false);

        lock.lock();
        for (auto &observer : observers_)
            observer->onJobFinished(context);

        return result;
    };

    auto task = std::packaged_task<LoopResult()>(fWrap);
    JobRuntime rt;
    rt.context = context.get();
    rt.result = task.get_future();
    context->clearLastResult();
    context->setQuit(false);
    context->readerCounters().access()->start();
    context->writerCounters().access()->start();
    rt.thread = std::thread([t = std::move(task)]() mutable { t.make_ready_at_thread_exit(); });
    context->setJobRuntime(std::move(rt));
}

void Executor::addObserver(const std::shared_ptr<JobObserverInterface> &observer)
{
    std::lock_guard<std::mutex> lock(mutex_);
    observers_.push_back(observer);
}

void Executor::removeObserver(const std::shared_ptr<JobObserverInterface> &observer)
{
    std::lock_guard<std::mutex> lock(mutex_);
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer), observers_.end());
}

bool is_running(const CratePipeline &pipeline)
{
    const bool anyRunning = std::any_of(std::begin(pipeline), std::end(pipeline),
        [] (const auto &step) { return step.context->jobRuntime().isRunning(); });
    return anyRunning;
}

bool is_idle(const CratePipeline &pipeline)
{
    return !is_running(pipeline);
}

bool start_pipeline(Executor &executor, CratePipeline &pipeline)
{
    if (!is_idle(pipeline))
    {
        spdlog::warn("start_pipeline: pipeline is not idle");
        return false;
    }

    for (auto &step: pipeline)
    {
        // Guard against restarting a job that already terminated while we were
        // still starting other jobs.
        if (!step.context->jobRuntime().isRunning() && !step.context->jobRuntime().isReady())
        {
            executor.startJob(step.context);
            spdlog::info("started job {}", step.context->name());
        }
        else
        {
            spdlog::info("job {} was already running", step.context->name());
        }
    }

    return true;
}

bool stop_pipeline(CratePipeline &pipeline, bool immediateShutdown)
{
    if (is_idle(pipeline))
    {
        spdlog::warn("stop_pipeline: pipeline is already idle");
        return false;
    }

    if (immediateShutdown)
        quit_pipeline(pipeline);
    else
        shutdown_pipeline(pipeline);

    Stopwatch swEmpty;
    empty_pipeline_inputs(pipeline);
    spdlog::warn("empty pipeline took {} ms", swEmpty.interval().count() / 1000.0);

    return true;
}

bool start_pipelines(Executor &executor, std::vector<CratePipeline> &pipelines)
{
    for (auto &pipeline: pipelines)
    {
        if (!is_idle(pipeline))
        {
            spdlog::warn("start_pipelines: pipeline is not idle");
            return false;
        }
    }

    for (auto &pipeline: pipelines)
        start_pipeline(executor, pipeline);

    return true;
}

void stop_pipelines(std::vector<CratePipeline> &pipelines, bool immediate)
{
    for (auto &pipeline: pipelines)
        stop_pipeline(pipeline, immediate);
}

void JobsWatcher::addJob(const std::shared_ptr<JobContextInterface> &job)
{
    allJobs.insert(job);
}

void JobsWatcher::onJobStarted(const std::shared_ptr<JobContextInterface> &job)
{
    auto logger = mvlc::get_logger("JobsWatcher");

    if (allJobs.find(job) != std::end(allJobs))
    {
        logger->info("job {} started", job->name());

        if (startedJobs.find(job) == std::end(startedJobs))
            startedJobs.insert(job);
        else
            logger->warn("job {} started again", job->name());

        if (startedJobs == allJobs)
            emit allJobsStarted();
    }
    else
        logger->warn("unknown job {} started", job->name());
}

void JobsWatcher::onJobFinished(const std::shared_ptr<JobContextInterface> &job)
{
    auto logger = mvlc::get_logger("JobsWatcher");

    if (allJobs.find(job) != std::end(allJobs))
    {
        logger->info("job {} finished", job->name());

        if (finishedJobs.find(job) == std::end(finishedJobs))
            finishedJobs.insert(job);
        else
            logger->warn("job {} finished again", job->name());

        if (finishedJobs == allJobs)
            emit allJobsFinished();
    }
    else
        logger->warn("unknown job {} finished", job->name());
}

void JobsWatcher::removeAllJobs()
{
    allJobs.clear();
    startedJobs.clear();
    finishedJobs.clear();
}

void JobsWatcher::reset()
{
    startedJobs.clear();
    finishedJobs.clear();
}

std::pair<std::vector<SocketLink>, int> build_stage1_socket_links(const Stage1BuildInfo &buildInfo)
{
    std::vector<nng::CreateLinkInfo> linkInfos;
    // First link: readout/replay produces -> readout_parser consumes. In DAQ
    // mode this is a lossfull pubsub link to avoid stalling the DAQ and missing
    // readout data.
    linkInfos.emplace_back(nng::CreateLinkInfo{
         .type = buildInfo.isReplay ? nng::LinkType::Pair : nng::LinkType::PubSub,
         .url = fmt::format("inproc://{}crate{}_stage0_raw_data", buildInfo.uniqueUrlPart, buildInfo.crateId),
    });

    // readout_parser -> splitter/eb/ana
    linkInfos.emplace_back(nng::CreateLinkInfo{
         .type = nng::LinkType::Pair,
         .url = fmt::format("inproc://{}crate{}_stage1_step0_parsed_data", buildInfo.uniqueUrlPart, buildInfo.crateId),
    });

    if (buildInfo.withSplitter)
    {
       // splitter -> eb/ana
       linkInfos.emplace_back(nng::CreateLinkInfo{
           .type = nng::LinkType::Pair,
           .url = fmt::format("inproc://{}crate{}_stage1_step1_split_data", buildInfo.uniqueUrlPart, buildInfo.crateId),
       });
    }

    if (buildInfo.withEventBuilder)
    {
       // eb -> ana
       linkInfos.emplace_back(nng::CreateLinkInfo{
           .type = nng::LinkType::Pair,
           .url = fmt::format("inproc://{}crate{}_stage1_step2_time_matched_data", buildInfo.uniqueUrlPart, buildInfo.crateId),
       });
    }

    return build_socket_pipeline(linkInfos);
}

std::pair<std::vector<SocketLink>, int> build_stage2_socket_links(const Stage2BuildInfo &buildInfo)
{
    std::vector<nng::CreateLinkInfo> linkInfos;

    if (buildInfo.withEventBuilder)
    {
       // stage2 input -> eb
       linkInfos.emplace_back(nng::CreateLinkInfo{
           .type = nng::LinkType::Pair,
           .url = fmt::format("inproc://{}crate{}_stage2_step0", buildInfo.uniqueUrlPart, buildInfo.crateId),
       });
    }

    // 'eb -> ana' or 'stage2 input -> ana'
    linkInfos.emplace_back(nng::CreateLinkInfo{
         .type = nng::LinkType::Pair,
         .url = fmt::format("inproc://{}crate{}_stage2_step1", buildInfo.uniqueUrlPart, buildInfo.crateId),
    });
    return build_socket_pipeline(linkInfos);
}

}
