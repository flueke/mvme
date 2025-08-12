//#include <cassert>
//#include <set>
//#include <system_error>

#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/protected.h>
#include <mesytec-mvlc/util/signal_handling.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <util/mesy_nng.h>
#include <set>

using namespace mesytec;
using namespace mesytec::mvlc;

struct ServerContext
{
    nng_stream_listener *listener = nullptr;
    nng_aio *aio = nullptr;
    mvlc::Protected<std::set<nng_stream *>> streams;
};

void listener_callback(void *arg)
{
    auto ctx = reinterpret_cast<ServerContext *>(arg);

    if (!ctx->aio)
    {
        if (int res = nng_aio_alloc(&ctx->aio, listener_callback, ctx))
        {
            spdlog::error("Failed to allocate AIO for listener: {}", nng_strerror(res));
            return;
        }

        nng_stream_listener_accept(ctx->listener, ctx->aio);
    }
    else if (auto stream = reinterpret_cast<nng_stream *>(nng_aio_get_output(ctx->aio, 0)))
    {
        nng_sockaddr addr;

        if (int res = nng_stream_get_addr(stream, NNG_OPT_REMADDR, &addr))
        {
            spdlog::error("Failed to get remote address: {}", nng_strerror(res));
            spdlog::info("Accepted connection from <unknown>");
        }
        else
            spdlog::info("Accepted connection from {}", nng::nng_sockaddr_to_string(addr));

        ctx->streams.access()->insert(stream);
        nng_aio_reap(ctx->aio);
        ctx->aio = nullptr;
        listener_callback(arg); // Accept the next connection
    }
}

int main()
{
    mvlc::set_global_log_level(spdlog::level::debug);
    mvlc::util::setup_signal_handlers();

    ServerContext ctx;

    if (int res = nng_stream_listener_alloc(&ctx.listener, "tcp4://localhost:42333"))
    {
        spdlog::error("Failed to allocate stream listener: {}", nng_strerror(res));
        return res;
    }

    if (int res = nng_stream_listener_listen(ctx.listener))
    {
        spdlog::error("Failed to listen on stream listener: {}", nng_strerror(res));
        nng_stream_listener_free(ctx.listener);
        return res;
    }

    listener_callback(&ctx); // kick of the listener

    while (!mvlc::util::signal_received())
    {
        spdlog::info("main loop");
        std::this_thread::sleep_for(std::chrono::seconds(100));
    }

    spdlog::info("left main loop");

    if (mvlc::util::signal_received())
    {
        nng_aio_cancel(ctx.aio);
    }

    #if 0
    nng_aio *send_aio = nullptr;

    if (int res = nng_aio_alloc(&send_aio, nullptr, nullptr))
    {
        spdlog::error("Failed to allocate AIO for send: {}", nng_strerror(res));
        nng_stream_listener_free(listener);
        nng_aio_free(accept_aio);
        return res;
    }

    std::vector<u32> fakeBuffer = { 1, 2, 3, 4, 5 }; // Example buffer data
    u32 bufferNumber = 0;
    u32 bufferSize = fakeBuffer.size();

    std::array<nng_iov, 3> iov =
    {{
        { &bufferNumber, sizeof(bufferNumber) },
        { &bufferSize, sizeof(bufferSize) },
        { fakeBuffer.data(), fakeBuffer.size() * sizeof(u32) },
    }};

    if (int res = nng_aio_set_iov(send_aio, iov.size(), iov.data()))
    {
        spdlog::error("Failed to set IOV for send: {}", nng_strerror(res));
        return res;
    }

    nng_stream_send(stream, send_aio);
    nng_aio_wait(send_aio);
    #endif
}
