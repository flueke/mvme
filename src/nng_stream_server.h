#ifndef AAF8B2BA_8FEA_4E1A_9454_254B4216F6D4
#define AAF8B2BA_8FEA_4E1A_9454_254B4216F6D4

// Streaming server implementation for MVME/mesytec-mvlc using NNG to handle networking.
// Supports tcp://, ipc://, inproc://, tcp4:// and tcp6:// URIs.
// The acceptor runs asychronously in the background. No thread creation needed.
// Message format is: u32 bufferNumber, u32 bufferSize, u32 data[bufferSize].
// Endianess is left as is.
//
// Usage:
// Create a NngStreamServer instance, call start() with a list of URIs to
// listen on. Use stop() to stop the server, isRunning() to query the state.
// Use send_to_all_clients() to do a blocking send to all connected clients.
// This will internally queue up async sends, then wait for them to complete
// before returning.

#include <util/mesy_nng.h>
#include "libmvme_export.h"

namespace mesytec::nng
{

struct LIBMVME_EXPORT ClientConnection
{
    nng_stream *stream = nullptr;
    nng_aio *aio = nullptr;

    explicit ClientConnection(nng_stream *s);
    ~ClientConnection();
    nng_sockaddr remoteAddress() const;
    std::string remoteAddressString() const;
};

struct LIBMVME_EXPORT NngStreamServer;

struct LIBMVME_EXPORT Acceptor
{
    NngStreamServer *ctx = nullptr;
    nng_stream_listener *listener = nullptr;
    nng_aio *accept_aio = nullptr;
};

struct LIBMVME_EXPORT NngStreamServer
{
    std::vector<Acceptor> acceptors;
    std::vector<std::unique_ptr<ClientConnection>> clients;
    std::mutex clients_mutex;
    std::atomic<bool> shutdown{true};

    // Start listening on the given URIs. Returns false if listening on any URI
    // failed or the server was already running.. You still have to call stop()
    // in case the server listens on some of the URIs.
    bool start(const std::vector<std::string> &listenUris);

    void stop();

    bool isRunning() const { return !shutdown; }

    // Stops the server and releases allocated resources.
    ~NngStreamServer();
};

// Send data to all clients in a blocking fashion.
// Message format is: u32 bufferNumber, u32 bufferSize, u32 data[bufferSize].
// Clients won't receive partial data, only complete messages.
// The senders network byte order is used, no reordering is done.
bool LIBMVME_EXPORT send_to_all_clients(NngStreamServer *ctx, u32 bufferNumber, const u32 *data, u32 bufferElements);

}

#endif /* AAF8B2BA_8FEA_4E1A_9454_254B4216F6D4 */
