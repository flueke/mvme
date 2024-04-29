#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/util/udp_sockets.h>
#include <QApplication>
#include <signal.h>

#ifndef __WIN32
#include <poll.h>
#else
#include <winsock2.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "analysis/analysis.h"
#include "multi_crate.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_mvlc_listfile.h"
#include "mvme_session.h"
#include "util/mesy_nng.h"
#include "util/mesy_nng_pipeline.h"
#include "util/stopwatch.h"
#include "vme_config.h"

using namespace mesytec;
using namespace mesytec::mvlc;
using namespace mesytec::mvme;

static std::atomic<bool> signal_received = false;

void signal_handler(int signum)
{
    std::cerr << "signal " << signum << "\n";
    std::cerr.flush();
    signal_received = true;
}

void setup_signal_handlers()
{
#ifndef __WIN32
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handlers");
    }
#endif
    // TODO: add signal handling for windows
}

struct MvlcEthReadoutLoopContext
{
    std::atomic<bool> quit;

    // This is put into output ReadoutDataMessageHeader messages and passed
    // to ReadoutLoopPlugins.
    u8 crateId;

    // The MVLC data stream is read from this socket.
    int mvlcDataSocket;

    // Readout data is written to this socket. Expected to use a lossless,
    // blocking protocol, e.g. pair or push as this data goes to the output
    // listfile.
    nng_socket dataOutputSocket;

    // Readout data is also written to this socket. Should be a lossfull,
    // non-blocking protocol, e.g. pub.
    nng_socket snoopOutputSocket;
};

static const size_t DefaultOutputMessageReserve = mvlc::util::Megabytes(1) + sizeof(multi_crate::ReadoutDataMessageHeader);
static const std::chrono::milliseconds FlushBufferTimeout(500);

