#ifndef DF704338_EE9D_465F_9467_11BAD11A0DDF
#define DF704338_EE9D_465F_9467_11BAD11A0DDF

#include "multi_crate.h"

namespace mesytec::mvme::multi_crate
{

struct LoopResult
{
    std::error_code ec;
    std::exception_ptr exception;
    int nngError = 0;

    bool hasError() const { return ec || exception || nngError; }
    std::string toString() const;
};

using job_function = std::function<LoopResult ()>;

struct JobRuntime;

class JobContextInterface
{
    public:
        virtual ~JobContextInterface() = default;
        virtual bool shouldQuit() const = 0;
        virtual void setQuit(bool b) = 0;
        void quit() { setQuit(true); }
        virtual nng::InputReader *inputReader() = 0;
        virtual nng::OutputWriter * outputWriter() = 0;
        virtual void setInputReader(nng::InputReader *reader) = 0;
        virtual void setOutputWriter(nng::OutputWriter *writer) = 0;
        virtual mvlc::Protected<SocketWorkPerformanceCounters> &readerCounters() = 0;
        virtual mvlc::Protected<SocketWorkPerformanceCounters> &writerCounters() = 0;
        virtual std::string name() const = 0;
        virtual void setName(const std::string &name) = 0;
        virtual void setJobRuntime(JobRuntime &&rt) = 0;
        virtual JobRuntime &jobRuntime() = 0;

        virtual job_function function() = 0;
};

struct JobRuntime
{
    JobContextInterface *context = nullptr;
    std::future<LoopResult> result;

    bool isRunning()
    {
        return result.valid() && result.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }

    LoopResult wait()
    {
        if (result.valid())
            return result.get();
        return {};
    }

    bool shouldQuit() const { return context->shouldQuit(); }
    void setQuit(bool b) { context->setQuit(b); }
};

class AbstractJobContext: public JobContextInterface
{
    public:
        bool shouldQuit() const override { return quit_; }
        void setQuit(bool b) override { quit_ = b; }

        nng::InputReader *inputReader() override { return inputReader_; }
        nng::OutputWriter * outputWriter() override { return outputWriter_; }
        void setInputReader(nng::InputReader *reader) override { inputReader_ = reader; }
        void setOutputWriter(nng::OutputWriter *writer) override { outputWriter_ = writer; }
        mvlc::Protected<SocketWorkPerformanceCounters> &readerCounters() override { return readerCounters_; }
        mvlc::Protected<SocketWorkPerformanceCounters> &writerCounters() override { return writerCounters_; }
        std::string name() const override { return name_; }
        void setName(const std::string &name) override { name_ = name; }
        void setJobRuntime(JobRuntime &&rt) override { jobRuntime_ = std::move(rt); }
        JobRuntime &jobRuntime() override { return jobRuntime_; }

    private:
        std::atomic<bool> quit_ = false;
        nng::InputReader *inputReader_ = nullptr;
        nng::OutputWriter *outputWriter_ = nullptr;
        mvlc::Protected<SocketWorkPerformanceCounters> readerCounters_;
        mvlc::Protected<SocketWorkPerformanceCounters> writerCounters_;

        std::string name_;
        JobRuntime jobRuntime_;
};

inline JobRuntime start_job(JobContextInterface &context)
{
    assert(!context.jobRuntime().isRunning()); // FIXME: return error in re.result or use std::optional
    JobRuntime rt;
    context.setQuit(false);
    context.readerCounters().access()->start();
    context.writerCounters().access()->start();
    rt.context = &context;
    rt.result = std::async(std::launch::async, context.function());
    return rt;
}

struct ReadoutParserContext;

LoopResult LIBMVME_EXPORT readout_parser_loop(ReadoutParserContext &context);

struct LIBMVME_EXPORT ReadoutParserContext: public AbstractJobContext
{
    public:
        job_function function() override
        {
            return [this] { return readout_parser_loop(*this); };
        }

        u8 crateId = 0;
        mvlc::ConnectionType inputFormat;
        size_t totalReadoutEvents = 0u;
        size_t totalSystemEvents = 0u;
        nng::unique_msg outputMessage = nng::make_unique_msg();
        u32 outputMessageNumber = 0u;
        mvlc::readout_parser::ReadoutParserState parserState;
        mvlc::Protected<mvlc::readout_parser::ReadoutParserCounters> parserCounters;
        std::chrono::steady_clock::time_point tLastFlush;
};

std::unique_ptr<ReadoutParserContext> LIBMVME_EXPORT make_readout_parser_context(const mvlc::CrateConfig &crateConfig);

struct AnalysisProcessingContext;

// Consumes ParsedEventsMessageHeader type messages.
LoopResult LIBMVME_EXPORT analysis_loop(AnalysisProcessingContext &context);

struct LIBMVME_EXPORT AnalysisProcessingContext: public AbstractJobContext
{
    std::shared_ptr<analysis::Analysis> analysis;
    bool isReplay = false;
    std::unique_ptr<multi_crate::MinimalAnalysisServiceProvider> asp = nullptr;
    u8 crateId = 0;

