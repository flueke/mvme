#ifndef B65D1EDC_ABFE_422B_B86C_DF8DCA67ED29
#define B65D1EDC_ABFE_422B_B86C_DF8DCA67ED29

#include "util/mesy_nng.h"

namespace mesytec::nng
{

struct InputReader
{
    virtual std::pair<unique_msg_handle, int> readMessage() = 0;
    virtual ~InputReader() = default;
};

struct OutputWriter
{
    virtual int writeMessage(unique_msg_handle &&msg) = 0;
    virtual ~OutputWriter() = default;
};

struct SocketInputReader: public InputReader
{
    nng_socket socket = NNG_SOCKET_INITIALIZER;
    int receiveFlags = 0; // can be set to NNG_FLAG_NONBLOCK, e.g. for the MultiInputReader

    std::pair<unique_msg_handle, int> readMessage() override
    {
        nng_msg *msg = nullptr;
        int res = nng_recvmsg(socket, &msg, receiveFlags);
        return {unique_msg_handle(msg, nng_msg_free), res};
    }
};

struct SocketOutputWriter: public OutputWriter
{
    nng_socket socket = NNG_SOCKET_INITIALIZER;
    size_t maxRetries = 3;
    retry_predicate retryPredicate;
    std::string debugInfo;

    int writeMessage(unique_msg_handle &&msg) override
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
        std::pair<unique_msg_handle, int> readMessage() override
        {
            std::lock_guard lock(mutex_);

            for (auto &reader : readers_)
            {
                auto [msg, res] = reader->readMessage();
                if (res == 0)
                    return {std::move(msg), 0};
            }

            return std::make_pair(unique_msg_handle(nullptr, nng_msg_free), 0);
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
        int writeMessage(unique_msg_handle &&msg) override
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

                if (int res = writers[i]->writeMessage(unique_msg_handle(dup, nng_msg_free)))
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

}

#endif /* B65D1EDC_ABFE_422B_B86C_DF8DCA67ED29 */
