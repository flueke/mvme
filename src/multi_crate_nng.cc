#include "multi_crate_nng.h"

#include <mesytec-mvlc/mesytec-mvlc.h>

#include "mvlc_daq.h"
#include "util/stopwatch.h"

using namespace mesytec::mvlc;
using namespace mesytec::nng;
using namespace mesytec::util;

namespace mesytec::mvme::multi_crate
{

//static constexpr std::chrono::milliseconds FlushBufferTimeout(500);
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
inline bool parser_maybe_alloc_output(ReadoutParserContext &ctx)
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

    StopWatch stopWatch;

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
        ctx.tLastFlush = std::chrono::steady_clock::now();
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
    set_thread_name("readout_parser_loop");

    LoopResult result;
    const auto crateId = context.crateId;

    spdlog::info("entering readout_parser_loop, crateId={}", crateId);

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

    context.tLastFlush = std::chrono::steady_clock::now();
    SocketWorkPerformanceCounters counters;
    counters.start();

    while (!context.shouldQuit())
    {
        StopWatch sw;

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
            spdlog::debug("readout_parser_loop (crateId={}): Received shutdown message, leaving loop", crateId);
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
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ParsedEvents));
            continue;
        }

        auto tReceive = sw.interval();

        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastInputMessageNumber);
        inputBuffersLost += bufferLoss >= 0 ? bufferLoss : 0u;
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

    spdlog::info("leaving readout_parser_loop, crateId={}", crateId);

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
    // FIXME: this thing receives data from multiple event builders so a single message loss calculation is not enough.
    u32 lastInputMessageNumber = 0u;
    size_t inputBuffersLost = 0;
    bool error = false;
    vme_analysis_common::TimetickGenerator timetickGen;
    context.readerCounters().access()->start();
    const auto crateId = context.crateId;

    spdlog::info("entering analysis_loop (crateId={})", crateId);

    while (!error && !context.shouldQuit())
    {
        if (!context.isReplay)
        {
            int elapsedSeconds = timetickGen.generateElapsedSeconds();

            while (elapsedSeconds >= 1)
            {
                context.analysis->processTimetick();
                elapsedSeconds--;
            }
        }

        StopWatch sw;

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
            spdlog::debug("analysis_loop (crateId={}) - receive_message: timeout", crateId);
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg.get());

        if (is_shutdown_message(inputMsg.get()))
        {
            spdlog::debug("analysis_loop (crateId={}): Received shutdown message, leaving loop", crateId);
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

        // FIXME: loss calcs per crate
        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastInputMessageNumber);
        inputBuffersLost += bufferLoss >= 0 ? bufferLoss : 0u;;
        lastInputMessageNumber = inputHeader.messageNumber;

        spdlog::debug("analysis_loop (crateId={}): received message {} of size {}", crateId, lastInputMessageNumber, msgLen);

        if (context.asp)
        {
            auto a = context.asp->daqStats_.access();
            a->droppedBuffers += inputBuffersLost;
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

                // FIXME: eventData.crateId != crateId

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
                    if (context.analysis && context.isReplay)
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
    spdlog::info("leaving analysis_loop, crateId={}", crateId);
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
                StopWatch sw;
                writers[crateId]->writeMessage(std::move(output.msg));
                counters[crateId].tSend += sw.interval();
                counters[crateId].bytesSent += msgLen;
                counters[crateId].messagesSent++;
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

            #if 0
            if (bytesRead == 0)
            {
                spdlog::warn("replay_loop: EOF reached");
                break;
            }
            #endif
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

        #if 0
        if (it == std::begin(input))
        {
            spdlog::warn("replay_loop: no complete input part in mainBuf (size={} words)", input.size());
        }
        else
        #endif
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

        context.writerCountersByCrate.access().ref() = counters;

        if (bytesRead == 0)
        {
            spdlog::warn("replay_loop: EOF reached");
            break;
        }
    }

    #if 1
    if (mainBuf.used())
    {
        spdlog::warn("replay_loop: {} bytes remaining in main buffer, discarding", mainBuf.used());
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
    context.writerCountersByCrate.access().ref() = counters;

    spdlog::info("leaving replay_loop");

    return result;
}

LoopResult test_consumer_loop(TestConsumerContext &context)
{
    set_thread_name("test_consumer_loop");

    spdlog::info("entering test_consumer_loop");

    LoopResult result{};

    while (!context.shouldQuit())
    {
        StopWatch sw;

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
            spdlog::debug("test_consumer_loop ({}) - receive_message: timeout", context.name());
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg.get());

        if (is_shutdown_message(inputMsg.get()))
        {
            spdlog::debug("test_consumer_loop ({}): Received shutdown message, leaving loop", context.name());
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

    spdlog::info("leaving test_consumer_loop");

    return result;
}

}
