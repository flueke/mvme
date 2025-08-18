#include "nng_stream_server.h"

namespace mesytec::nng
{

void accept_cb(void *arg);
void start_accept(Acceptor *acceptor);

ClientConnection::ClientConnection(nng_stream *s)
    : stream(s)
    , aio(nullptr)
{
    if (int rv = nng_aio_alloc(&aio, nullptr, nullptr))
    {
        spdlog::error("Failed to allocate client AIO: {}", nng_strerror(rv));
        throw nng::exception(rv);
    }
}

ClientConnection::~ClientConnection()
{
    if (aio)
        nng_aio_free(aio);

    if (stream)
        nng_stream_free(stream);
}

nng_sockaddr ClientConnection::remoteAddress() const
{
    nng_sockaddr addr{};
    nng_stream_get_addr(stream, NNG_OPT_REMADDR, &addr);
    return addr;
}

std::string ClientConnection::remoteAddressString() const
{
    return nng::nng_sockaddr_to_string(remoteAddress());
}

bool NngStreamServer::start(const std::vector<std::string> &listenUris)
{
    if (!shutdown)
    {
        spdlog::warn("ServerContext::start(): Server is already running");
        return false;
    }

    shutdown = false;
    spdlog::info("ServerContext::start(): Starting server");

    // Start listening on all configured URIs
    bool ret = true;

    for (const auto &uri: listenUris)
    {
        Acceptor acceptor;
        acceptor.ctx = this;

        if (int rv = nng_stream_listener_alloc(&acceptor.listener, uri.c_str()))
        {
            spdlog::error("Failed to allocate listener for {}: {}", uri, nng_strerror(rv));
            ret = false;
            continue;
        }

        if (int rv = nng_stream_listener_listen(acceptor.listener))
        {
            spdlog::error("Failed to listen on {}: {}", uri, nng_strerror(rv));
            nng_stream_listener_free(acceptor.listener);
            ret = false;
            continue;
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

    return ret;
}

void NngStreamServer::stop()
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

    return;
}

NngStreamServer::~NngStreamServer()
{
    if (!shutdown)
        stop();
}

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
    }
    catch (const nng::exception &e)
    {
        spdlog::warn("Failed to handle new connection: {}", e.what());
    }

    // Continue accepting
    start_accept(acceptor);
}

// Send data to all clients in a blocking fashion
bool send_to_all_clients(NngStreamServer *ctx, u32 bufferNumber, const u32 *data,
                         u32 bufferElements)
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
}