void mvlc_eth_readout_loop(MvlcEthReadoutLoopContext &context)
{
#ifdef __linux__
    prctl(PR_SET_NAME,"mvlc_eth_readout_loop",0,0,0);
#endif

    spdlog::info("entering mvlc_eth_readout_loop, crateId={}", context.crateId);

    multi_crate::NngMsgWriteHandle lfh;
    u32 messageNumber = 1u;
    std::vector<u8> previousData;
    s32 lastPacketNumber = -1;

    auto new_output_message = [&] () -> nng_msg *
    {
        nng_msg *msg = {};

        if (auto res = nng::allocate_reserve_message(&msg, DefaultOutputMessageReserve))
        {
            spdlog::error("mvlc_eth_readout_loop: could not allocate nng output message: {}", nng_strerror(res));
            return nullptr;
        }

        lfh.setMessage(msg);

        multi_crate::ReadoutDataMessageHeader header{};
        header.messageType = multi_crate::MessageType::ListfileBuffer;
        header.messageNumber = messageNumber++;
        header.bufferType = static_cast<u32>(mvlc::ConnectionType::ETH);
        header.crateId = context.crateId;

        {
            auto messageNumber = header.messageNumber; // fix for gcc + spdlog/fmt + packed struct members
            spdlog::debug("mvlc_eth_readout_loop: preparing new output message: crateId={}, messageNumber={}",
                header.crateId, messageNumber);
        }

        nng_msg_append(msg, &header, sizeof(header));
        assert(nng_msg_len(msg) == sizeof(header));

        nng_msg_append(msg, previousData.data(), previousData.size());
        previousData.clear();

        return msg;
    };

    auto flush_output_message = [&] (nng_msg *msg) -> int
    {
        multi_crate::fixup_listfile_buffer_message(mvlc::ConnectionType::ETH, msg, previousData);
        nng_msg *msgClone = nullptr;

        if (nng_socket_id(context.snoopOutputSocket) >= 0)
        {
            if (auto res = nng_msg_dup(&msgClone, msg))
            {
                spdlog::error("mvlc_eth_readout_loop: could not allocate nng output message");
                return res;
            }
        }

        // Retry until the external quit flag is set.
        auto retryPredicate = [&]
        {
            return !context.quit;
        };

        // The main (blocking) output socket. listfile_writer or similar reads
        // from the other end.
        if (auto res = nng::send_message_retry(context.dataOutputSocket, msg, retryPredicate))
        {
            nng_msg_free(msg);
            nng_msg_free(msgClone); // TODO: is it ok to pass nullptr here?
            return res;
        }

        // Also write to the snoop socket if one was setup. Only one send attempt.
        if (msgClone)
        {
            if (auto res = nng::send_message_retry(context.snoopOutputSocket, msgClone, 1))
            {
                spdlog::warn("mvlc_eth_readout_loop: could not write to snoop output: {}", nng_strerror(res));
                nng_msg_free(msgClone);
            }
        }

        return 0;
    };

    ReadoutLoopPlugin::Arguments pluginArgs{};
    pluginArgs.crateId = context.crateId;
    pluginArgs.listfileHandle = &lfh;

    std::vector<std::unique_ptr<ReadoutLoopPlugin>> readoutLoopPlugins;
    readoutLoopPlugins.emplace_back(std::make_unique<TimetickPlugin>());

    auto msg = new_output_message();

    for (auto &plugin: readoutLoopPlugins)
        plugin->readoutStart(pluginArgs);

    auto tLastFlush = std::chrono::steady_clock::now();

    while (!context.quit)
    {
        assert(nng::allocated_free_space(msg) >= eth::JumboFrameMaxSize);

        auto msgUsed = nng_msg_len(msg);

        // Note: should not alloc as we reserved space when the message was
        // created. This just increases the size of the message.
        nng_msg_realloc(msg, msgUsed + eth::JumboFrameMaxSize);

        size_t bytesTransferred = 0;
        auto ec = eth::receive_one_packet(
            context.mvlcDataSocket,
            reinterpret_cast<u8 *>(nng_msg_body(msg)) + msgUsed,
            eth::JumboFrameMaxSize, bytesTransferred, 100);

        if (ec && ec != std::errc::resource_unavailable_try_again)
        {
            spdlog::error("Error reading from mvlc data socket: {}", ec.message());
            break;
        }

        assert(bytesTransferred <= eth::JumboFrameMaxSize);

        if (bytesTransferred > 0)
        {
            eth::PacketReadResult prr{};
            prr.buffer = reinterpret_cast<u8 *>(nng_msg_body(msg)) + msgUsed;
            prr.bytesTransferred = bytesTransferred;

            if (prr.hasHeaders())
            {
                //spdlog::debug("mvlc_eth_readout_loop (crate{}): incoming packet: packetNumber={}, crateId={}, size={} bytes",
                //    context.crateId, prr.packetNumber(), prr.controllerId(), prr.bytesTransferred);
            }

            if (!prr.hasHeaders())
            {
                spdlog::warn("crate{}: no valid headers in received packet of size {}. Dropping the packet!",
                    context.crateId, prr.bytesTransferred);
            }
            else if (prr.controllerId() != context.crateId)
            {
                spdlog::warn("crate{}: incoming data packet has crateId={} set, excepted {}. Dropping the packet!",
                    context.crateId, prr.controllerId(), context.crateId);
            }
            else
            {
                if (lastPacketNumber >= 0)
                {
                    if (auto loss = eth::calc_packet_loss(
                        lastPacketNumber, prr.packetNumber()))
                    {
                        spdlog::warn("crate{}: lost {} incoming data packets!",
                            context.crateId, loss);
                    }
                }

                // Update the message size. This should not alloc as we can only shrink
                // the message here.
                nng_msg_realloc(msg, msgUsed + bytesTransferred);

                // Cross check size here.
                assert(nng_msg_len(msg) == msgUsed + bytesTransferred);
            }
        }

        // Run plugins (currently only timetick generation here).
        for (const auto &plugin: readoutLoopPlugins)
        {
            plugin->operator()(pluginArgs);
        }

        // Check if either the flush timeout elapsed or there is no more space
        // for packets in the output message.
        if (auto elapsed = std::chrono::steady_clock::now() - tLastFlush;
            elapsed >= FlushBufferTimeout || nng::allocated_free_space(msg) < eth::JumboFrameMaxSize)
        {
            if (nng::allocated_free_space(msg) < eth::JumboFrameMaxSize)
                spdlog::trace("crate{}: flushing full output message #{}", context.crateId, messageNumber - 1);
            else
                spdlog::trace("crate{}: flushing output message #{} due to timeout", context.crateId, messageNumber - 1);


            if (flush_output_message(msg) != 0)
                return;

            msg = new_output_message();

            if (!msg)
                return;

            tLastFlush = std::chrono::steady_clock::now();
        }
    }

    for (auto &plugin: readoutLoopPlugins)
        plugin->readoutStop(pluginArgs);

    spdlog::info("leaving mvlc_eth_readout_loop, crateId={}", context.crateId);
}

struct ListfileWriterContext
{
    std::atomic<bool> quit;

    // Readout data in the form of ReadoutDataMessageHeader messages is read
    // from this socket.
    nng_socket dataInputSocket;

    // This is where readout data is written to if non-null.
    std::unique_ptr<listfile::WriteHandle> lfh;
};

