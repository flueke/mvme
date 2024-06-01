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
        //friend LoopResult replay_loop(ReplayJobContext &context);

        mvlc::listfile::ReadHandle *lfh = nullptr;
        std::vector<nng::OutputWriter *> outputWritersByCrate;
        mvlc::Protected<std::vector<SocketWorkPerformanceCounters>> writerCountersByCrate;

        void setOutputWriterForCrate(unsigned crateId, nng::OutputWriter *writer)
        {
            if (outputWritersByCrate.size() <= crateId)
                outputWritersByCrate.resize(crateId + 1);
            outputWritersByCrate[crateId] = writer;
        }

        job_function function() override
        {
            return [this] { return replay_loop(*this); };
        }

    private:
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

}

#endif /* DF704338_EE9D_465F_9467_11BAD11A0DDF */
