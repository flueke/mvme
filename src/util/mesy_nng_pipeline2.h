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

    explicit SocketInputReader(nng_socket s): socket(s) {}

    std::pair<unique_msg, int> readMessage() override
    {
        nng_msg *msg = nullptr;
        int res = nng_recvmsg(socket, &msg, receiveFlags);
        return {unique_msg(msg, nng_msg_free), res};
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
        int res = 0;
        if (retryPredicate)
            res = send_message_retry(socket, msg.get(), retryPredicate, debugInfo.c_str());
        else
            res = send_message_retry(socket, msg.get(), maxRetries, debugInfo.c_str());

        if (res == 0)
            msg.release(); // nng has taken ownership at this point

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

            return std::make_pair(unique_msg(nullptr, nng_msg_free), 0);
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
            for (size_t i=0; i<writers.size() - 1; ++i)
            {
                nng_msg *dup = nullptr;

                if (int res = nng_msg_dup(&dup, msg.get()))
                    return res; // allocation failure -> terminate immediately

                if (int res = writers[i]->writeMessage(unique_msg(dup, nng_msg_free)))
                    retval = res; // store last error that occured
            }

            // Now write the original msg to the last writer.
            if (int res = writers.back()->writeMessage(std::move(msg)))
                retval = res;

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

// Abstraction for a nng socket based processing pipeline.
// The pipeline consists of a sequence of processing stages, each stage has an
// input and an output socket. The first stage is the producer stage, having
// only an output socket, while the last stage is a consumer, having only an
// input socket.
// stageN and stageN+1 are connected by a married socket couple. Usually stageN
// output listens, stageN+1 output dials.
struct SocketPipeline
{
    // Processing pipeline element.
    struct Element
    {
        nng_socket inputSocket = NNG_SOCKET_INITIALIZER;
        nng_socket outputSocket = NNG_SOCKET_INITIALIZER;
        std::string inputUrl;
        std::string outputUrl;
    };

    // Married socket couple. Listener is usually the stageN output, dialer is the stageN+1 input.
    struct Couple
    {
        nng_socket listener = NNG_SOCKET_INITIALIZER;
        nng_socket dialer = NNG_SOCKET_INITIALIZER;
        std::string url;
    };

    // Processing stage0 .. stageN-1.
    std::vector<Element> pipeline;

    // Married socket couples stage0 .. stageN-1.
    // couples[N] contains the output socket of stageN and the input socket of stageN-1.
    std::vector<Couple> couples;

    void addElement(nng_socket inputSocket, nng_socket outputSocket,
        const std::string &inputUrl = {}, const std::string &outputUrl = {})
    {
        Element elem;
        elem.inputSocket = inputSocket;
        elem.outputSocket = outputSocket;
        elem.inputUrl = inputUrl;
        elem.outputUrl = outputUrl;

        Couple couple;
        couple.listener = outputSocket;
        couple.url = outputUrl;

        if (!couples.empty())
        {
            couples.back().dialer = inputSocket;
            assert(couples.back().url == inputUrl);
        }

        pipeline.emplace_back(elem);
        couples.emplace_back(couple);
    }

    void addProducer(nng_socket outputSocket, const std::string &outputUrl = {})
    {
        assert(pipeline.empty());
        addElement(NNG_SOCKET_INITIALIZER, outputSocket, {}, outputUrl);
    }

    void addConsumer(nng_socket inputSocket, const std::string &inputUrl = {})
    {
        assert(!pipeline.empty());
        addElement(inputSocket, NNG_SOCKET_INITIALIZER, inputUrl, {});
    }
};

// Create a pipeline from a sequence of married socket couples.
// N couples will result in a pipeline of size N+1. The pipeline starts with a
// producer and ends with a consumer with optional processing elements
// in-between.
inline SocketPipeline pipeline_from_couples(const std::vector<SocketPipeline::Couple> &couples)
{
    SocketPipeline ret;

    if (!couples.empty())
        ret.addProducer(couples.front().listener, couples.front().url);

    for (size_t i=1; i<couples.size(); ++i)
        ret.addElement(couples[i-1].dialer, couples[i].listener, couples[i-1].url, couples[i].url);

    if (!couples.empty())
        ret.addConsumer(couples.back().dialer, couples.back().url);

    return ret;
}

inline std::pair<SocketPipeline::Couple, int> make_pair_couple(const std::string &url)
{
    auto result = std::make_pair<SocketPipeline::Couple, int>({}, 0);
    auto listener = make_pair_socket();
    auto dialer = make_pair_socket();

    if (int res = marry_listen_dial(listener, dialer, url.c_str()))
        result.second = res;
    else
        result.first = {listener, dialer, url};

    return result;
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