void listfile_writer_loop(ListfileWriterContext &context)
{
#ifdef __linux__
    prctl(PR_SET_NAME,"listfile_writer_loop",0,0,0);
#endif

    spdlog::info("entering listfile_writer_loop");

    // Last received message number per crate.
    std::array<u32, frame_headers::CtrlIdMask+1> lastMessageNumbers;
    lastMessageNumbers.fill(0);

    while (!context.quit)
    {
        nng_msg *msg = nullptr;

        if (auto res = nng::receive_message(context.dataInputSocket, &msg))
        {
            if (res != NNG_ETIMEDOUT)
                spdlog::warn("listfile_writer_loop: Error reading from data input socket: {}", nng_strerror(res));
            continue;
        }

        assert(msg != nullptr);

        if (nng_msg_len(msg) < sizeof(multi_crate::ReadoutDataMessageHeader))
        {
            spdlog::warn("listfile_writer_loop: Incoming message is too short!");
            // TODO: count this error (should not happen)
            nng_msg_free(msg);
            continue;
        }

        auto header = *reinterpret_cast<multi_crate::ReadoutDataMessageHeader *>(nng_msg_body(msg));

        if (header.crateId > frame_headers::CtrlIdMask)
        {
            spdlog::warn("listfile_writer_loop: Invalid crateId={} in incoming data packet!", header.crateId);
            nng_msg_free(msg);
            continue;
        }

        {
            auto messageNumber = header.messageNumber; // fix for gcc + spdlog/fmt + packed struct members
            spdlog::debug("listfile_writer_loop: incoming message from crate{}, messageNumber={}",
                header.crateId, messageNumber);
        }

        {
            auto messageNumber = header.messageNumber; // fix for gcc + spdlog/fmt + packed struct members
            if (auto loss = readout_parser::calc_buffer_loss(header.messageNumber, lastMessageNumbers[header.crateId]))
                spdlog::warn("listfile_writer_loop: lost {} messages from crate{}! (header.messageNumber={}, lastMessageNumber={}",
                loss, header.crateId, messageNumber, lastMessageNumbers[header.crateId]);
        }

        lastMessageNumbers[header.crateId] = header.messageNumber;

        // Trim off the header from the front of the message. The rest of the
        // message is pure readout data and added system event frames.
        nng_msg_trim(msg, sizeof(multi_crate::ReadoutDataMessageHeader));

        auto dataPtr = reinterpret_cast<const u32 *>(nng_msg_body(msg));
        size_t dataSize = nng_msg_len(msg) / sizeof(u32);
        std::basic_string_view<u32> dataView(dataPtr, dataSize);

        if (context.lfh)
        {
            try
            {
                context.lfh->write(reinterpret_cast<const u8 *>(nng_msg_body(msg)), nng_msg_len(msg));
            }
            catch(const std::exception& e)
            {
                spdlog::warn("listfile_writer_loop: Error writing to output listfile: {}", e.what());
            }
        }

        nng_msg_free(msg);
    }

    spdlog::info("leaving listfile_writer_loop");
}

struct ReadoutParserNngContext
{
    nng_msg *outputMessage = nullptr;
    bool outputError = false;
    u32 outputMessageNumber = 0u;
    nng_socket outputSocket = NNG_SOCKET_INITIALIZER;
    size_t totalReadoutEvents = 0u;
    size_t totalSystemEvents = 0u;
};

// Allocates and prepares a new ParsedEventsMessageHeader message if there isn't
// one in the context object.
bool parser_maybe_alloc_output(ReadoutParserNngContext &ctx)
{
    auto &msg = ctx.outputMessage;

    if (msg)
        return false;

    if (nng::allocate_reserve_message(&msg, DefaultOutputMessageReserve))
        return false;

    multi_crate::ParsedEventsMessageHeader header{};
    header.messageType = multi_crate::MessageType::ParsedEvents;
    header.messageNumber = ++ctx.outputMessageNumber;

    nng_msg_append(msg, &header, sizeof(header));
    assert(nng_msg_len(msg) == sizeof(header));

    return true;
}

bool flush_output_message(ReadoutParserNngContext &ctx, const char *debugInfo = "")
{
    const auto msgSize = nng_msg_len(ctx.outputMessage);

    if (auto res = nng::send_message_retry(ctx.outputSocket, ctx.outputMessage, 3, debugInfo))
    {
        nng_msg_free(ctx.outputMessage);
        ctx.outputMessage = nullptr;
        ctx.outputError = true;
        spdlog::error("{}: send_message_retry: {}:", debugInfo, nng_strerror(res));
        return false;
    }

    ctx.outputMessage = nullptr;

    spdlog::debug("{}: sent message {} of size {}",
        debugInfo, ctx.outputMessageNumber, msgSize);

    return true;
}

// The event data callback for the readout parser. Serializes the parsed module
// data list into the current output message.
void parser_nng_eventdata(void *ctx_, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());
    assert(eventIndex >= 0 && eventIndex <= std::numeric_limits<u8>::max());
    assert(moduleCount < std::numeric_limits<u8>::max());


    auto &ctx = *reinterpret_cast<ReadoutParserNngContext *>(ctx_);
    ++ctx.totalReadoutEvents;
    auto &msg = ctx.outputMessage;

    size_t requiredBytes = sizeof(multi_crate::ParsedDataEventHeader);

    for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        auto &moduleData = moduleDataList[moduleIndex];
        requiredBytes += sizeof(multi_crate::ParsedModuleHeader);
        requiredBytes += moduleData.data.size * sizeof(u32);
    }

    size_t bytesFree =  msg ? nng::allocated_free_space(msg) : 0u;

    if (msg && bytesFree < requiredBytes)
    {
        if (!flush_output_message(ctx, "parser_nng_eventdata"))
            return;
    }

    if (!msg && !parser_maybe_alloc_output(ctx))
        return;

    bytesFree =  msg ? nng::allocated_free_space(msg) : 0u;
    assert(bytesFree >= requiredBytes);

    multi_crate::ParsedDataEventHeader eventHeader{};
    eventHeader.magicByte = multi_crate::ParsedDataEventMagic;
    eventHeader.crateIndex = crateIndex;
    eventHeader.eventIndex = eventIndex;
    eventHeader.moduleCount = moduleCount;

    if (int res = nng_msg_append(msg, &eventHeader, sizeof(eventHeader)))
    {
        spdlog::error("parser_nng_eventdata: nng_msg_append: {}", nng_strerror(res));
        return;
    }

    for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        auto &moduleData = moduleDataList[moduleIndex];

        multi_crate::ParsedModuleHeader moduleHeader = {};
        moduleHeader.prefixSize = moduleData.prefixSize;
        moduleHeader.dynamicSize = moduleData.dynamicSize;
        moduleHeader.suffixSize = moduleData.suffixSize;

        if (int res = nng_msg_append(msg, &moduleHeader, sizeof(moduleHeader)))
        {
            spdlog::error("parser_nng_eventdata: nng_msg_append: {}", nng_strerror(res));
            return;
        }

        if (int res = nng_msg_append(msg, moduleData.data.data, moduleData.data.size * sizeof(u32)))
        {
            spdlog::error("parser_nng_eventdata: nng_msg_append: {}", nng_strerror(res));
            return;
        }
    }
}

