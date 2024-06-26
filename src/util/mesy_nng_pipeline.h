#ifndef B65D1EDC_ABFE_422B_B86C_DF8DCA67ED29
#define B65D1EDC_ABFE_422B_B86C_DF8DCA67ED29

#include "util/mesy_nng.h"

namespace mesytec::nng
{

struct InputReader
{
    virtual std::pair<unique_msg, int> readMessage() = 0;
    virtual ~InputReader() = default;
};

struct OutputWriter
{
    virtual int writeMessage(unique_msg &&msg) = 0;
    virtual ~OutputWriter() = default;
};

struct SocketInputReader: public InputReader
{
    nng_socket socket = NNG_SOCKET_INITIALIZER;
    int receiveFlags = 0; // can be set to NNG_FLAG_NONBLOCK, e.g. for the MultiInputReader
    std::string debugInfo;

    explicit SocketInputReader(nng_socket s): socket(s) {}

    std::pair<unique_msg, int> readMessage() override
    {
        nng_msg *msg = nullptr;
        int res = nng_recvmsg(socket, &msg, receiveFlags);
        return { make_unique_msg(msg), res };
    }
};

struct SocketOutputWriter: public OutputWriter
{
    nng_socket socket = NNG_SOCKET_INITIALIZER;
    size_t maxRetries = 3;
    retry_predicate retryPredicate; // if set, overrides maxRetries
    std::string debugInfo;

    explicit SocketOutputWriter(nng_socket s): socket(s) {}

    int writeMessage(unique_msg &&msg) override
    {
        spdlog::trace("SocketOutputWriter: entering writeMessage, msg=({}, {})", fmt::ptr(msg.get()), fmt::ptr(msg.get_deleter()));

        int res = 0;

        if (retryPredicate)
            res = send_message_retry(socket, msg.get(), retryPredicate, debugInfo.c_str());
        else
            res = send_message_retry(socket, msg.get(), maxRetries, debugInfo.c_str());

        spdlog::trace("SocketOutputWriter: send_message_retry returned {} (msg.get()=({}, {}))", res, fmt::ptr(msg.get()), fmt::ptr(msg.get_deleter()));

        if (res == 0)
        {
            spdlog::trace("SocketOutputWriter: releasing message ({}, {})", fmt::ptr(msg.get()), fmt::ptr(msg.get_deleter()));
            msg.release(); // nng has taken ownership at this point
        }
        else
        {
            spdlog::trace("SocketOutputWriter: write failed for msg=({}, {})", fmt::ptr(msg.get()), fmt::ptr(msg.get_deleter()));
        }

        spdlog::trace("SocketOutputWriter: leaving writeMessage, msg=({}, {})", fmt::ptr(msg.get()), fmt::ptr(msg.get_deleter()));
        return res;
    }
};

// Reads from multiple InputReaders in sequence. If one reader returns a
// message, the others are not checked. Use with NNG_FLAG_NONBLOCK to avoid
// accumulating read timeouts.
class MultiInputReader: public InputReader
{
    public:
        std::pair<unique_msg, int> readMessage() override
        {
            std::lock_guard lock(mutex_);

            for (auto &reader : readers_)
            {
                auto [msg, res] = reader->readMessage();
                if (res == 0)
                    return {std::move(msg), 0};
            }

            return std::make_pair(make_unique_msg(), 0);
        }

        void addReader(std::unique_ptr<InputReader> &&reader)
        {
            std::lock_guard lock(mutex_);
            readers_.emplace_back(std::move(reader));
        }

    private:
        std::mutex mutex_;
        std::vector<std::unique_ptr<InputReader>> readers_;

};

class MultiOutputWriter: public OutputWriter
{
    public:
        int writeMessage(unique_msg &&msg) override
        {
            std::lock_guard lock(mutex_);

            int retval = 0;

            if (writers.empty())
                return 0;

            // Write copies of msg to all writers except the last one.
            for (size_t i=0, size=writers.size(); i<size - 1; ++i)
            {
                nng_msg *dup = nullptr;

                if (int res = nng_msg_dup(&dup, msg.get()))
                    return res; // allocation failure -> terminate immediately

                if (int res = writers[i]->writeMessage(make_unique_msg(dup)))
                    retval = res; // store last error that occured
            }

            // Now write the original msg to the last writer.
            if (int res = writers.back()->writeMessage(std::move(msg)))
            {
                retval = res;
            }

            return retval;
        }

        void addWriter(std::unique_ptr<OutputWriter> &&writer)
        {
            std::lock_guard lock(mutex_);
            writers.emplace_back(std::move(writer));
        }

    private:
        std::mutex mutex_;
        std::vector<std::unique_ptr<OutputWriter>> writers;
};

// Linked socket couple. Listener is usually the stageN output, dialer is the
// stageN+1 input.
struct SocketLink
{
    nng_socket listener = NNG_SOCKET_INITIALIZER;
    nng_socket dialer = NNG_SOCKET_INITIALIZER;
    std::string url;

    bool operator==(const SocketLink &o) const;
    bool operator!=(const SocketLink &o) const { return !(*this == o); }
};

using SocketFactory = std::function<nng_socket ()>;

inline std::pair<SocketLink, int> make_link(
    const std::string &url,
    SocketFactory listenerFactory,
    SocketFactory dialerFactory)
{
    auto result = std::make_pair<SocketLink, int>({}, 0);
    auto listener = listenerFactory();
    auto dialer = dialerFactory();

    if (int res = marry_listen_dial(listener, dialer, url.c_str()))
    {
        result.second = res;
        nng_close(dialer);
        nng_close(listener);
    }
    else
        result.first = {listener, dialer, url};

    return result;
}


inline std::pair<SocketLink, int> make_pair_link(const std::string &url)
{
    return make_link(url, [] { return make_pair_socket(); }, [] { return make_pair_socket(); });
}

// Note: sub subscribes to all incoming messages, even empty ones.
inline std::pair<SocketLink, int> make_pubsub_link(const std::string &url)
{
    auto listenerFactory = [] { return make_pub_socket(); };
    auto dialerFactory = []
    {
        auto s = make_sub_socket();
        // This subscription does receive empty messages.
        nng_socket_set(s, NNG_OPT_SUB_SUBSCRIBE, nullptr, 0);
        return s;
    };

    return make_link(url, listenerFactory, dialerFactory);
}

inline int close_link(SocketLink &link)
{
    int ret = 0;

    if (int res = nng_close(link.dialer))
        ret = res;

    link.dialer = NNG_SOCKET_INITIALIZER;

    if (int res = nng_close(link.listener))
        ret = res;

    link.listener = NNG_SOCKET_INITIALIZER;

    return ret;
}

template<typename LinkContainer>
int close_links(LinkContainer &links)
{
    int ret = 0;

    for (auto &link: links)
        if (auto res = close_link(link))
            ret = res;

    return ret;
}

enum class LinkType
{
    Pair,
    PubSub,
};

struct CreateLinkInfo
{
    LinkType type;
    std::string url;
};

std::pair<std::vector<SocketLink>, int> build_socket_pipeline(const std::vector<CreateLinkInfo> &linkInfos);

}

#endif /* B65D1EDC_ABFE_422B_B86C_DF8DCA67ED29 */
