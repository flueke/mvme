#ifndef B18E3651_CA9A_43BC_AA25_810EA16533CD
#define B18E3651_CA9A_43BC_AA25_810EA16533CD

#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pipeline0/push.h>
#include <nng/protocol/pubsub0/pub.h>
#include <nng/protocol/pubsub0/sub.h>
#include <spdlog/spdlog.h>

namespace mesytec::nng
{

inline void mesy_nng_error(const char *const msg, int rv)
{
    spdlog::error("{} ({})", msg, nng_strerror(rv));
}

inline nng_msg *alloc_message(size_t size)
{
    nng_msg *msg = {};
    if (int res = nng_msg_alloc(&msg, size))
        return nullptr;
    return msg;
}

inline int receive_message(nng_socket sock, nng_msg **msg_ptr, int flags = 0)
{
    if (auto res = nng_recvmsg(sock, msg_ptr, flags))
    {
        nng_msg_free(*msg_ptr);
        *msg_ptr = NULL;
        return res;
    }

    return 0;
}

inline int allocate_reserve_message(nng_msg **msg, size_t reserve = 0)
{
    assert(msg);

    if (auto res = nng_msg_alloc(msg, 0))
        return res;

    if (auto res = nng_msg_reserve(*msg, reserve))
        return res;

    return 0;
}

inline size_t allocated_free_space(nng_msg *msg)
{
    auto capacity = nng_msg_capacity(msg);
    auto used = nng_msg_len(msg);
    assert(capacity >= used);
    return capacity - used;
}

inline nng_socket make_pair_socket(nng_duration timeout = 100)
{
    nng_socket socket;

    if (int res = nng_pair0_open(&socket))
    {
        mesy_nng_error("nng_pair0_open", res);
        return NNG_SOCKET_INITIALIZER;
    }

    if (int res = nng_socket_set(socket, NNG_OPT_RECVTIMEO, &timeout, sizeof(timeout)))
    {
        nng_close(socket);
        mesy_nng_error("nng_socket_set", res);
        return NNG_SOCKET_INITIALIZER;
    }

    if (int res = nng_socket_set(socket, NNG_OPT_SENDTIMEO, &timeout, sizeof(timeout)))
    {
        nng_close(socket);
        mesy_nng_error("nng_socket_set", res);
        return NNG_SOCKET_INITIALIZER;
    }

    return socket;
}

inline std::string socket_get_string_opt(nng_socket s, const char *opt)
{
    char *dest = nullptr;

    if (nng_socket_get_string(s, opt, &dest))
        return {};

    std::string result{*dest};
    nng_strfree(dest);
    return result;
}

inline std::string pipe_get_string_opt(nng_pipe p, const char *opt)
{
    char *dest = nullptr;

    if (nng_pipe_get_string(p, opt, &dest))
        return {};

    std::string result{*dest};
    nng_strfree(dest);
    return result;
}

inline void log_socket_info(nng_socket s, const char *info)
{
    auto sockName = socket_get_string_opt(s, NNG_OPT_SOCKNAME);
    auto localAddress = socket_get_string_opt(s, NNG_OPT_LOCADDR);
    auto remoteAddress = socket_get_string_opt(s, NNG_OPT_REMADDR);

    spdlog::info("{}: {}={}", info, NNG_OPT_SOCKNAME, sockName);
    spdlog::info("{}: {}={}", info, NNG_OPT_LOCADDR, localAddress);
    spdlog::info("{}: {}={}", info, NNG_OPT_REMADDR, remoteAddress);
}

inline void log_pipe_info(nng_pipe p, const char *info)
{
    auto sockName = pipe_get_string_opt(p, NNG_OPT_SOCKNAME);
    auto localAddress = pipe_get_string_opt(p, NNG_OPT_LOCADDR);
    auto remoteAddress = pipe_get_string_opt(p, NNG_OPT_REMADDR);

    spdlog::info("{}: {}={}", info, NNG_OPT_SOCKNAME, sockName);
    spdlog::info("{}: {}={}", info, NNG_OPT_LOCADDR, localAddress);
    spdlog::info("{}: {}={}", info, NNG_OPT_REMADDR, remoteAddress);
}

inline int send_message_retry(nng_socket socket, nng_msg *msg, size_t maxTries = 0, const char *debugInfo = "")
{
    int res = 0;
    size_t attempt = 0u;

    do
    {
        res = nng_sendmsg(socket, msg, 0);

        if (res)
        {
            if (res != NNG_ETIMEDOUT)
                return res;

            if (res == NNG_ETIMEDOUT)
                spdlog::warn("send_message_retry: {} - send timeout", debugInfo);

            if (maxTries > 0 && attempt >= maxTries)
                return res;

            ++attempt;
        }
    } while (res == NNG_ETIMEDOUT);

    return 0;
}

}

#endif /* B18E3651_CA9A_43BC_AA25_810EA16533CD */
