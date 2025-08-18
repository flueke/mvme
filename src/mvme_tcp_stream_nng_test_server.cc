#include <atomic>
#include <mutex>
#include <vector>

#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/protected.h>
#include <mesytec-mvlc/util/signal_handling.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <util/mesy_nng.h>

using namespace mesytec;
using namespace mesytec::mvlc;

struct ClientConnection
{
    nng_stream *stream;
    nng_aio *aio;

    explicit ClientConnection(nng_stream *s)
        : stream(s)
        , aio(nullptr)
    {
        if (int rv = nng_aio_alloc(&aio, nullptr, nullptr))
        {
            spdlog::error("Failed to allocate client AIO: {}", nng_strerror(rv));
            throw nng::exception(rv);
        }
    }

    ~ClientConnection()
    {
        if (aio)
            nng_aio_free(aio);

        if (stream)
            nng_stream_free(stream);
    }

    nng_sockaddr remoteAddress() const
    {
        nng_sockaddr addr{};
        nng_stream_get_addr(stream, NNG_OPT_REMADDR, &addr);
        return addr;
    }

    std::string remoteAddressString() const { return nng::nng_sockaddr_to_string(remoteAddress()); }
};

struct ServerContext;

struct Acceptor
{
    ServerContext *ctx = nullptr;
    nng_stream_listener *listener = nullptr;
    nng_aio *accept_aio = nullptr;
};

void accept_cb(void *arg);
void start_accept(Acceptor *acceptor);

struct ServerContext
{
    std::vector<Acceptor> acceptors;
    std::vector<std::unique_ptr<ClientConnection>> clients;
    std::mutex clients_mutex;
    std::atomic<bool> shutdown{true};

    void start(const std::vector<std::string> &listenUris)
    {
        if (!shutdown)
        {
            spdlog::warn("ServerContext::start(): Server is already running");
        }

        shutdown = false;
        spdlog::info("ServerContext::start(): Starting server");

        // Start listening on all configures URIs
        for (const auto &uri: listenUris)
        {
            Acceptor acceptor;
            acceptor.ctx = this;

            if (int rv = nng_stream_listener_alloc(&acceptor.listener, uri.c_str()))
            {
                spdlog::error("Failed to allocate listener for {}: {}", uri, nng_strerror(rv));
                return;
            }

            if (int rv = nng_stream_listener_listen(acceptor.listener))
            {
                spdlog::error("Failed to listen on {}: {}", uri, nng_strerror(rv));
                nng_stream_listener_free(acceptor.listener);
                return;
            }

            nng_sockaddr local_addr{};
            if (nng_stream_listener_get_addr(acceptor.listener, NNG_OPT_LOCADDR, &local_addr) == 0)
            {
                spdlog::info("Listening on {}", nng::nng_sockaddr_to_string(local_addr));
            }

            acceptors.emplace_back(std::move(acceptor));
        }

        for (auto &acceptor: acceptors)
        {
            start_accept(&acceptor);
        }
    }

    void stop()
    {
        if (shutdown)
        {
            spdlog::warn("ServerContext::stop(): Server is already stopped");
            return;
        }

        spdlog::info("ServerContext::stop(): Stopping server");
        shutdown = true;

        for (auto &acceptor: acceptors)
        {
            if (acceptor.accept_aio)
            {
                nng_aio_cancel(acceptor.accept_aio);
                nng_aio_wait(acceptor.accept_aio);
            }
            nng_aio_free(acceptor.accept_aio);
            nng_stream_listener_free(acceptor.listener);
        }
    }

    ~ServerContext()
    {
        if (!shutdown)
            stop();
    }
};

void start_accept(Acceptor *acceptor)
{
    if (acceptor->ctx->shutdown)
        return;

    if (!acceptor->accept_aio)
    {
        if (int rv = nng_aio_alloc(&acceptor->accept_aio, accept_cb, acceptor))
        {
            spdlog::error("Failed to allocate accept AIO: {}", nng_strerror(rv));
            return;
        }
    }

    nng_aio_set_timeout(acceptor->accept_aio, 100);
    nng_stream_listener_accept(acceptor->listener, acceptor->accept_aio);
}