    job_function function() override
    {
        return [this] { return analysis_loop(*this); };
    }
};

std::unique_ptr<AnalysisProcessingContext> LIBMVME_EXPORT make_analysis_context(const std::shared_ptr<analysis::Analysis> &analysis, VMEConfig *vmeConfig);
std::unique_ptr<AnalysisProcessingContext> LIBMVME_EXPORT make_analysis_context(const std::string &filename, VMEConfig *vmeConfig);

struct ReplayJobContext;

LoopResult LIBMVME_EXPORT replay_loop(ReplayJobContext &context);

struct ReplayJobContext: public AbstractJobContext
{
    public:
        mvlc::listfile::ReadHandle *lfh = nullptr;
        std::vector<nng::OutputWriter *> outputWritersByCrate;
        std::vector<std::unique_ptr<mvlc::Protected<SocketWorkPerformanceCounters>>> writerCountersByCrate;

        void setOutputWriterForCrate(unsigned crateId, nng::OutputWriter *writer)
        {
            if (outputWritersByCrate.size() <= crateId)
            {
                outputWritersByCrate.resize(crateId + 1);
                writerCountersByCrate.resize(crateId + 1);
            }
            outputWritersByCrate[crateId] = writer;
            writerCountersByCrate[crateId] = std::make_unique<mvlc::Protected<SocketWorkPerformanceCounters>>();
        }

        job_function function() override
        {
            return [this] { return replay_loop(*this); };
        }
};

// Goal: share a ReplayContext but have this wrapper prodive access to the correct output writer counters for a specific crate
struct CrateReplayWrapperContext: public JobContextInterface
{
    u8 crateId = 0;
    std::shared_ptr<ReplayJobContext> replayContext;

    bool shouldQuit() const override { return replayContext->shouldQuit(); }
    void setQuit(bool b) override { replayContext->setQuit(b); }
    nng::InputReader *inputReader() override { return replayContext->inputReader(); }
    nng::OutputWriter *outputWriter() override { return replayContext->outputWritersByCrate[crateId]; }
    void setInputReader(nng::InputReader *reader) override { replayContext->setInputReader(reader); }
    void setOutputWriter(nng::OutputWriter *writer) override { replayContext->setOutputWriterForCrate(crateId, writer); }
    mvlc::Protected<SocketWorkPerformanceCounters> &readerCounters() override { return replayContext->readerCounters(); }
    mvlc::Protected<SocketWorkPerformanceCounters> &writerCounters() override { return *replayContext->writerCountersByCrate[crateId]; }
    std::string name() const override { return name_; }
    void setName(const std::string &name) override { name_ = name; }
    void setJobRuntime(JobRuntime &&rt) override { replayContext->setJobRuntime(std::move(rt)); }
    JobRuntime &jobRuntime() override { return replayContext->jobRuntime(); }

    job_function function() override
    {
        return [this] { return replay_loop(*replayContext); };
    }

    private:
        std::string name_;
};

class TestConsumerContext;

LoopResult LIBMVME_EXPORT test_consumer_loop(TestConsumerContext &context);

class TestConsumerContext: public AbstractJobContext
{
    public:
        std::shared_ptr<spdlog::logger> logger;

        job_function function() override
        {
            return [this] { return test_consumer_loop(*this); };
        }
};

using SocketLink = nng::SocketPipeline::Link;

#if 0
struct OwningSocketLink
{
    nng_socket listener = NNG_SOCKET_INITIALIZER;
    nng_socket dialer = NNG_SOCKET_INITIALIZER;
    std::string url;

    void swap(OwningSocketLink &o)
    {
        std::swap(listener, o.listener);
        std::swap(dialer, o.dialer);
        std::swap(url, o.url);
    }

    ~OwningSocketLink()
    {
        if (nng_socket_id(dialer) != nng_socket_id(NNG_SOCKET_INITIALIZER))
        {
            spdlog::info("~OwningSocketLink(): closing link.dialer {}", url);
            nng_close(dialer);
            dialer = NNG_SOCKET_INITIALIZER;
        }
        if (nng_socket_id(listener) != nng_socket_id(NNG_SOCKET_INITIALIZER))
        {
            spdlog::info("~OwningSocketLink(): closing link.listener {}", url);
            nng_close(listener);
            listener = NNG_SOCKET_INITIALIZER;
        }
    }

