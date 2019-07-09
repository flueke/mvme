#include "mvlc/mvlc_impl_eth.h"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

#include <QDebug>

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

#include "mvlc/mvlc_buffer_validators.h"
#include "mvlc/mvlc_dialog.h"
#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_util.h"
#include "util/strings.h"

#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MVLC_ETH_LOG_LEVEL
#define MVLC_ETH_LOG_LEVEL LOG_LEVEL_WARN
#endif

#define LOG_LEVEL_SETTING MVLC_ETH_LOG_LEVEL

#define DO_LOG(level, prefix, fmt, ...)\
do\
{\
    if (LOG_LEVEL_SETTING >= level)\
    {\
        fprintf(stderr, prefix "%s(): " fmt "\n", __FUNCTION__, ##__VA_ARGS__);\
    }\
} while (0);

#define LOG_WARN(fmt, ...)  DO_LOG(LOG_LEVEL_WARN,  "WARN - mvlc_eth ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DO_LOG(LOG_LEVEL_INFO,  "INFO - mvlc_eth ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - mvlc_eth ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DO_LOG(LOG_LEVEL_TRACE, "TRACE - mvlc_eth ", fmt, ##__VA_ARGS__)

namespace
{


// Does IPv4 host lookup for a UDP socket. On success the resulting struct
// sockaddr_in is copied to dest.
std::error_code lookup(const std::string &host, u16 port, sockaddr_in &dest)
{
    using namespace mesytec::mvlc;

    if (host.empty())
        return MVLCErrorCode::EmptyHostname;

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
    {
        qDebug("%s: HostLookupError, host=%s, error=%s", __PRETTY_FUNCTION__, host.c_str(),
               gai_strerror(rc));
        return make_error_code(MVLCErrorCode::HostLookupError);
    }

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
    {
        qDebug("%s: HostLookupError, host=%s, no result found", __PRETTY_FUNCTION__, host.c_str());
        return make_error_code(MVLCErrorCode::HostLookupError);
    }

    return {};
}

struct timeval ms_to_timeval(unsigned ms)
{
    unsigned seconds = ms / 1000;
    ms -= seconds * 1000;

    struct timeval tv;
    tv.tv_sec  = seconds;
    tv.tv_usec = ms * 1000;

    return tv;
}

#ifndef __WIN32
std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    struct timeval tv = ms_to_timeval(ms);

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
static const int SocketReceiveBufferSize = 1024 * 1024 * 100;

} // end anon namespace

namespace mesytec
{
namespace mvlc
{
namespace eth
{

Impl::Impl(const std::string &host)
    : m_host(host)
{
#ifdef __WIN32
    WORD wVersionRequested;
    WSADATA wsaData;
    wVersionRequested = MAKEWORD(2, 1);
    WSAStartup( wVersionRequested, &wsaData );
#endif
}

Impl::~Impl()
{
    disconnect();

#ifdef __WIN32
    WSACleanup();
#endif
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
//   should tell us if another client is currently using the MVLC. It could be
//   some sort of "DAQ mode register" or a way to check where the MVLC is
//   currently sending its data output.
std::error_code Impl::connect()
{
    auto close_sockets = [this] ()
    {
        if (m_cmdSock >= 0)
            ::close(m_cmdSock);
        if (m_dataSock >= 0)
            ::close(m_dataSock);
        m_cmdSock = -1;
        m_dataSock = -1;
    };

    if (isConnected())
        return make_error_code(MVLCErrorCode::IsConnected);

    m_cmdSock = -1;
    m_dataSock = -1;
    resetPipeAndChannelStats();
    std::fill(m_lastPacketNumbers.begin(), m_lastPacketNumbers.end(), -1);

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

        // both socket and bind calls succeeded
        break;

        try_again:
        {
            close_sockets();
        }
    }

    if (m_cmdSock < 0 || m_dataSock < 0)
        return make_error_code(MVLCErrorCode::BindLocalError);

    // Call connect on the sockets so that we receive only datagrams
    // originating from the MVLC.
    if (int res = ::connect(m_cmdSock, reinterpret_cast<struct sockaddr *>(&m_cmdAddr),
                            sizeof(m_cmdAddr)))
    {
        close_sockets();
        return std::error_code(errno, std::system_category());
    }

    if (int res = ::connect(m_dataSock, reinterpret_cast<struct sockaddr *>(&m_dataAddr),
                            sizeof(m_dataAddr)))
    {
        close_sockets();
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

        if (res != 0)
            return std::error_code(errno, std::system_category());

        {
            int actualBufferSize = 0;
            socklen_t szLen = sizeof(actualBufferSize);

#ifndef __WIN32
            res = getsockopt(getSocket(pipe), SOL_SOCKET, SO_RCVBUF,
                             &actualBufferSize,
                             &szLen);
#else
            res = getsockopt(getSocket(pipe), SOL_SOCKET, SO_RCVBUF,
                             reinterpret_cast<char *>(&actualBufferSize),
                             &szLen);
#endif

            assert(res == 0);
            if (res != 0)
                return std::error_code(errno, std::system_category());

            LOG_INFO("pipe=%u, SO_RCVBUF=%d", static_cast<unsigned>(pipe), actualBufferSize);

            if (actualBufferSize < SocketReceiveBufferSize)
            {
                LOG_INFO("pipe=%u, requested SO_RCVBUF of %d bytes, got %d bytes",
                         static_cast<unsigned>(pipe), SocketReceiveBufferSize, actualBufferSize);
            }
        }
    }

    assert(m_cmdSock >= 0 && m_dataSock >= 0);

    // Send some initial request to verify there's an MVLC on the other side.
    //
    // Try to read the destination Command pipe address from the MVLC. Sending
    // this command will make the MVLC set the source IP address as the Command
    // pipes destination address and then send out the reply. This means the
    // resulting address should be the clients IP address used to connect to
    // the MVLC.
    {
        MVLCDialog dlg(this);
        u32 cmd_ip_lo = 0;
        u32 cmd_ip_hi = 0;

        if (auto ec = dlg.readRegister(registers::cmd_ip_lo, cmd_ip_lo))
        {
            close_sockets();
            return ec;
        }

        if (auto ec = dlg.readRegister(registers::cmd_ip_hi, cmd_ip_hi))
        {
            close_sockets();
            return ec;
        }

        u32 cmd_ip = (cmd_ip_hi << 16) | cmd_ip_lo;

        LOG_INFO("MVLC destination Command IP-Address: %s", format_ipv4(cmd_ip).toStdString().c_str());
    }

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
//static const size_t MaxIncomingPayloadSIze = MaxOutgoingPayloadSize;

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    // Note: it is not necessary to split this into multiple calls to send()
    // because outgoing MVLC command buffers have to be smaller than the
    // maximum, non-jumbo ethernet MTU.
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

#ifdef __WIN32
// FIXME: use WSAGetLastError here once the std;:error_code infrastructure exists
static inline std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
                                                 u16 &bytesTransferred, int timeout_ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    struct timeval tv = ms_to_timeval(timeout_ms);

    int sres = ::select(0, &fds, nullptr, nullptr, &tv);

    if (sres == 0)
        return make_error_code(MVLCErrorCode::SocketTimeout);

    if (sres == SOCKET_ERROR)
        return make_error_code(MVLCErrorCode::SocketError);

    ssize_t res = ::recv(sockfd, reinterpret_cast<char *>(dest), size, 0);

    if (res < 0)
        return make_error_code(MVLCErrorCode::SocketError);

    bytesTransferred = res;
    return {};
}
#else
static inline std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
                                                 u16 &bytesTransferred, int)
{
    bytesTransferred = 0u;

    ssize_t res = ::recv(sockfd, reinterpret_cast<char *>(dest), size, 0);

    if (res < 0)
        return std::error_code(errno, std::system_category());

    bytesTransferred = res;
    return {};
}
#endif

PacketReadResult Impl::read_packet(Pipe pipe_, u8 *buffer, size_t size)
{
    PacketReadResult res = {};

    unsigned pipe = static_cast<unsigned>(pipe_);
    auto &pipeStats = m_pipeStats[pipe];

    ++pipeStats.receiveAttempts;

    if (pipe >= PipeCount)
    {
        res.ec = make_error_code(MVLCErrorCode::InvalidPipe);
        return res;
    }

    if (!isConnected())
    {
        res.ec = make_error_code(MVLCErrorCode::IsDisconnected);
        return res;
    }

    res.ec = receive_one_packet(getSocket(pipe_), buffer, size,
                                res.bytesTransferred,
                                getReadTimeout(pipe_));
    res.buffer = buffer;

    if (res.ec && res.bytesTransferred == 0)
        return res;

    ++pipeStats.receivedPackets;
    pipeStats.receivedBytes += res.bytesTransferred;
    ++pipeStats.packetSizes[res.bytesTransferred];

    if (!res.hasHeaders())
    {
        ++pipeStats.shortPackets;
        LOG_WARN("  pipe=%u, received data is smaller than the MVLC UDP header size", pipe);
        res.ec = make_error_code(MVLCErrorCode::ShortRead);
        return res;
    }

    LOG_TRACE("  pipe=%u, header0=0x%08x -> packetChannel=%u, packetNumber=%u, wordCount=%u",
              pipe, res.header0(), res.packetChannel(), res.packetNumber(), res.dataWordCount());

    LOG_TRACE("  pipe=%u, header1=0x%08x -> udpTimestamp=%u, nextHeaderPointer=%u",
              pipe, res.header1(), res.udpTimestamp(), res.nextHeaderPointer());

    LOG_TRACE("  pipe=%u, calculated available data words = %u, leftover bytes = %u",
              pipe, res.availablePayloadWords(), res.leftoverBytes());

    if (res.leftoverBytes() > 0)
    {
        LOG_WARN("  pipe=%u, %u leftover bytes in received packet",
                 pipe, res.leftoverBytes());
        ++pipeStats.packetsWithResidue;
    }

    if (res.packetChannel() >= NumPacketChannels)
    {
        LOG_WARN("  pipe=%u, packet channel number out of range: %u", pipe, res.packetChannel());
        ++pipeStats.packetChannelOutOfRange;
        res.ec = make_error_code(MVLCErrorCode::UDPPacketChannelOutOfRange);
        return res;
    }

    auto &channelStats = m_packetChannelStats[res.packetChannel()];
    ++channelStats.receivedPackets;
    channelStats.receivedBytes += res.bytesTransferred;

    {
        auto &lastPacketNumber = m_lastPacketNumbers[res.packetChannel()];

        LOG_TRACE("  pipe=%u, packetChannel=%u, packetNumber=%u, lastPacketNumber=%d",
                  pipe, res.packetChannel(), res.packetNumber(), lastPacketNumber);

        // Packet loss calculation. The initial lastPacketNumber value is -1.
        if (lastPacketNumber >= 0)
        {
            auto loss = calc_packet_loss(lastPacketNumber, res.packetNumber());

            if (loss > 0)
            {
                LOG_WARN("  pipe=%u, packetChannel=%u, lastPacketNumber=%u,"
                         " packetNumber=%u, loss=%d",
                         pipe, res.packetChannel(), lastPacketNumber, res.packetNumber(), loss);
            }

            res.lostPackets = loss;
            pipeStats.lostPackets += loss;
            channelStats.lostPackets += loss;
        }

        lastPacketNumber = res.packetNumber();
        ++channelStats.packetSizes[res.bytesTransferred];
    }

    // Check where nextHeaderPointer is pointing to
    if (res.nextHeaderPointer() != header1::NoHeaderPointerPresent)
    {
        u32 *start = res.payloadBegin();
        u32 *end   = res.payloadEnd();
        u32 *headerp = start + res.nextHeaderPointer();

        if (headerp >= end)
        {
            ++pipeStats.headerOutOfRange;
            ++channelStats.headerOutOfRange;

            LOG_INFO("  pipe=%u, nextHeaderPointer out of range: nHPtr=%u, "
                     "availDataWords=%u, pktChan=%u, pktNum=%d, pktSize=%u bytes",
                     pipe, res.nextHeaderPointer(), res.availablePayloadWords(),
                     res.packetChannel(), res.packetNumber(), res.bytesTransferred);
        }
        else
        {
            u32 header = *headerp;
            LOG_TRACE("  pipe=%u, nextHeaderPointer=%u -> header=0x%08x",
                      pipe, res.nextHeaderPointer(), header);
            u32 type = get_frame_type(header);
            ++pipeStats.headerTypes[type];
            ++channelStats.headerTypes[type];
        }
    }
    else
    {
        LOG_TRACE("  pipe=%u, NoHeaderPointerPresent, eth header1=0x%08x",
                  pipe, res.header1());
        ++pipeStats.noHeader;
        ++channelStats.noHeader;
    }

    return res;
}

/* initial:
 *   next_header_pointer = 0
 *   packet_number = 0
 *
 *   - receive one packet
 *   - make sure there are two header words
 *   - extract packet_number and number_of_data_words
 *   - record possible packet loss or ordering problems based on packet number
 *   - check to make sure timestamp is incrementing (packet ordering) (not implemented yet)
 *   -
 */

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

    LOG_TRACE("+ pipe=%u, size=%zu, bufferAvail=%zu", pipe, requestedSize, receiveBuffer.available());

    copy_and_update();

    if (size == 0)
    {
        LOG_TRACE("  pipe=%u, size=%zu, read request satisfied from buffer, new buffer size=%zu",
                  pipe, requestedSize, receiveBuffer.available());
        return {};
    }

    // All data from the read buffer should have been consumed at this point.
    // It's time to issue actual read requests.
    assert(receiveBuffer.available() == 0);

    size_t readCount = 0u;
    auto &pipeStats = m_pipeStats[pipe];
    const auto tStart = std::chrono::high_resolution_clock::now();

    while (size > 0)
    {
        assert(receiveBuffer.available() == 0);
        receiveBuffer.reset();

        LOG_TRACE("  pipe=%u, requestedSize=%zu, remainingSize=%zu, reading from MVLC...",
                  pipe, requestedSize, size);

        auto rr = read_packet(pipe_, receiveBuffer.buffer.data(), receiveBuffer.buffer.size());

        ++readCount;

        LOG_TRACE("  pipe=%u, received %u bytes, ec=%s",
                  pipe, rr.bytesTransferred, rr.ec.message().c_str());

        if (rr.ec && rr.bytesTransferred == 0)
            return rr.ec;

        receiveBuffer.start = reinterpret_cast<u8 *>(rr.payloadBegin());
        receiveBuffer.end   = reinterpret_cast<u8 *>(rr.payloadEnd());

        // Copy to destination buffer
        copy_and_update();

        auto tEnd = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);

        //qDebug() << elapsed.count() << getReadTimeout(pipe_);

        if (elapsed.count() >= getReadTimeout(pipe_))
        {
            LOG_TRACE("  pipe=%u, read of size=%zu completes with %zu bytes and timeout"
                      " after %zu reads, remaining bytes in buffer=%zu",
                      pipe, requestedSize, bytesTransferred, readCount,
                      receiveBuffer.available());

            return make_error_code(MVLCErrorCode::SocketTimeout);
        }
    }

    LOG_TRACE("  pipe=%u, read of size=%zu completed using %zu reads, remaining bytes in buffer=%zu",
              pipe, requestedSize, readCount, receiveBuffer.available());

    return {};
}

std::error_code Impl::getReadQueueSize(Pipe pipe_, u32 &dest)
{
    auto pipe = static_cast<unsigned>(pipe_);
    assert(pipe < PipeCount);

    if (pipe < PipeCount)
        dest = m_receiveBuffers[static_cast<unsigned>(pipe)].available();

    return make_error_code(MVLCErrorCode::InvalidPipe);
}

std::array<PipeStats, PipeCount> Impl::getPipeStats() const
{
    return m_pipeStats;
}

std::array<PacketChannelStats, NumPacketChannels> Impl::getPacketChannelStats() const
{
    return m_packetChannelStats;
}

void Impl::resetPipeAndChannelStats()
{
    m_pipeStats = {};
    m_packetChannelStats = {};
}

u32 Impl::getCmdAddress() const
{
    return ::ntohl(m_cmdAddr.sin_addr.s_addr);
}

u32 Impl::getDataAddress() const
{
    return ::ntohl(m_dataAddr.sin_addr.s_addr);
}

s32 calc_packet_loss(u16 lastPacketNumber, u16 packetNumber)
{
    static const s32 PacketNumberMax = eth::header0::PacketNumberMask;

    s32 diff = packetNumber - lastPacketNumber;

    if (diff < 1)
    {
        diff = PacketNumberMax + diff;
        return diff;
    }

    return diff - 1;
}

} // end namespace eth
} // end namespace mvlc
} // end namespace mesytec
