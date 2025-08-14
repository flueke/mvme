#include "mvme_tcp_stream_server.h"

#include <cassert>

#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/protected.h>
#include <util/mesy_nng.h>

#include "mvme_workspace.h"
#include "util/qt_str.h"


namespace mesytec::mvme
{

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

struct ServerContext
{
    nng_stream_listener *listener = nullptr;
    nng_aio *accept_aio = nullptr;
    std::vector<std::unique_ptr<ClientConnection>> clients;
    std::mutex clients_mutex;
    std::atomic<bool> shutdown{false};

    ServerContext()
    {
    }

    ~ServerContext()
    {
        if (accept_aio)
        {
            nng_aio_free(accept_aio);
        }
        if (listener)
        {
            nng_stream_listener_free(listener);
        }
    }
};

void accept_cb(void *arg);

void start_accept(ServerContext *ctx)
{
    if (ctx->shutdown)
        return;

    if (ctx->accept_aio == nullptr)
    {
        int rv = nng_aio_alloc(&ctx->accept_aio, accept_cb, ctx);
        if (rv != 0)
        {
            spdlog::error("Failed to allocate accept AIO: {}", nng_strerror(rv));
            return;
        }
    }

    nng_aio_set_timeout(ctx->accept_aio, 100);
    nng_stream_listener_accept(ctx->listener, ctx->accept_aio);
}

void accept_cb(void *arg)
{
    auto ctx = static_cast<ServerContext *>(arg);

    if (ctx->shutdown)
        return;

    int rv = nng_aio_result(ctx->accept_aio);
    if (rv != 0)
    {
        if (rv != NNG_ETIMEDOUT)
        {
            spdlog::error("Accept failed: {}, restarting", nng_strerror(rv));
        }
        start_accept(ctx);
        return;
    }

    // Retrieve the nng stream object from the aio
    nng_stream *stream = static_cast<nng_stream *>(nng_aio_get_output(ctx->accept_aio, 0));
    if (stream == nullptr)
    {
        spdlog::error("Accepted null stream");
        start_accept(ctx);
        return;
    }

    try
    {
        auto client = std::make_unique<ClientConnection>(stream);

        // Add to client list
        {
            auto addrStr = client->remoteAddressString();
            std::lock_guard<std::mutex> lock(ctx->clients_mutex);
            ctx->clients.emplace_back(std::move(client));
            spdlog::info("Accepted new connection from {}", addrStr);
        }
    } catch (const nng::exception &e)
    {
        spdlog::warn("Failed to handle new connection: {}", e.what());
    }

    // Continue accepting
    start_accept(ctx);
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

struct MvmeTcpStreamServer::Private
{
    bool enabled_ = false;
    std::string listenUri_;
    std::shared_ptr<spdlog::logger> logger_;
    StreamConsumerBase::Logger mvmeLogger_;
    ServerContext serverContext_;
};

const char *MvmeTcpStreamServer::DefaultListenUri = "tcp://*:42333";

MvmeTcpStreamServer::MvmeTcpStreamServer(const std::string &listenUri)
    : IStreamBufferConsumer()
    , d(std::make_unique<Private>())
{
    d->logger_ = mvlc::get_logger("mvme_tcp_stream_server");
    d->listenUri_ = listenUri;
}

MvmeTcpStreamServer::~MvmeTcpStreamServer()
{
    d->serverContext_.shutdown = true;
}

void MvmeTcpStreamServer::startup()
{
    auto &ctx = d->serverContext_;

    if (!ctx.listener)
    {

        // Setup listener
        int rv = nng_stream_listener_alloc(&ctx.listener, d->listenUri_.c_str());
        if (rv != 0)
        {
            spdlog::error("Failed to allocate listener: {}", nng_strerror(rv));
            return;
        }

        rv = nng_stream_listener_listen(ctx.listener);
        if (rv != 0)
        {
            spdlog::error("Failed to listen: {}", nng_strerror(rv));
            return;
        }

        // Display local address
        nng_sockaddr local_addr{};
        if (nng_stream_listener_get_addr(ctx.listener, NNG_OPT_LOCADDR, &local_addr) == 0)
        {
            spdlog::info("Listening on {}", nng::nng_sockaddr_to_string(local_addr));
        }
    }

    ctx.shutdown = false;

    if (!ctx.accept_aio || !nng_aio_busy(ctx.accept_aio))
    {
        start_accept(&ctx);
    }
}

void MvmeTcpStreamServer::shutdown()
{
    auto &ctx = d->serverContext_;

    if (ctx.accept_aio)
    {
        spdlog::info("Shutting down server");
        ctx.shutdown = true;

        nng_aio_cancel(ctx.accept_aio);
        nng_aio_wait(ctx.accept_aio);
    }
}

void MvmeTcpStreamServer::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig,
                                   const analysis::Analysis *analysis)
{
    Q_UNUSED(runInfo);
    Q_UNUSED(vmeConfig);
    Q_UNUSED(analysis);
}

void MvmeTcpStreamServer::endRun(const DAQStats &stats, const std::exception *e)
{
    Q_UNUSED(stats);
    Q_UNUSED(e);
}

void MvmeTcpStreamServer::processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer,
                                        size_t bufferSize)
{
    Q_UNUSED(bufferType);
    assert(bufferSize <= std::numeric_limits<u32>::max());
    auto &ctx = d->serverContext_;
    send_to_all_clients(&ctx, bufferNumber, buffer, bufferSize);
}

void MvmeTcpStreamServer::setLogger(StreamConsumerBase::Logger logger)
{
    d->mvmeLogger_ = logger;
}

StreamConsumerBase::Logger &MvmeTcpStreamServer::getLogger()
{
    return d->mvmeLogger_;
}

#if 0
void MvmeTcpStreamServer::setListenUri(const std::string &listenUri)
{
    d->listenUri_ = listenUri;
    shutdown();
    startup();
}

std::string MvmeTcpStreamServer::getListenUri() const
{
    return d->listenUri_;
}
#endif

void MvmeTcpStreamServer::reloadConfiguration()
{
    auto settings = make_workspace_settings();

    d->enabled_ = settings.value(QSL("TcpStreamServer/Enabled"), false).toBool();

    if (!d->enabled_)
    {
        logMessage(QSL("TcpStreamServer is disabled, shutting down"));
        shutdown();
        return;
    }

    std::string listenUri = settings.value(QSL("TcpStreamServer/ListenUri"), "tcp://*:42333").toString().toStdString();

    if (listenUri != d->listenUri_)
    {
        logMessage(QSL("TcpStreamServer attempting to listen on %1").arg(listenUri.c_str()));
        d->listenUri_ = listenUri;
        shutdown();
        startup();
    }

    #if 0
    // This wants to run in the event servers thread.
    bool invoked = QMetaObject::invokeMethod(this,
                                                "setEnabled",
                                                Qt::QueuedConnection,
                                                Q_ARG(bool, enabled));
    assert(invoked);
    (void) invoked;
    #endif
}

} // namespace mesytec::mvme
