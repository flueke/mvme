#include "mvlc/mvlc_impl_udp.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>

#ifndef __WIN32
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <ws2tcpip.h>
#include <stdio.h>
#include <fcntl.h>
#endif

#include "mvlc/mvlc_error.h"

#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#define LOG_LEVEL_SETTING LOG_LEVEL_TRACE

#define DO_LOG(level, prefix, fmt, ...)\
do\
{\
    if (LOG_LEVEL_SETTING >= level)\
    {\
        fprintf(stderr, prefix "%s(): " fmt "\n", __FUNCTION__, ##__VA_ARGS__);\
    }\
} while (0);

#define LOG_WARN(fmt, ...)  DO_LOG(LOG_LEVEL_WARN,  "WARN - mvlc_udp ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DO_LOG(LOG_LEVEL_INFO,  "INFO - mvlc_udp ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - mvlc_udp ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DO_LOG(LOG_LEVEL_TRACE, "TRACE - mvlc_udp ", fmt, ##__VA_ARGS__)

namespace
{


// Does IPv4 host lookup for a UDP socket. On success the resulting struct
// sockaddr_in is copied to dest.
std::error_code lookup(const std::string &host, u16 port, sockaddr_in &dest)
{
    using namespace mesytec::mvlc;

    dest = {};
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *result = nullptr, *rp = nullptr;

    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(),
                         &hints, &result);

    // TODO: check getaddrinfo specific error codes. make and use getaddrinfo error category
    if (rc != 0)
        return make_error_code(MVLCErrorCode::HostLookupError);

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        if (rp->ai_addrlen == sizeof(dest))
        {
            std::memcpy(&dest, rp->ai_addr, rp->ai_addrlen);
            break;
        }
    }

    freeaddrinfo(result);

    if (!rp)
        return make_error_code(MVLCErrorCode::HostLookupError);

    return {};
}

#ifndef __WIN32
std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    unsigned seconds = ms / 1000;
    ms -= seconds * 1000;

    struct timeval tv;
    tv.tv_sec  = seconds;
    tv.tv_usec = ms * 1000;

    int res = setsockopt(sock, SOL_SOCKET, optname, &tv, sizeof(tv));

    if (res != 0)
        return std::error_code(errno, std::system_category());
    return {};
}
#else
std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    DWORD optval = ms;
    int res = setsockopt(sock, SOL_SOCKET, optname,
                         reinterpret_cast<const char *>(optval),
                         sizeof(optval));

    if (res != 0)
        return std::error_code(errno, std::system_category());
    return {};
}
#endif

std::error_code set_socket_write_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_SNDTIMEO, sock, ms);
}

std::error_code set_socket_read_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_RCVTIMEO, sock, ms);
}

const u16 FirstDynamicPort = 49152;
static const int SocketReceiveBufferSize = 1024 * 1024;

} // end anon namespace