void accept_cb(void *arg)
{
    auto acceptor = static_cast<Acceptor *>(arg);

    if (acceptor->ctx->shutdown)
        return;

    if (int rv = nng_aio_result(acceptor->accept_aio))
    {
        if (rv != NNG_ETIMEDOUT)
        {
            spdlog::error("Accept failed: {}, restarting", nng_strerror(rv));
        }
        start_accept(acceptor);
        return;
    }

    // Retrieve the nng stream object from the aio
    nng_stream *stream = static_cast<nng_stream *>(nng_aio_get_output(acceptor->accept_aio, 0));
    if (!stream)
    {
        spdlog::error("Accepted null stream");
        start_accept(acceptor);
        return;
    }

    try
    {
        auto client = std::make_unique<ClientConnection>(stream);

        // Add to client list
        {
            auto addrStr = client->remoteAddressString();
            std::lock_guard<std::mutex> lock(acceptor->ctx->clients_mutex);
            acceptor->ctx->clients.emplace_back(std::move(client));
            spdlog::info("Accepted new connection from {}", addrStr);
        }
    } catch (const nng::exception &e)
    {
        spdlog::warn("Failed to handle new connection: {}", e.what());
    }

    // Continue accepting
    start_accept(acceptor);
}

// Send data to all clients in a blocking fashion
bool send_to_all_clients(ServerContext *ctx, u32 bufferNumber, const u32 *data, u32 bufferElements)
{
    if (ctx->shutdown)
        return false;

    std::unique_lock<std::mutex> lock(ctx->clients_mutex);
    if (ctx->clients.empty())
    {
        return true; // No clients to send to
    }

    std::array<nng_iov, 3> iovs = {{{&bufferNumber, sizeof(bufferNumber)},
                                    {&bufferElements, sizeof(bufferElements)},
                                    {const_cast<u32 *>(data), bufferElements * sizeof(u32)}}};

    // Setup sends for each client
    for (auto &client: ctx->clients)
    {
        assert(client->stream);
        assert(client->aio);

        int rv = nng_aio_set_iov(client->aio, iovs.size(), iovs.data());
        assert(rv == 0); // will only fail if iov is too large

        nng_stream_send(client->stream, client->aio);
    }

    std::vector<size_t> clientsToRemove;

    for (auto it = ctx->clients.begin(); it != ctx->clients.end(); ++it)
    {
        auto &client = *it;
        auto addrStr = client->remoteAddressString();
        nng_aio_wait(client->aio);
        assert(!nng_aio_busy(client->aio));

        if (int rv = nng_aio_result(client->aio))
        {
            spdlog::warn("Send to failed: {}", nng_strerror(rv));
            clientsToRemove.push_back(std::distance(ctx->clients.begin(), it));
        }
    }

    // reverse sort the indexes so we start removing from the end
    std::sort(std::begin(clientsToRemove), std::end(clientsToRemove), std::greater<size_t>());

    for (auto index: clientsToRemove)
    {
        spdlog::info("Removing client @{}", index);
        ctx->clients.erase(ctx->clients.begin() + index);
    }

    return true;
}

static const std::vector<std::string> listenUris =
{
    "tcp4://*:42333",
    "ipc:///tmp/mvme_tcp_stream_server.ipc",
    "tcp4://*:42334",
};

int main()
{
        {
        mvlc::set_global_log_level(spdlog::level::debug);
        mvlc::util::setup_signal_handlers();

        ServerContext ctx;
        ctx.start(listenUris);

        // Main loop - send data periodically to all clients
        u32 bufferNumber = 0;
        std::vector<u32> data = {1, 2, 3, 4, 5};

        while (!mvlc::util::signal_received())
        {
            // spdlog::info("Main loop iteration, {} clients connected. bufferNumber={}",
            // ctx.clients.size(), bufferNumber);

            if (!send_to_all_clients(&ctx, bufferNumber, data.data(), data.size()))
            {
                spdlog::warn("Failed to send data to all clients");
            }

            bufferNumber++;
            std::for_each(std::begin(data), std::end(data), [](u32 &val) { val++; });

            // simulate daq/replay delay here
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Cleanup
        spdlog::info("Shutting down server");
        #if 0
        ctx.shutdown = true;

        for (auto &acceptor: ctx.acceptors)
        {
            if (acceptor.accept_aio)
            {
                nng_aio_cancel(acceptor.accept_aio);
                nng_aio_wait(acceptor.accept_aio);
            }
            nng_stream_listener_free(acceptor.listener);
            nng_aio_free(acceptor.accept_aio);
        }
        #endif


    }

    spdlog::info("left main scope. ServerContext should be destroyed by now");
    nng_fini(); // Note: don't do this in mvme. It destroys state that NNG needs to operate. Helps valgrind a bit though.
    return 0;
}