    OwningSocketLink() = default;
    OwningSocketLink(const OwningSocketLink &) = delete;
    OwningSocketLink &operator=(const OwningSocketLink &) = delete;
    OwningSocketLink(OwningSocketLink &&o)
    {
        swap(o);
    }

    OwningSocketLink &operator=(OwningSocketLink &&o)
    {
        swap(o);
        return *this;
    }

    OwningSocketLink(const SocketLink &link)
        : listener(link.listener)
        , dialer(link.dialer)
        , url(link.url)
    {
    }

    OwningSocketLink &operator=(SocketLink &link)
    {
        listener = link.listener;
        dialer = link.dialer;
        url = link.url;
    }

    bool operator==(const OwningSocketLink &o) const;
    bool operator!=(const OwningSocketLink &o) const { return !(*this == o); }
};
#endif

struct CratePipelineStep
{
    SocketLink inputLink;
    SocketLink outputLink;
    int nngError = 0;
    std::shared_ptr<nng::InputReader> reader;
    std::shared_ptr<nng::MultiOutputWriter> writer;
    std::shared_ptr<JobContextInterface> context;

    #if 0
    // Make CratePipelineStep take ownership of the inputLink and outputLink
    // sockets.

    CratePipelineStep() = default;
    ~CratePipelineStep()
    {
        if (nng_socket_id(outputLink.dialer) != nng_socket_id(NNG_SOCKET_INITIALIZER))
        {
            spdlog::info("~CratePipelineStep(): closing outputLink.dialer {}", outputLink.url);
            nng_close(outputLink.dialer);
            outputLink.dialer = NNG_SOCKET_INITIALIZER;
        }
        if (nng_socket_id(outputLink.listener) != nng_socket_id(NNG_SOCKET_INITIALIZER))
        {
            spdlog::info("~CratePipelineStep(): closing outputLink.listener {}", outputLink.url);
            nng_close(outputLink.listener);
            outputLink.listener = NNG_SOCKET_INITIALIZER;
        }
    }

    void swap(CratePipelineStep &o)
    {
        std::swap(inputLink, o.inputLink);
        std::swap(outputLink, o.outputLink);
        std::swap(nngError, o.nngError);
        std::swap(reader, o.reader);
        std::swap(writer, o.writer);
        std::swap(context, o.context);
    }

    CratePipelineStep(CratePipelineStep &&o)
    {
        swap(o);
    }

    CratePipelineStep &operator=(CratePipelineStep &&o)
    {
        swap(o);
        return *this;
    }

    CratePipelineStep(const CratePipelineStep &) = delete;
    CratePipelineStep &operator=(const CratePipelineStep &) = delete;
    #endif
};

class JobRuntimeWatcher: public QObject
{
    Q_OBJECT
    signals:
        void finished();

    public:
        JobRuntimeWatcher(JobRuntime &rt, QObject *parent = nullptr)
            : QObject(parent)
            , rt_(rt)
        {
            connect(&timer_, &QTimer::timeout, this, &JobRuntimeWatcher::check);
        }

    public slots:
        void start()
        {
            timer_.start(100);
        }

    private slots:
        void check()
        {
            if (!rt_.isRunning())
            {
                timer_.stop();
                emit finished();
            }
        }

    private:
        JobRuntime &rt_;
        QTimer timer_;

};

class JobWatcher: public QObject
{
    Q_OBJECT
    signals:
        void finished();

    public:
        JobWatcher(const std::weak_ptr<JobContextInterface> &jobContext, QObject *parent = nullptr)
            : QObject(parent)
            , jobContext_(jobContext)
        {
            connect(&timer_, &QTimer::timeout, this, &JobWatcher::check);
        }

    public slots:
        void start()
        {
            timer_.start(100);
        }

        void setJobContext(const std::weak_ptr<JobContextInterface> &jobContext)
        {
            jobContext_ = jobContext;
        }

    private slots:
        void check()
        {
            if (auto ctx = jobContext_.lock())
            {
                if (ctx->jobRuntime().isRunning())
                {
                    timer_.stop();
                    emit finished();
                }
            }
        }

    private:
        std::weak_ptr<JobContextInterface> jobContext_;
        QTimer timer_;

};


}

#endif /* DF704338_EE9D_465F_9467_11BAD11A0DDF */