namespace mesytec
{
namespace mvlc
{
namespace udp
{

Impl::Impl(const std::string &host)
    : m_host(host)
{
}

Impl::~Impl()
{
    disconnect();
}

// A note about using ::bind() and then ::connect():
//
// Under linux this has the effect of changing the local bound address from
// INADDR_ANY to the address of the interface that's used to reach the remote
// address. E.g. when connecting to localhost the following will happen: after
// the bind() call the local "listen" address will be 0.0.0.0, after the
// connect() call this will change to 127.0.0.1. The local port specified in
// the bind() call will be kept. This is nice.

// Things happening in Impl::connect:
// * Remote host lookup to get the IPv4 address of the MVLC.
// * Create two UDP sockets and bind them to two consecutive local ports.
//   Ports are tried starting from FirstDynamicPort (49152).
// * Use ::connect() on both sockets with the MVLC address and the default
//   command and data ports. This way the sockets will only receive datagrams
//   originating from the MVLC.
// * TODO: Send an initial request and read the response. Preferably this
//   should tells us if another client is currently using the MVLC. It could be
//   some sort of "DAQ mode register" or a way to check where the MVLC is
//   currently sending its data output.
std::error_code Impl::connect()
{
    if (isConnected())
        return make_error_code(MVLCErrorCode::IsConnected);

    m_cmdSock = -1;
    m_dataSock = -1;
    m_stats = {};

    // lookup remote host
    // create and bind two UDP sockets on consecutive local ports
    // => cmd and data sockets
    // send an initial request on the data socket, receive the response or timeout
    // verify the response
    // if ok: set connected and return success
    // else return error

    if (auto ec = lookup(m_host, CommandPort, m_cmdAddr))
        return ec;

    assert(m_cmdAddr.sin_port == htons(CommandPort));

    // Copy address and replace the port with DataPort
    m_dataAddr = m_cmdAddr;
    m_dataAddr.sin_port = htons(DataPort);

    // Lookup succeeded and we have now have two remote addresses, one for the
    // command and one for the data pipe.
    //
    // Now create two IPv4 UDP sockets and try to bind them to two consecutive
    // local ports.
    for (u16 localCmdPort = FirstDynamicPort;
         // Using 'less than' to leave one spare port for the data pipe
         localCmdPort < std::numeric_limits<u16>::max();
         localCmdPort++)
    {
        assert(m_cmdSock < 0 && m_dataSock < 0);

        // Not being able to create the sockets is considered a fatal error.

        m_cmdSock = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_cmdSock < 0)
            return std::error_code(errno, std::system_category());

        m_dataSock = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_dataSock < 0)
        {
            if (m_cmdSock >= 0)
            {
                ::close(m_cmdSock);
                m_cmdSock = -1;
            }
            return std::error_code(errno, std::system_category());
        }

        assert(m_cmdSock >= 0 && m_dataSock >= 0);

        // Setup the local address structures using two consecutive port
        // numbers.
        struct sockaddr_in cmdLocal = {};
        cmdLocal.sin_family = AF_INET;
        cmdLocal.sin_addr.s_addr = INADDR_ANY;
        cmdLocal.sin_port = htons(localCmdPort);

        struct sockaddr_in dataLocal = cmdLocal;
        dataLocal.sin_port = htons(localCmdPort + 1);

        // Bind both sockets. In case of an error close the sockets and
        // continue with the loop.
        if (int res = ::bind(m_cmdSock, reinterpret_cast<struct sockaddr *>(&cmdLocal),
                             sizeof(cmdLocal)))
        {
            goto try_again;
        }

        if (int res = ::bind(m_dataSock, reinterpret_cast<struct sockaddr *>(&dataLocal),
                             sizeof(dataLocal)))
        {
            goto try_again;
        }

        break;

        try_again:
        {
            ::close(m_cmdSock);
            ::close(m_dataSock);
            m_cmdSock = -1;
            m_dataSock = -1;
        }
    }

    if (m_cmdSock < 0 || m_dataSock < 0)
        return make_error_code(MVLCErrorCode::BindLocalError);

    // Call connect on the sockets so that we receive only datagrams from the
    // MVLC.
    if (int res = ::connect(m_cmdSock, reinterpret_cast<struct sockaddr *>(&m_cmdAddr),
                            sizeof(m_cmdAddr)))
    {
        ::close(m_cmdSock);
        ::close(m_dataSock);
        m_cmdSock = -1;
        m_dataSock = -1;
        return std::error_code(errno, std::system_category());
    }

    if (int res = ::connect(m_dataSock, reinterpret_cast<struct sockaddr *>(&m_dataAddr),
                            sizeof(m_dataAddr)))
    {
        ::close(m_cmdSock);
        ::close(m_dataSock);
        m_cmdSock = -1;
        m_dataSock = -1;
        return std::error_code(errno, std::system_category());
    }

    // Set read and write timeouts
    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
        if (auto ec = set_socket_write_timeout(getSocket(pipe), getWriteTimeout(pipe)))
            return ec;

        if (auto ec = set_socket_read_timeout(getSocket(pipe), getReadTimeout(pipe)))
            return ec;
    }