void parser_nng_systemevent(void *ctx_, int crateIndex, const u32 *header, u32 size)
{
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());
    auto &ctx = *reinterpret_cast<ReadoutParserNngContext *>(ctx_);
    ++ctx.totalSystemEvents;
    auto &msg = ctx.outputMessage;

    size_t requiredBytes = sizeof(multi_crate::ParsedSystemEventHeader) + size * sizeof(u32);
    size_t bytesFree =  msg ? nng::allocated_free_space(msg) : 0u;

    if (msg && bytesFree < requiredBytes)
    {
        if (!flush_output_message(ctx, "parser_nng_systemevent"))
            return;
    }

    if (!msg && !parser_maybe_alloc_output(ctx))
        return;

    bytesFree =  msg ? nng::allocated_free_space(msg) : 0u;
    assert(bytesFree >= requiredBytes);

    multi_crate::ParsedSystemEventHeader eventHeader{};
    eventHeader.magicByte = multi_crate::ParsedSystemEventMagic;
    eventHeader.crateIndex = crateIndex;
    eventHeader.eventSize = size;

    if (int res = nng_msg_append(msg, &eventHeader, sizeof(eventHeader)))
    {
        spdlog::error("parser_nng_systemevent: nng_msg_append: {}", nng_strerror(res));
        return;
    }

    if (int res = nng_msg_append(msg, header, size * sizeof(u32)))
    {
        spdlog::error("parser_nng_systemevent: nng_msg_append: {}", nng_strerror(res));
        return;
    }
}

