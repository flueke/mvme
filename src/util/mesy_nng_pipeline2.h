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

class SocketPipeline;

int close_sockets(SocketPipeline &pipeline);

// Abstraction for a nng socket based processing pipeline.
// The pipeline consists of a sequence of processing stages, each stage has an
// input and an output socket. The first stage is the producer stage, having
// only an output socket, while the last stage is a consumer, having only an
// input socket.
// stageN and stageN+1 are connected by a married socket couple. Usually stageN
// output listens, stageN+1 output dials.
class SocketPipeline
{
    public:
        // Processing pipeline element.
        struct Element
        {
            nng_socket inputSocket = NNG_SOCKET_INITIALIZER;
            nng_socket outputSocket = NNG_SOCKET_INITIALIZER;
            std::string inputUrl;
            std::string outputUrl;

            bool operator==(const Element &o) const;
            bool operator!=(const Element &o) const { return !(*this == o); }
        };

        // Linked socket couple. Listener is usually the stageN output, dialer is the stageN+1 input.
        struct Link
        {
            nng_socket listener = NNG_SOCKET_INITIALIZER;
            nng_socket dialer = NNG_SOCKET_INITIALIZER;
            std::string url;

            bool operator==(const Link &o) const;
            bool operator!=(const Link &o) const { return !(*this == o); }
        };

        const std::vector<Link> &links() const
        {
            return links_;
        }

        const std::vector<Element> &elements() const
        {
            return elements_;
        }

        // Create a pipeline from a sequence of socket links.
        // N links will result in a pipeline of size N+1. The pipeline starts with a
        // producer and ends with a consumer with optional processing elements
        // in-between.
        static SocketPipeline fromLinks(const std::vector<SocketPipeline::Link> &links)
        {
            SocketPipeline ret;

            if (links.empty())
                return ret;

            // [0]: [X, out(0)]
            {
                SocketPipeline::Element element;
                element.inputSocket = NNG_SOCKET_INITIALIZER;
                element.outputSocket = links.front().listener;
                element.outputUrl = links.front().url;
                ret.elements_.emplace_back(element);
            }

            for (size_t i=1; i<links.size(); ++i)
            {
                // [i]: [in(i-1), out(i)]
                SocketPipeline::Element element;
                element.inputSocket = links[i-1].dialer;
                element.outputSocket = links[i].listener;
                element.inputUrl = links[i-1].url;
                element.outputUrl = links[i].url;
                ret.elements_.emplace_back(element);
            }

            // [i+1]: [in(i-1), X]
            {
                SocketPipeline::Element element;
                element.inputSocket = links.back().dialer;
                element.outputSocket = NNG_SOCKET_INITIALIZER;
                element.inputUrl = links.back().url;
                ret.elements_.emplace_back(element);
            }

            ret.links_ = links;

            return ret;
        }

    private:
        // Processing stage0 .. stageN-1.
        std::vector<Element> elements_;

        // Married socket links stage0 .. stageN-1.
        // links[N] contains the output socket of stageN and the input socket of stageN-1.
        std::vector<Link> links_;
};

inline int close_link(SocketPipeline::Link &link)
{
    int ret = 0;

    if (int res = nng_close(link.dialer))
        ret = res;

    if (int res = nng_close(link.listener))
        ret = res;

    return ret;
}

inline int close_links(const std::vector<SocketPipeline::Link> &links)
{
    int ret = 0;

    for (auto link: links)
    {
        if (int res = close_link(link))
            ret = res;
    }

    return ret;
}

inline int close_sockets(SocketPipeline &pipeline)
{
    int ret = 0;

    for (auto link: pipeline.links())
    {
        if (int res = close_link(link))
            ret = res;
    }

    return ret;
}

using SocketFactory = std::function<nng_socket ()>;

inline std::pair<SocketPipeline::Link, int> make_link(
    const std::string &url,
    SocketFactory listenerFactory,
    SocketFactory dialerFactory)
{
    auto result = std::make_pair<SocketPipeline::Link, int>({}, 0);
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

inline std::pair<SocketPipeline::Link, int> make_pair_link(const std::string &url)
{
    return make_link(url, [] { return make_pair_socket(); }, [] { return make_pair_socket(); });
}

inline std::pair<SocketPipeline::Link, int> make_pubsub_link(const std::string &url)
{
    return make_link(url, [] { return make_pub_socket(); }, [] { return make_sub_socket(); });
}

#if 0
class AbstractLoopContext
{
    public:
        virtual std::atomic<bool> &quit() = 0;
        virtual ~AbstractLoopContext() = default;
};

class AbstractProcessorContext: public AbstractLoopContext
{
    public:
        virtual InputReader *inputReader() = 0;
        virtual OutputWriter *outputWriter() = 0;
        virtual ~AbstractProcessorContext() = default;
};

class BasicProcessorContext: public AbstractProcessorContext
{
    public:
        BasicProcessorContext(std::unique_ptr<InputReader> inputReader = {}, std::unique_ptr<OutputWriter> outputWriter = {}):
            quit_(false), inputReader_(std::move(inputReader)), outputWriter_(std::move(outputWriter)) {}

        std::atomic<bool> &quit() override { return quit_; }
        InputReader *inputReader() override { return inputReader_.get(); }
        OutputWriter *outputWriter() override { return outputWriter_.get(); }
        std::unique_ptr<InputReader> &takeInputReader() { return inputReader_; }
        std::unique_ptr<OutputWriter> &takeOutputWriter() { return outputWriter_; }

    private:
        std::atomic<bool> quit_;
        std::unique_ptr<InputReader> inputReader_;
        std::unique_ptr<OutputWriter> outputWriter_;
};
#endif

}

#endif /* B65D1EDC_ABFE_422B_B86C_DF8DCA67ED29 */
