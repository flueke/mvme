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