// TODO: move context creation out of this processing loop. Initial work and
// error checking does not need to happen in the processing threads.
void readout_parser_loop(
    nng_socket inputSocket,
    nng_socket outputSocket,
    const mvlc::CrateConfig &crateConfig,
    std::atomic<bool> &quit
    )
{
    prctl(PR_SET_NAME,"readout_parser_loop",0,0,0);

    spdlog::info("entering readout_parser_loop, crateId={}", crateConfig.crateId);

    size_t totalInputBytes = 0u;
    u32 lastInputMessageNumber = 0u;
    size_t inputBuffersLost = 0;
    const auto listfileFormat = crateConfig.connectionType;

    std::string stacksYaml;
    for (const auto &stack: crateConfig.stacks)
        stacksYaml += to_yaml(stack);

    spdlog::info("readout_parser_loop (crateId={}): readout stacks:\n{}", crateConfig.crateId, stacksYaml);

    ReadoutParserNngContext parserContext;
    parserContext.outputSocket = outputSocket;
    auto parserState = mvlc::readout_parser::make_readout_parser(
        crateConfig.stacks, crateConfig.crateId, &parserContext);
    mvlc::readout_parser::ReadoutParserCounters parserCounters = {};
    mvlc::readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        parser_nng_eventdata,
        parser_nng_systemevent,
    };

    nng_msg *inputMsg = nullptr;
    u32 &outputMessageNumber = parserContext.outputMessageNumber;
    std::chrono::microseconds tReceive(0);
    std::chrono::microseconds tProcess(0);
    std::chrono::microseconds tSend(0);
    std::chrono::microseconds tTotal(0);
    auto tLastReport = std::chrono::steady_clock::now();
    const auto tStart = std::chrono::steady_clock::now();

    auto log_stats = [&]
    {
        spdlog::info("readout_parser_loop (crateId={}): lastInputMessageNumber={}, inputBuffersLost={}, totalInput={:.2f} MiB",
            crateConfig.crateId, lastInputMessageNumber, inputBuffersLost, 1.0 * totalInputBytes / mvlc::util::Megabytes(1));
        spdlog::info("readout_parser_loop (crateId={}): time budget: "
                    "  tReceive = {} ms, "
                    "  tProcess = {} ms, "
                    "  tSend = {} ms, "
                    "  tTotal = {} ms",
                    crateConfig.crateId,
                    tReceive.count() / 1000.0,
                    tProcess.count() / 1000.0,
                    tSend.count() / 1000.0,
                    tTotal.count() / 1000.0);

        auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tStart);
        auto totalBytes = parserCounters.bytesProcessed;
        auto totalMiB = totalBytes / (1024.0*1024.0);
        //auto bytesPerSecond = 1.0 * totalBytes / totalElapsed.count();
        auto MiBperSecond = totalMiB / totalElapsed.count() * 1000.0;
        spdlog::info("readout_parser_loop (crateId={}): outputMessages={}, bytesProcessed={:.2f} MiB, rate={:.2f} MiB/s",
            crateConfig.crateId, outputMessageNumber, totalMiB, MiBperSecond);
    };

    auto crateId = crateConfig.crateId;

    while (!quit && !parserContext.outputError)
    {
        StopWatch stopWatch;
        stopWatch.start();
        if (auto res = nng::receive_message(inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::error("readout_parser_loop (crateId={}) - receive_message: {}",
                    crateId, nng_strerror(res));
                return;
            }
            spdlog::trace("readout_parser_loop (crateId={}) - receive_message: timeout", crateId);
        }
        else if (nng_msg_len(inputMsg) < sizeof(multi_crate::ReadoutDataMessageHeader))
        {
            if (nng_msg_len(inputMsg) == 0)
                break;

            spdlog::warn("readout_parser_loop (crateId={}) - incoming message too short (len={})",
                crateId, nng_msg_len(inputMsg));
        }
        else
        {
            tReceive += stopWatch.interval();
            totalInputBytes += nng_msg_len(inputMsg);
            auto inputHeader = *reinterpret_cast<const multi_crate::ReadoutDataMessageHeader *>(
                nng_msg_body(inputMsg));
            auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastInputMessageNumber);
            inputBuffersLost += bufferLoss >= 0 ? bufferLoss : 0u;;
            lastInputMessageNumber = inputHeader.messageNumber;
            spdlog::debug("readout_parser_loop (crateId={}): received message {} of size {}",
                crateId, lastInputMessageNumber, nng_msg_len(inputMsg));

            nng_msg_trim(inputMsg, sizeof(multi_crate::ReadoutDataMessageHeader));
            auto inputData = reinterpret_cast<const u32 *>(nng_msg_body(inputMsg));
            size_t inputLen = nng_msg_len(inputMsg) / sizeof(u32);

            readout_parser::parse_readout_buffer(
                listfileFormat,
                parserState,
                parserCallbacks,
                parserCounters,
                inputHeader.messageNumber,
                inputData,
                inputLen);

            tProcess += stopWatch.interval();

            // TODO: also flush after a certain time
            tTotal += stopWatch.end();
        }

        if (inputMsg)
        {
            nng_msg_free(inputMsg);
            inputMsg = nullptr;
        }

        {
            auto now = std::chrono::steady_clock::now();
            auto tReportElapsed = now - tLastReport;
            static const auto ReportInterval = std::chrono::seconds(1);

            if (tReportElapsed >= ReportInterval)
            {
                log_stats();
                tLastReport = now;
            }
        }
    }

    if (parserContext.outputMessage)
        flush_output_message(parserContext, "readout_parser_loop");

    if (inputMsg)
    {
        nng_msg_free(inputMsg);
        inputMsg = nullptr;
    }

    assert(!parserContext.outputMessage);

    // send empty message
    if (auto res = nng_msg_alloc(&parserContext.outputMessage, 0))
    {
        spdlog::error("readout_parser_loop (crateId={}) - nng_msg_alloc: {}", crateId, nng_strerror(res));
        return;
    }

    if (auto res = nng::send_message_retry(outputSocket, parserContext.outputMessage,
        3, "readout_parser_loop"))
    {
        nng_msg_free(parserContext.outputMessage);
        parserContext.outputMessage = nullptr;
        spdlog::error("readout_parser_loop (crateId={}): send_message_retry: {}:", crateId, nng_strerror(res));
        return;
    }

    log_stats();

    {
        std::ostringstream ss;
        readout_parser::print_counters(ss, parserCounters);
        spdlog::info("readout_parser_loop (crateId={}): parser counters:\n{}", crateId, ss.str());
    }

    spdlog::info("leaving readout_parser_loop, crateId={}", crateId);
}

// EventBuilder - this can be split up: N threads can call
// recordEventData()/recordSystemEvent() while one thread repeatedly calls
// buildEvents().

struct EventBuilderContext
{
    std::atomic<bool> &quit;
    nng_socket inputSocket;
    nng_socket outputSocket;
    nng_socket snoopOutputSocket;
    mvlc::EventBuilder eventBuilder;
};

// Calls recordEventData and recordSystemEvent with data read from inputSocket.
// on the event builder. Event builder output is written to the outputSocket and
// duplicated on the snoop output.
void event_builder_loop(EventBuilderContext &context)
{
    prctl(PR_SET_NAME,"event_builder_loop",0,0,0);

    spdlog::info("Entering event_builder_loop");

    while (!context.quit)
    {
        nng_msg *inputMsg = nullptr;
        if (auto res = nng::receive_message(context.inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::warn("event_builder_loop: Error reading from input socket: {}", nng_strerror(res));
            }
            continue;
        }

        assert(inputMsg != nullptr);

        if (nng_msg_len(inputMsg) < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            spdlog::warn("event_builder_loop: Incoming message is too short!");
            // TODO: count this error (should not happen)
            nng_msg_free(inputMsg);
            continue;
        }

        // TODO: actually do some work here.
    }

    spdlog::info("Leaving event_builder_loop");
}