    // Set socket receive buffer size
    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
#ifndef __WIN32
        int res = setsockopt(getSocket(pipe), SOL_SOCKET, SO_RCVBUF,
                             &SocketReceiveBufferSize,
                             sizeof(SocketReceiveBufferSize));
#else
        int res = setsockopt(getSocket(pipe), SOL_SOCKET, SO_RCVBUF,
                             reinterpret_cast<const char *>(&SocketReceiveBufferSize),
                             sizeof(SocketReceiveBufferSize));
#endif
        assert(res == 0);
    }

    // TODO: send some initial request to verify there's an MVLC on the other side
    // Note: this should not interfere with any other active client.

    assert(m_cmdSock >= 0 && m_dataSock >= 0);
    return {};
}

std::error_code Impl::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    ::close(m_cmdSock);
    ::close(m_dataSock);
    m_cmdSock = -1;
    m_dataSock = -1;
    return {};
}

bool Impl::isConnected() const
{
    return m_cmdSock >= 0 && m_dataSock >= 0;
}

std::error_code Impl::setWriteTimeout(Pipe pipe, unsigned ms)
{
    auto p = static_cast<unsigned>(pipe);

    if (p >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    m_writeTimeouts[p] = ms;

    if (isConnected())
        return set_socket_write_timeout(getSocket(pipe), ms);

    return {};
}

std::error_code Impl::setReadTimeout(Pipe pipe, unsigned ms)
{
    auto p = static_cast<unsigned>(pipe);

    if (p >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    m_readTimeouts[p] = ms;

    if (isConnected())
        return set_socket_read_timeout(getSocket(pipe), ms);

    return {};
}

unsigned Impl::getWriteTimeout(Pipe pipe) const
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return 0u;
    return m_writeTimeouts[static_cast<unsigned>(pipe)];
}

unsigned Impl::getReadTimeout(Pipe pipe) const
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return 0u;
    return m_readTimeouts[static_cast<unsigned>(pipe)];
}

// Standard MTU is 1500 bytes
// Jumbos Frames are usually 9000 bytes
// IPv4 header is 20 bytes
// UDP header is 8 bytes
static const size_t MaxOutgoingPayloadSize = 1500 - 20 - 8;
static const size_t MaxIncomingPayloadSIze = MaxOutgoingPayloadSize;
static const size_t JumboMaxIncomingPayloadSize = 9000 - 20 - 8;

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    // Note: it should not be necessary to split this into multiple calls to
    // send() because outgoing MVLC command buffers should be smaller than the
    // maximum ethernet MTU.
    // The send() call should return EMSGSIZE if the payload is too large to be
    // atomically transmitted.
    assert(size <= MaxOutgoingPayloadSize);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    bytesTransferred = 0;

    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    ssize_t res = ::send(getSocket(pipe), reinterpret_cast<const char *>(buffer), size, 0);

    if (res < 0)
        return std::error_code(errno, std::system_category());

    bytesTransferred = res;
    return {};
}


/* initial:
 *   next_header_pointer = 0
 *   packet_number = 0
 *
 *   - receive one packet
 *   - make sure there are two header words
 *   - extract packet_number and number_of_data_words
 *   - record possible packet loss or ordering problems based on packet number
 *   - check to make sure timestamp is incrementing (packet ordering)
 *   -
 */

static inline std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
                                                 size_t &bytesTransferred)
{
    bytesTransferred = 0u;

    ssize_t res = ::recv(sockfd, reinterpret_cast<char *>(dest), size, 0);

    if (res < 0)
        return std::error_code(errno, std::system_category());

    bytesTransferred = res;
    return {};
}

