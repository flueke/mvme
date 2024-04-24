#pragma once

#include "mesy_nng.h"

namespace mesytec::nng
{

struct PipelineElement
{
    nng_socket inputSocket = NNG_SOCKET_INITIALIZER;
    nng_socket outputSocket = NNG_SOCKET_INITIALIZER;
};

std::string make_inproc_url(const char *prefix, int idx)
{
    return fmt::format("inproc://{}{}", prefix, idx);
};


// inputSocket listens, outputSocket dials
int init_inproc_pipeline(std::vector<PipelineElement> &pipeline, const char *prefix)
{
    const auto psize = pipeline.size();

    if (psize == 0)
        return 0;

    // listeners
    for (size_t i=0; i < psize; ++i)
    {
        auto &e = pipeline[i];
        e.inputSocket = make_pair_socket();
        auto listenUrl = make_inproc_url(prefix, i);
        spdlog::info("listen {}", listenUrl);
        if (auto res = nng_listen(e.inputSocket, listenUrl.c_str(), nullptr, 0))
            return res;
    }

    // dialers
    for (size_t i=0; i < psize; ++i)
    {
        auto &e = pipeline[i];
        e.outputSocket = make_pair_socket();
        auto dialUrl = make_inproc_url(prefix, i+1);
        if (i < psize - 1) // do not dial the last output socket
        {
            spdlog::info("dial {}", dialUrl);
            if (auto res = nng_dial(e.outputSocket, dialUrl.c_str(), nullptr, 0))
                return res;
        }
    }

    return 0;
}

int close_pipeline(std::vector<PipelineElement> &pipeline)
{
    for (auto &e: pipeline)
    {
        if (auto res = nng_close(e.inputSocket))
            return res;
        if (auto res = nng_close(e.outputSocket))
            return res;
    }

    return 0;
}

// Appends a new part to the pipe. The inputSocket is setup to listen. If the
// pipe was not empty the previous parts outputSocket is connected to the
// current inputSocket: e_prev.outputSocket -> e_new.inputSocket
// inputSocket listens, outputSocket dials
int pipeline_add_inproc_part(std::vector<PipelineElement> &pipeline, const char *prefix)
{
    spdlog::info("begin pipeline_add_inproc_part");

    PipelineElement e;
    e.inputSocket = make_pair_socket();
    auto listenUrl = make_inproc_url(prefix, pipeline.size());
    spdlog::info("{} this inputSocket listen: {}", prefix, listenUrl);
    if (auto res = nng_listen(e.inputSocket, listenUrl.c_str(), nullptr, 0))
        return res;

    e.outputSocket = make_pair_socket();

    if (!pipeline.empty())
    {
        auto dialUrl = make_inproc_url(prefix, pipeline.size());
        spdlog::info("{} dial previous outputSocket: {}", prefix, dialUrl);
        if (auto res = nng_dial(pipeline.back().outputSocket, dialUrl.c_str(), nullptr, 0))
            return res;
    }

    pipeline.emplace_back(e);
    return 0;
}

int pipe_forwarder(PipelineElement e)
{
    spdlog::info("pipe_forwarder started");
    bool quit = false;

    while (!quit)
    {
        nng_msg *msg = nullptr;

        if (auto res = receive_message(e.inputSocket, &msg, 0))
        {
            if (res != NNG_ETIMEDOUT)
                return res;
            continue;
        }

        spdlog::info("pipe_forwarder received message");

        quit = (nng_msg_len(msg) == 0);

        if (auto res = send_message_retry(e.outputSocket, msg))
        {
            nng_msg_free(msg);
            return res;
        }

        spdlog::info("pipe_forwarder sent message");
    }

    spdlog::info("pipe_forwarder exiting");

    return 0;
}

int pipe_filter_publisher(PipelineElement e, nng_socket pubSocket)
{
    spdlog::info("pipe_filter_publisher started");
    bool quit = false;

    while (!quit)
    {
        nng_msg *msg = nullptr;

        if (auto res = receive_message(e.inputSocket, &msg, 0))
        {
            if (res != NNG_ETIMEDOUT)
                return res;
            continue;
        }

        spdlog::info("pipe_filter_publisher received message");

        quit = (nng_msg_len(msg) == 0);

        nng_msg *pubMsg = nullptr;

        if (auto res = nng_msg_dup(&pubMsg, msg))
        {
            nng_msg_free(msg);
            return res;
        }

        if (auto res = send_message_retry(e.outputSocket, msg))
        {
            nng_msg_free(msg);
            return res;
        }

        if (auto res = send_message_retry(pubSocket, pubMsg))
        {
            nng_msg_free(pubMsg);
            return res;
        }

        spdlog::info("pipe_filter_publisher sent messages");
    }

    spdlog::info("pipe_forwarder exiting");

    return 0;
}

int pipe_end_receiver(PipelineElement e, const char *prefix)
{
    spdlog::info("pipe_end started");

    auto resultUri = fmt::format("inproc://{}_result", prefix);

    auto resultSocket = make_pair_socket();

    if (auto res = nng_listen(resultSocket, resultUri.c_str(), nullptr, 0))
    {
        mesy_nng_error("pipe_end_receiver nng_listen(resultSocket)", res);
        return res;
    }

    if (auto res = nng_dial(e.outputSocket, resultUri.c_str(), nullptr, 0))
    {
        mesy_nng_error("pipe_end_receiver nng_dial(pipe0.output)", res);
        return res;
    }

    bool quit = false;

    while (!quit)
    {
        nng_msg *msg = nullptr;

        if (auto res = receive_message(resultSocket, &msg, 0))
        {
            if (res != NNG_ETIMEDOUT)
                return res;
            continue;
        }

        spdlog::info("pipe_end received message");

        quit = (nng_msg_len(msg) == 0);

        if (!quit)
        {
            uint32_t value = 0u;
            nng_msg_trim_u32(msg, &value);
            spdlog::info("received value=0x{:x}", value);
        }
        else
            spdlog::info("pipe_end received empty message -> quit");
    }

    spdlog::info("pipe_end exiting");
    if (auto res = nng_close(resultSocket))
    {
        mesy_nng_error("pipe_end_receiver nng_close", res);
        return res;
    }

    return 0;
}

#if 0
int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::trace);

    #if 0
    std::vector<PipelineElement> pipe0(10);

    if (auto res = init_inproc_pipeline(pipe0, "pipe0_"))
        mesy_nng_fatal("main init_inproc_pipeline", res);

    auto resultSocket = make_pair_socket();
    if (auto res = nng_listen(resultSocket, "inproc://pipe0_result", nullptr, 0))
        mesy_nng_fatal("main nng_listen(resultSocket)", res);

    if (auto res = nng_dial(pipe0.back().outputSocket, "inproc://pipe0_result", nullptr, 0))
        mesy_nng_fatal("main nng_dial(pipe0.output)", res);

    std::vector<std::future<int>> pipelineJobs;

    // first element does not get a job: +1
    for (auto pipe_it = std::begin(pipe0) + 1; pipe_it != std::end(pipe0); ++pipe_it)
    {
        pipelineJobs.emplace_back(std::async(std::launch::async, pipe_forwarder, *pipe_it));
    }
    #else
    std::vector<PipelineElement> pipe0;
    std::vector<std::future<int>> pipelineJobs;

    for (int i=0; i<2; ++i)
    {
        if (auto res = pipeline_add_inproc_part(pipe0, "pipe0"))
            mesy_nng_fatal("pipeline_add_inproc_part", res);
        if (i > 0) // first pipe element is fed from main
            pipelineJobs.emplace_back(std::async(std::launch::async, pipe_forwarder, pipe0.back()));
    }

    #if 1
    nng_socket pipe0PubSocket;

    if (auto res = nng_pub0_open(&pipe0PubSocket))
        mesy_nng_fatal("nng_pub0_open", res);

    if (auto res = nng_listen(pipe0PubSocket, "inproc://pipe0_pub", nullptr, 0))
        mesy_nng_fatal("nng_listen pipe0_pub", res);

    if (auto res = pipeline_add_inproc_part(pipe0, "pipe0"))
        mesy_nng_fatal("pipeline_add_inproc_part", res);

    pipelineJobs.emplace_back(std::async(std::launch::async, pipe_filter_publisher, pipe0.back(), pipe0PubSocket));
    #endif

    for (int i=0; i<2; ++i)
    {
        if (auto res = pipeline_add_inproc_part(pipe0, "pipe0"))
            mesy_nng_fatal("pipeline_add_inproc_part", res);
        pipelineJobs.emplace_back(std::async(std::launch::async, pipe_forwarder, pipe0.back()));
    }

    pipelineJobs.emplace_back(std::async(std::launch::async, pipe_end_receiver, pipe0.back(), "pipe0"));

    #if 0
    const char *pipe0ResultUri = "inproc://pipe0_result";

    if (auto res = nng_listen(pipe0.back().outputSocket, pipe0ResultUri, nullptr, 0))
        mesy_nng_fatal("main pipeline_end_inproc", res);

    auto resultSocket = make_pair_socket();

    if (auto res = nng_dial(resultSocket, pipe0ResultUri, nullptr, 0))
        mesy_nng_fatal("main nng_dial(resultSocket)", res);
    #endif
    #endif

    nng_msg *outMsg = nullptr;
    if (auto res = nng_msg_alloc(&outMsg, 0))
        mesy_nng_fatal("main msg alloc", res);
    if (auto res = nng_msg_append_u32(outMsg, 0xdeadbeef))
        mesy_nng_fatal("main msg append", res);

    if (auto res = send_message_retry(pipe0.front().outputSocket, outMsg, 0, "main input"))
        mesy_nng_fatal("main send_message_retry", res);

    outMsg = nullptr;

    spdlog::info("main sent first message");

    if (auto res = nng_msg_alloc(&outMsg, 0))
        mesy_nng_fatal("main msg alloc", res);

    if (auto res = send_message_retry(pipe0.front().outputSocket, outMsg, 0, "main input"))
        mesy_nng_fatal("main send_message_retry", res);

    outMsg = nullptr;

    spdlog::info("main sent quit message");

    #if 0
    bool quit = false;
    size_t timeouts = 0u;
    while (!quit)
    {
        nng_msg *msg = nullptr;

        if (auto res = receive_message(resultSocket, &msg, 0))
        {
            if (res != NNG_ETIMEDOUT)
            {
                mesy_nng_fatal("main receive_message", res);
                return res;
            }

            // max wait time until the pipeline is considered dead
            if (++timeouts >= 10)
            {
                spdlog::error("no result from pipeline, assuming it's stuck");
                break;
            }

            continue;
        }

        quit = (nng_msg_len(msg) == 0);

        if (!quit)
        {
            uint32_t value = 0u;
            nng_msg_trim_u32(msg, &value);
            spdlog::info("received value=0x{:x}", value);
        }
        else
            spdlog::info("main received empty message -> quit");
    }

    spdlog::info("left main loop");
    #endif

    int ret = 0;

    for (auto &f: pipelineJobs)
    {
        try
        {
            if (auto res = f.get())
            {
                spdlog::warn("Error from pipeline job: {}", nng_strerror(res));
                ret = 1;
            }
        }
        catch(const std::exception& e)
        {
                spdlog::warn("Exception from pipeline job: {}", e.what());
                ret = 1;
        }
    }

    if (auto res = close_pipeline(pipe0))
        mesy_nng_fatal("main close_pipeline", res);

    return ret;
}
#endif

}