void analysis_loop(
    nng_socket inputSocket,
    std::atomic<bool> &quit
    )
{
    prctl(PR_SET_NAME,"analysis_loop",0,0,0);
    nng_msg *inputMsg = nullptr;
    size_t totalInputBytes = 0u;
    u32 lastInputMessageNumber = 0u;
    size_t inputBuffersLost = 0;
    bool error = false;

    spdlog::info("entering analysis_loop");

    while (!error && !quit)
    {
        if (auto res = nng::receive_message(inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::error("analysis_nng - receive_message: {}", nng_strerror(res));
                return;
            }
        }
        else if (nng_msg_len(inputMsg) < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            if (nng_msg_len(inputMsg) == 0)
                break;

            spdlog::warn("analysis_nng - incoming message too short (len={})",
                nng_msg_len(inputMsg));
        }
        else
        {
            totalInputBytes += nng_msg_len(inputMsg);
            auto inputHeader = nng::msg_trim_read<multi_crate::ParsedEventsMessageHeader>(inputMsg).value();
            auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastInputMessageNumber);
            inputBuffersLost += bufferLoss >= 0 ? bufferLoss : 0u;;
            lastInputMessageNumber = inputHeader.messageNumber;
            if (inputHeader.messageType != multi_crate::MessageType::ParsedEvents)
            {
                spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                    static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ParsedEvents));
                    break;
            }
            spdlog::debug("analysis_nng: received message {} of size {}", lastInputMessageNumber, nng_msg_len(inputMsg));

            while (true)
            {
                if (nng_msg_len(inputMsg) < 1)
                    break;

                const u8 eventMagic = *reinterpret_cast<u8 *>(nng_msg_body(inputMsg));

                if (eventMagic == multi_crate::ParsedDataEventMagic)
                {
                    auto eventHeader = nng::msg_trim_read<multi_crate::ParsedDataEventHeader>(inputMsg);
                    if (!eventHeader)
                        break;

                    for (size_t moduleIndex=0u; moduleIndex<eventHeader->moduleCount; ++moduleIndex)
                    {
                        auto moduleHeader = nng::msg_trim_read<multi_crate::ParsedModuleHeader>(inputMsg);
                        if (!moduleHeader)
                            break;

                        if (moduleHeader->totalBytes())
                        {
                            const u32 *moduleData = reinterpret_cast<const u32 *>(nng_msg_body(inputMsg));

                            mvlc::util::log_buffer(std::cout, moduleData, moduleHeader->totalSize(), fmt::format("crate={}, event={}, module={}, size={}",
                                eventHeader->crateIndex, eventHeader->eventIndex, moduleIndex, moduleHeader->totalSize()));

                            if (nng_msg_trim(inputMsg, moduleHeader->totalBytes()))
                                break;
                        }
                    }
                }
                else if (eventMagic == multi_crate::ParsedSystemEventMagic)
                {
                    auto eventHeader = nng::msg_trim_read<multi_crate::ParsedSystemEventHeader>(inputMsg);
                    if (!eventHeader)
                        break;
                    if (nng_msg_trim(inputMsg, eventHeader->totalBytes()))
                        break;
                }
                else
                {
                    spdlog::warn("analysis_loop: incoming message contains unknown subsection '{}'", eventMagic);
                    break;
                }
            }

            assert(nng_msg_len(inputMsg) == 0);

            nng_msg_free(inputMsg);
            inputMsg = nullptr;
        }
    }

    spdlog::info("analysis_nng: lastInputMessageNumber={}, inputBuffersLost={}, totalInput={:.2f} MiB",
        lastInputMessageNumber, inputBuffersLost, 1.0 * totalInputBytes / mvlc::util::Megabytes(1));
}