std::error_code Impl::read(Pipe pipe_, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    unsigned pipe = static_cast<unsigned>(pipe_);

    assert(buffer);
    assert(pipe < PipeCount);

    const size_t requestedSize = size;
    bytesTransferred = 0u;

    if (pipe >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    auto &receiveBuffer = m_receiveBuffers[pipe];

    // Copy from receiveBuffer into the dest buffer while updating local
    // variables.
    auto copy_and_update = [&buffer, &size, &bytesTransferred, &receiveBuffer] ()
    {
        if (size_t toCopy = std::min(receiveBuffer.available(), size))
        {
            memcpy(buffer, receiveBuffer.start, toCopy);
            buffer += toCopy;
            size -= toCopy;
            receiveBuffer.start += toCopy;
            bytesTransferred += toCopy;
        }
    };

    LOG_TRACE("+++ pipe=%u, size=%zu, bufferAvail=%zu",
              pipe, requestedSize, receiveBuffer.available());

    copy_and_update();

    if (size == 0)
    {
        LOG_TRACE("pipe=%u, size=%zu, read request satisfied from buffer, new buffer size=%zu",
                  pipe, requestedSize, receiveBuffer.available());
        return {};
    }

    // All data from the read buffer should have been consumed at this point.
    // It's time to issue actual read requests
    assert(receiveBuffer.available() == 0);

    size_t readCount = 0u;

    while (size > 0)
    {
        assert(receiveBuffer.available() == 0);
        receiveBuffer.reset();

        LOG_TRACE("pipe=%u, requestedSize=%zu, remainingSize=%zu, reading from MVLC...",
                  pipe, requestedSize, size);

        size_t transferred = 0;

        auto ec = receive_one_packet(
            getSocket(pipe_),
            receiveBuffer.buffer.data(),
            receiveBuffer.buffer.size(),
            transferred);

        ++readCount;

        LOG_TRACE("pipe=%u, received %zu bytes, ec=%s",
                  pipe, transferred, ec.message().c_str());

        if (ec)
            return ec;

        ++m_stats.receivedPackets;

        if (transferred < HeaderBytes)
        {
            ++m_stats.shortPackets;
            LOG_WARN("pipe=%u, received data is less than the header size", pipe);

            // Did receive less than the header size
            return make_error_code(MVLCErrorCode::ShortRead);
        }

        receiveBuffer.start = receiveBuffer.buffer.data() + HeaderBytes;
        receiveBuffer.end   = receiveBuffer.buffer.data() + transferred;

        u32 header0 = receiveBuffer.header0();
        u32 header1 = receiveBuffer.header1();

        // TODO: update stats, check packet number for loss, record loss
        u16 packetNumber        = (header0 >> header0::PacketNumberShift)  & header0::PacketNumberMask;
        u16 dataWordCount       = (header0 >> header0::NumDataWordsShift)  & header0::NumDataWordsMask;
        u32 udpTimestamp        = (header1 >> header1::TimestampShift)     & header1::TimestampMask;
        u16 nextHeaderPointer   = (header1 >> header1::HeaderPointerShift) & header1::HeaderPointerMask;

        LOG_TRACE("pipe=%u, header0=0x%08x -> packetNumber=%u, wordCount=%u",
                  pipe, header0, packetNumber, dataWordCount);

        LOG_TRACE("pipe=%u, header1=0x%08x -> udpTimestamp=%u, nextHeaderPointer=%u",
                  pipe, header1, udpTimestamp, nextHeaderPointer);

        const u16 availableDataWords = receiveBuffer.available() / sizeof(u32);
        const u16 leftoverBytes = receiveBuffer.available() % sizeof(u32);

        LOG_TRACE("pipe=%u, calculated available data words = %u, leftover bytes = %u",
                  pipe, availableDataWords, leftoverBytes);

        m_stats.lastTimestamp = udpTimestamp;

        if (m_stats.lastPacketNumber < 0)
        {
            m_stats.lastPacketNumber = packetNumber;
        }
        else
        {
            s32 packetDiff = packetNumber - m_stats.lastPacketNumber;

            if (packetDiff == 1)
            {
                m_stats.lastPacketNumber = packetNumber;
            }
            else if (packetDiff > 1)
            {
                m_stats.lostPackets += packetDiff - 1;
                m_stats.lastPacketNumber = packetNumber;
            }
            else if (packetDiff < 1)
            {
                m_stats.unorderedPackets++;
            }
        }

        copy_and_update();
    }

    LOG_TRACE("pipe=%u, read of size=%zu completed using %zu reads, remaining bytes in buffer=%zu",
              pipe, requestedSize, readCount, receiveBuffer.available());

    return {};
}

} // end namespace udp
} // end namespace mvlc
} // end namespace mesytec
