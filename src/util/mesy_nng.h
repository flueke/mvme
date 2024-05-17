#ifndef B18E3651_CA9A_43BC_AA25_810EA16533CD
#define B18E3651_CA9A_43BC_AA25_810EA16533CD

#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pipeline0/push.h>
#include <nng/protocol/pubsub0/pub.h>
#include <nng/protocol/pubsub0/sub.h>
#include <spdlog/spdlog.h>

#include <optional>
#include "thread_name.h"

namespace mesytec::nng
{

inline void mesy_nng_error(const char *const msg, int rv)
{
    spdlog::error("{} ({})", msg, nng_strerror(rv));
}

inline void mesy_nng_error(const std::string &msg, int rv)
{
    spdlog::error("{} ({})", msg, nng_strerror(rv));
}

inline nng_msg *alloc_message(size_t size)
{
    nng_msg *msg = {};
    if (int res = nng_msg_alloc(&msg, size))
    {
        mesy_nng_error("nng_msg_alloc", res);
        return nullptr;
    }
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

inline int set_socket_timeouts(nng_socket socket, nng_duration timeout = 100)
{
    if (int res = nng_socket_set(socket, NNG_OPT_RECVTIMEO, &timeout, sizeof(timeout)))
    {
        mesy_nng_error("nng_socket_set", res);
        return res;
    }

    if (int res = nng_socket_set(socket, NNG_OPT_SENDTIMEO, &timeout, sizeof(timeout)))
    {
        mesy_nng_error("nng_socket_set", res);
        return res;
    }

    return 0;
}

using socket_factory = std::function<int (nng_socket *)>;

inline nng_socket make_socket(socket_factory factory, nng_duration timeout = 100)
{
    nng_socket socket;

    if (int res = factory(&socket))
    {
        mesy_nng_error("make_socket", res);
        return NNG_SOCKET_INITIALIZER;
    }

    if (set_socket_timeouts(socket, timeout) != 0)
    {
        nng_close(socket);
        return NNG_SOCKET_INITIALIZER;
    }

    return socket;
}

inline nng_socket make_pair_socket(nng_duration timeout = 100)
{
    return make_socket(nng_pair0_open, timeout);
}

inline nng_socket make_push_socket(nng_duration timeout = 100)
{
    return make_socket(nng_push0_open, timeout);
}

inline nng_socket make_pull_socket(nng_duration timeout = 100)
{
    return make_socket(nng_pull0_open, timeout);
}

inline nng_socket make_pub_socket(nng_duration timeout = 100)
{
    return make_socket(nng_pub0_open, timeout);
}

inline nng_socket make_sub_socket(nng_duration timeout = 100)
{
    return make_socket(nng_sub0_open, timeout);
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

using retry_predicate = std::function<bool ()>;

class RetryNTimes
{
    public:
        RetryNTimes(size_t maxTries = 3)
            : maxTries_(maxTries)
        {}

        bool operator()()
        {
            return attempt_++ < maxTries_;
        }

    private:
        size_t maxTries_;
        size_t attempt_ = 0u;
};

inline int send_message_retry(nng_socket socket, nng_msg *msg, retry_predicate rp, const char *debugInfo = "")
{
    int res = 0;

    do
    {
        res = nng_sendmsg(socket, msg, 0);

        if (res)
        {
            if (res != NNG_ETIMEDOUT)
                return res;

            if (res == NNG_ETIMEDOUT)
                spdlog::warn("send_message_retry: {} - send timeout", debugInfo);

            if (!rp())
                return res;
        }
    } while (res == NNG_ETIMEDOUT);

    return 0;
}

inline int send_message_retry(nng_socket socket, nng_msg *msg, size_t maxTries = 3, const char *debugInfo = "")
{
    RetryNTimes retryPredicate(maxTries);

    return send_message_retry(socket, msg, retryPredicate, debugInfo);
}

inline int send_empty_message(nng_socket socket, retry_predicate rp)
{
    nng_msg *msg = nullptr;

    if (int res = allocate_reserve_message(&msg, 0))
        return res;

    if (int res = send_message_retry(socket, msg, rp, "send_empty_message"))
    {
        nng_msg_free(msg);
        return res;
    }

    return 0;
}

inline int send_empty_message(nng_socket socket, size_t maxTries = 3)
{
    RetryNTimes retryPredicate(maxTries);

    return send_empty_message(socket, retryPredicate);
}

// Read type T from the front of msg and trim the message by sizeof(T).
template<typename T>
std::optional<T> msg_trim_read(nng_msg *msg)
{
    const auto oldlen = nng_msg_len(msg); (void) oldlen;
    if (nng_msg_len(msg) < sizeof(T))
        return {};

    T result = *reinterpret_cast<T *>(nng_msg_body(msg));
    nng_msg_trim(msg, sizeof(T));
    const auto newlen = nng_msg_len(msg); (void) newlen;
    assert(newlen + sizeof(T) == oldlen);
    return result;
}

inline const char *nng_stat_unit_to_string(int unit)
{
    switch (unit)
    {
        case NNG_UNIT_BYTES:
            return "B";
        case NNG_UNIT_MESSAGES:
            return "msgs";
        case NNG_UNIT_MILLIS:
            return "ms";
        case NNG_UNIT_EVENTS:
            return "events";
    }

    return "";
}

inline const char *nng_stat_type_to_string(int type)
{
    switch (type)
    {
        case NNG_STAT_SCOPE:
            return "scope";
        case NNG_STAT_LEVEL:
            return "level";
        case NNG_STAT_COUNTER:
            return "counter";
        case NNG_STAT_STRING:
            return "string";
        case NNG_STAT_BOOLEAN:
            return "bool";
        case NNG_STAT_ID:
            return "id";
    }

    return "";
}

// Important: as of nng-1.8.0 sockets stats are only implemented for pair1 type
// sockets!

template<typename Visitor>
void visit_nng_stats(nng_stat *stat, Visitor visitor, unsigned depth=0)
{
    visitor(stat, depth);

    auto statType = nng_stat_type(stat);

    if (statType == NNG_STAT_SCOPE)
    {
        for (auto child = nng_stat_child(stat); child; child = nng_stat_next(child))
        {
            visit_nng_stats(child, visitor, depth+1);
        }
    }
}

template<typename Value>
std::string format_stat(int type, const char *name, const char *desc, u64 ts, Value value, int unit)
{
    return fmt::format("type={}, name={}, desc={}, ts={}, value={}, unit={}",
        nng::nng_stat_type_to_string(type),
        name, desc, ts, value,
        nng::nng_stat_unit_to_string(unit));
}

#if 1 // sandbox
using unique_msg_handle = std::unique_ptr<nng_msg, decltype(&nng_msg_free)>;
#endif

struct YDuplicatorContext
{
    std::atomic<bool> quit;
    nng_socket inputSocket;
    std::pair<nng_socket, nng_socket> outputSockets;
    std::function<bool (nng_msg *msg)> isShutdownMessage;
    std::string info;
};

inline void duplicator_loop(YDuplicatorContext &ctx)
{
    util::set_thread_name("duplicator_loop");

    while (!ctx.quit)
    {
        nng_msg *msg = nullptr;

        if (receive_message(ctx.inputSocket, &msg) != 0)
            continue;

        if (ctx.isShutdownMessage && ctx.isShutdownMessage(msg))
        {
            nng_msg_free(msg);
            break;
        }

        nng_msg *clone = nullptr;
        nng_msg_dup(&clone, msg);

        assert(msg && clone);

        auto info = fmt::format("duplicator_loop({}) output0", ctx.info);

        if (msg && send_message_retry(ctx.outputSockets.first, msg, 3, info.c_str()) != 0)
            nng_msg_free(msg);

        info = fmt::format("duplicator_loop({}) output1", ctx.info);

        if (clone && send_message_retry(ctx.outputSockets.second, clone, 3, info.c_str()) != 0)
            nng_msg_free(clone);
    }
}

inline int marry_listen_dial(nng_socket listen, nng_socket dial, const char *url)
{
    if (int res = nng_listen(listen, url, nullptr, 0))
    {
        mesy_nng_error("nng_listen", res);
        return res;
    }

    if (int res = nng_dial(dial, url, nullptr, 0))
    {
        mesy_nng_error("nng_dial", res);
        return res;
    }

    return 0;
}

}

#endif /* B18E3651_CA9A_43BC_AA25_810EA16533CD */