int main(int argc, char *argv[])
{
    std::string generalHelp = R"~(
        write the help text please!
)~";

    QApplication app(argc, argv);
    mvme_init("mvme_multicrate_collector", false);
    app.setWindowIcon(QIcon(":/window_icon.png"));

    spdlog::set_level(spdlog::level::warn);
    mesytec::mvlc::set_global_log_level(spdlog::level::warn);

    setup_signal_handlers();

    if (argc < 2)
    {
        std::cout << generalHelp;
        return 1;
    }

    argh::parser parser({"-h", "--help", "--log-level"});
    parser.add_params({"--analysis", "--listfile"});
    parser.parse(argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
            logLevelName = str_tolower(logLevelName);
        else if (parser["--trace"])
            logLevelName = "trace";
        else if (parser["--debug"])
            logLevelName = "debug";
        else if (parser["--info"])
            logLevelName = "info";

        if (!logLevelName.empty())
            spdlog::set_level(spdlog::level::from_str(logLevelName));
    }

    trace_log_parser_info(parser, "mvlc-cli");

    if (parser.pos_args().size() <= 1)
    {
        std::cerr << "Error: no vme configs given on command line\n";
        return 1;
    }

    // Read vme and analysis configs

    std::vector<std::unique_ptr<VMEConfig>> vmeConfigs;
    std::shared_ptr<analysis::Analysis> analysisConfig;

    for (size_t i=1; i<parser.pos_args().size(); ++i)
    {
        auto filename = QString::fromStdString(parser.pos_args().at(i));
        auto [vmeConfig, errorString ] = read_vme_config_from_file(filename);
        if (!vmeConfig)
        {
            std::cerr << fmt::format("Error reading mvme vme config from '{}': {}\n",
                filename.toStdString(), errorString.toStdString());
            return 1;
        }

        vmeConfigs.emplace_back(std::move(vmeConfig));
    }

    std::string str;

    if (parser("--analysis") >> str)
    {
        auto filename = QString::fromStdString(str);
        auto [ana, errorString] = analysis::read_analysis_config_from_file(filename);

        if (!ana)
        {
            std::cerr << fmt::format("Error reading mvme analysis config from '{}': {}\n",
                filename.toStdString(), errorString.toStdString());
            return 1;
        }

        analysisConfig = ana;
    }

    // Create sockets for the mvlc data pipes.
    std::vector<int> mvlcDataSockets;

    for (auto &vmeConfig: vmeConfigs)
    {
        auto settings = vmeConfig->getControllerSettings();
        if (!settings.contains("mvlc_hostname"))
        {
            std::cerr << fmt::format("non ETH mvlc controller found in vme config!\n");
            return 1;
        }

        auto host = settings.value("mvlc_hostname").toString().toStdString();
        auto port = eth::DataPort;
        std::error_code ec;
        auto sock = eth::connect_udp_socket(host, port, &ec);

        if (ec)
        {
            std::cerr << fmt::format("Error connecting to '{}': {}\n", host, ec.message());
            return 1;
        }

        mvlcDataSockets.emplace_back(sock);

        std::cout << fmt::format("Created data pipe socket for MVLC {}\n", host);
    }

    // Redirect the data streams to our sockets
    for (auto sock: mvlcDataSockets)
    {
        static const std::array<u32, 2> EmptyRequest = { 0xF1000000, 0xF2000000 };
        size_t bytesTransferred = 0u;

        if (auto ec = eth::write_to_socket(sock,
            reinterpret_cast<const u8 *>(EmptyRequest.data()),
            EmptyRequest.size() * sizeof(u32),
            bytesTransferred))
        {
            std::cerr << fmt::format("Error redirecting MVLC ETH data stream: {}\n",
                ec.message());
            return 1;
        }
    }

    std::vector<mvlc::CrateConfig> crateConfigs;

    for (size_t i=0; i<vmeConfigs.size(); ++i)
    {
        auto crateConfig = vmeconfig_to_crateconfig(vmeConfigs[i].get());
        crateConfig.crateId = i; // FIXME: vmeconfig_to_crateconfig should set the crateId correctly

        crateConfigs.emplace_back(std::move(crateConfig));
    }

    // All readout threads write messages of type
    // multi_crate::MessageType::ListfileBuffer to the readoutProducerSocket.
    nng_socket readoutProducerSocket = nng::make_push_socket();

    // A single(!) thread reads ListfileBuffer messages from this socket and
    // writes a listfile. Do connect multiple pull sockets to the
    // readoutProducerSocket as that will round-robin distribute the messages!
    nng_socket listfileConsumerSocket = nng::make_pull_socket();

    // TODO: check for socket validity via nng_socket_id() and/or add another
    // way of communicating errors from the socket creation functions.
    if (int res = nng_listen(listfileConsumerSocket, "inproc://readoutData", nullptr, 0))
    {
        nng::mesy_nng_error("nng_listen readoutData", res);
        return 1;
    }

    if (int res = nng_dial(readoutProducerSocket, "inproc://readoutData", nullptr, 0))
    {
        nng::mesy_nng_error("nng_dial readoutData", res);
        return 1;
    }

    // Readout data producers also publish their data on this socket. One snoop
    // output socket per crate.
    std::vector<nng_socket> readoutProducerSnoopSockets;
    // Connected to the respective producer snoop socket. Readout parsers read from one of these.
    std::vector<nng_socket> readoutConsumerInputSockets;

    std::vector<nng_socket> readoutParserOutputSockets;
    std::vector<nng_socket> parsedDataConsumerSockets;

    u16 readoutDataSnoopPort = 42666;

    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto uri = fmt::format("inproc://readoutDataSnoop{}", i);

        nng_socket pubSocket = nng::make_pub_socket();

        if (int res = nng_listen(pubSocket, uri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_listen {}", uri), res);
            return 1;
        }

        auto snoopTcpUri = fmt::format("tcp://*:{}", readoutDataSnoopPort++);

        if (int res = nng_listen(pubSocket, snoopTcpUri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_listen {}", snoopTcpUri), res);
            return 1;
        }

        readoutProducerSnoopSockets.emplace_back(pubSocket);

        nng_socket subSocket = nng::make_sub_socket();

        if (int res = nng_socket_set_string(subSocket, NNG_OPT_SUB_SUBSCRIBE, ""))
        {
            nng::mesy_nng_error("readout consumer socket subscribe", res);
            return 1;
        }

        if (int res = nng_dial(subSocket, uri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_dial {}", uri), res);
            return 1;
        }

        readoutConsumerInputSockets.emplace_back(subSocket);
    }

    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto uri = fmt::format("inproc://parsedData{}", i);

        nng_socket parserOutputSocket = nng::make_pair_socket();

        if (int res = nng_listen(parserOutputSocket, uri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_listen {}", uri), res);
            return 1;
        }

        readoutParserOutputSockets.emplace_back(parserOutputSocket);

        nng_socket analysisInputSocket = nng::make_pair_socket();

        if (int res = nng_dial(analysisInputSocket, uri.c_str(), nullptr, 0))
        {
            nng::mesy_nng_error(fmt::format("nng_dial {}", uri), res);
            return 1;
        }

        parsedDataConsumerSockets.emplace_back(analysisInputSocket);
    }

    std::vector<std::unique_ptr<MvlcEthReadoutLoopContext>> readoutContexts;

    for (size_t i=0; i<mvlcDataSockets.size(); ++i)
    {
        auto ctx = std::make_unique<MvlcEthReadoutLoopContext>();
        ctx->quit = false;
        ctx->mvlcDataSocket = mvlcDataSockets[i];
        ctx->dataOutputSocket = readoutProducerSocket;
        ctx->snoopOutputSocket = readoutProducerSnoopSockets[i];
        ctx->crateId = i;
        readoutContexts.emplace_back(std::move(ctx));
    }

    ListfileWriterContext listfileWriterContext{};
    listfileWriterContext.quit = false;
    listfileWriterContext.dataInputSocket = listfileConsumerSocket;

    // Open the output listfile if one should be written.
    std::unique_ptr<listfile::ZipCreator> listfileCreator;

    if (parser("--listfile") >> str)
    {
        try
        {
            listfileCreator = std::make_unique<listfile::ZipCreator>();
            listfileCreator->createArchive(str);
            listfileWriterContext.lfh = listfileCreator->createZIPEntry("listfile.mvlclst");
            spdlog::info("Opened output listfile {}", str);
            auto &lfh = listfileWriterContext.lfh;
            listfile::listfile_write_magic(*lfh, ConnectionType::ETH);
            listfile::listfile_write_endian_marker(*lfh, 0);

            for (unsigned crateId=0; crateId < vmeConfigs.size(); ++crateId)
            {
                auto crateConfig = vmeconfig_to_crateconfig(vmeConfigs[crateId].get());
                crateConfig.crateId = crateId;
                listfile::listfile_write_crate_config(*lfh, crateConfig);
                mvme_mvlc::listfile_write_mvme_config(*lfh, crateId, *vmeConfigs[crateId]);
            }
        }
        catch(const std::exception& e)
        {
            spdlog::error("Error opening output listfile {}: {}", str, e.what());
            return 1;
        }
    }

    // Thread creation starts here.
    std::thread listfileWriterThread(listfile_writer_loop, std::ref(listfileWriterContext));
    std::vector<std::thread> readoutThreads;
    std::vector<std::thread> parserThreads;
    std::vector<std::thread> analysisThreads;
    std::atomic<bool> quit = false;

    for (auto &readoutContext: readoutContexts)
    {
        std::thread readoutThread(mvlc_eth_readout_loop, std::ref(*readoutContext));
        readoutThreads.emplace_back(std::move(readoutThread));
    }

    assert(readoutConsumerInputSockets.size() == vmeConfigs.size());

    for (size_t i=0; i<readoutConsumerInputSockets.size(); ++i)
    {
        std::thread parserThread(readout_parser_loop,
            readoutConsumerInputSockets[i], readoutParserOutputSockets[i],
            crateConfigs[i], std::ref(quit));

        parserThreads.emplace_back(std::move(parserThread));
    }

    for (size_t i=0; i<parsedDataConsumerSockets.size(); ++i)
    {
        std::thread analysisThread(analysis_loop, parsedDataConsumerSockets[i], std::ref(quit));
        analysisThreads.emplace_back(std::move(analysisThread));
    }

    // Loop until we get interrupted. TODO: tell loops to quit somehow, e.g. by
    // injecting an empty message into the processing chains. This way ensures
    // that each loop processes all its input packets before quitting.
    spdlog::info("entering wait_for_signal loop!");

    #if 1
    do
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (!signal_received);
    #else
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    #endif

    spdlog::info("trying to stop all the things!");


    // TODO: use empty messages to tell downstream consumers to also quit.
    // Inject empty messages into readoutProducerSocket. This way the listfile
    // writer will consume all pending messages before terminating itself.
    for (auto &readoutContext: readoutContexts)
        readoutContext->quit = true;
    listfileWriterContext.quit = true;
    quit = true;

    for (auto &t: readoutThreads)
        if (t.joinable())
            t.join();

    if (listfileWriterThread.joinable())
        listfileWriterThread.join();

    for (auto &t: parserThreads)
        if (t.joinable())
            t.join();

    for (auto &t: analysisThreads)
        if (t.joinable())
            t.join();


    #if 0
    ProcessingContext context{};
    context.quit = false;
    context.vmeConfigs = std::move(vmeConfigs);
    context.dataSockets = mvlcDataSockets;
    context.lfh = std::move(lfh);

    processing_loop(context);

    if (context.lfh)
    {
        spdlog::info("Writing EndRun and EndOfFile sections");
        for (unsigned crateId=0; crateId < vmeConfigs.size(); ++crateId)
        {
            listfile::listfile_write_timestamp_section(
                *context.lfh, crateId, system_event::subtype::EndRun);
        }

        for (unsigned crateId=0; crateId < vmeConfigs.size(); ++crateId)
        {
            listfile_write_system_event(*context.lfh, crateId, system_event::subtype::EndOfFile);
        }
    }
    #endif

    //int ret = app.exec();
    int ret = 0;
    mvme_shutdown();
    return ret;
}
