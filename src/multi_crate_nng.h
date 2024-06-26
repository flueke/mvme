#ifndef DF704338_EE9D_465F_9467_11BAD11A0DDF
#define DF704338_EE9D_465F_9467_11BAD11A0DDF

#include "multi_crate.h"
#include "util/stopwatch.h"
#include "multi_event_splitter.h"

namespace mesytec::mvme::multi_crate
{

// Definitions:
// - stage0: raw mvlc data stream
// - stage1: parsed events. optional: multi event splitting and timestamp based event building
//           per crate stage1 analysis
// - stage2: input is parsed data from stage1
//           stage2 event builder and stage2 analysis belong here
// - stage3 (planned):
//           extracted data values (analysis data extraction)
// FIXME: broken concept. Can also have stage3 data in stage1, so stage1 grows by one step

struct LIBMVME_EXPORT LoopResult
{
    std::error_code ec;
    std::exception_ptr exception;
    int nngError = 0;

    bool hasError() const { return ec || exception || nngError; }
    std::string toString() const;
};

using job_function = std::function<LoopResult ()>;

struct JobRuntime;

class LIBMVME_EXPORT JobContextInterface
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
        virtual std::optional<LoopResult> lastResult() const = 0;
        virtual void setLastResult(const LoopResult &result) = 0;
        virtual void clearLastResult() = 0;

        virtual job_function function() = 0;
        // May do additional things to restart the job, e.g. seek to the beginning of a file.
        //virtual job_function restart_function() { return {}; }
        // May do additional things when stopping the job.
        //virtual job_function stop_function() { return {}; }
};

struct LIBMVME_EXPORT JobRuntime
{
    JobContextInterface *context = nullptr;
    std::future<LoopResult> result;
    std::thread thread;

    bool isRunning()
    {
        return thread.joinable();
    }

    bool isReady() const
    {
        return result.valid() && result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    LoopResult wait()
    {
        if (thread.joinable())
        {
            spdlog::info("JobRuntime::wait(): waiting for thread to finish");
            thread.join();
            spdlog::info("JobRuntime::wait(): thread finished");
        }
        return result.get();
    }

    bool shouldQuit() const { return context->shouldQuit(); }
    void setQuit(bool b) { context->setQuit(b); }
};

class LIBMVME_EXPORT AbstractJobContext: public JobContextInterface
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
        std::optional<LoopResult> lastResult() const override { return lastResult_; }
        void setLastResult(const LoopResult &result) override { lastResult_ = result; }
        void clearLastResult() override { lastResult_.reset(); }

    private:
        std::atomic<bool> quit_ = false;
        nng::InputReader *inputReader_ = nullptr;
        nng::OutputWriter *outputWriter_ = nullptr;
        mvlc::Protected<SocketWorkPerformanceCounters> readerCounters_;
        mvlc::Protected<SocketWorkPerformanceCounters> writerCounters_;

        std::string name_;
        JobRuntime jobRuntime_;
        std::optional<LoopResult> lastResult_;
};

inline bool start_job(JobContextInterface &context)
{
    assert(!context.jobRuntime().isRunning()); // FIXME: return error in re.result or use std::optional

    if (context.jobRuntime().isRunning())
        return false;

    context.clearLastResult();
    context.setQuit(false);
    context.readerCounters().access()->start();
    context.writerCounters().access()->start();

    JobRuntime rt;
    rt.context = &context;
    auto task = std::packaged_task<LoopResult()>(context.function());
    rt.result = task.get_future();
    rt.thread = std::thread([t = std::move(task)]() mutable { t.make_ready_at_thread_exit(); });
    context.setJobRuntime(std::move(rt));
    return true;
}

struct ReadoutParserContext;

LoopResult LIBMVME_EXPORT readout_parser_loop(ReadoutParserContext &context);

struct LIBMVME_EXPORT ReadoutParserContext: public AbstractJobContext
{
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
    StopWatch flushTimer;
};

std::unique_ptr<ReadoutParserContext> LIBMVME_EXPORT make_readout_parser_context(const mvlc::CrateConfig &crateConfig);

struct MultiEventSplitterContext;

LoopResult LIBMVME_EXPORT multievent_splitter_loop(MultiEventSplitterContext &context);

struct LIBMVME_EXPORT MultiEventSplitterContext: public AbstractJobContext
{
    job_function function() override
    {
        return [this] { return multievent_splitter_loop(*this); };
    }

    u8 crateId = 0;
    nng::unique_msg outputMessage = nng::make_unique_msg();
    u32 outputMessageNumber = 0u;
    multi_event_splitter::State state;
    StopWatch flushTimer;
};

struct EventBuilderContext;

LoopResult LIBMVME_EXPORT event_builder_loop(EventBuilderContext &context);

struct LIBMVME_EXPORT EventBuilderContext: public AbstractJobContext
{
    job_function function() override
    {
        return [this] { return event_builder_loop(*this); };
    }

    u8 crateId = 0;
    mvlc::EventBuilderConfig eventBuilderConfig;
    std::unique_ptr<mvlc::EventBuilder> eventBuilder;
    std::array<u8, mvlc::MaxVMECrates> inputCrateMappings;
    std::array<u8, mvlc::MaxVMECrates> outputCrateMappings;

    nng::unique_msg outputMessage = nng::make_unique_msg();
    u32 outputMessageNumber = 0u;
    StopWatch flushTimer;

    EventBuilderContext()
    {
        for (size_t i=0; i<inputCrateMappings.size(); ++i)
        {
            inputCrateMappings[i] = i;
            outputCrateMappings[i] = i;
        }
    }
};

struct AnalysisProcessingContext;

// Consumes ParsedEventsMessageHeader type messages.
LoopResult LIBMVME_EXPORT analysis_loop(AnalysisProcessingContext &context);

struct LIBMVME_EXPORT AnalysisProcessingContext: public AbstractJobContext
{
    u8 crateId = 0;
    RunInfo runInfo;
    std::shared_ptr<analysis::Analysis> analysis;
    std::unique_ptr<multi_crate::MinimalAnalysisServiceProvider> asp = nullptr;

    job_function function() override
    {
        return [this] { return analysis_loop(*this); };
    }
};

std::unique_ptr<AnalysisProcessingContext> LIBMVME_EXPORT make_analysis_context(const std::shared_ptr<analysis::Analysis> &analysis, VMEConfig *vmeConfig);
std::unique_ptr<AnalysisProcessingContext> LIBMVME_EXPORT make_analysis_context(const std::string &filename, VMEConfig *vmeConfig);

struct ReplayJobContext;

LoopResult LIBMVME_EXPORT replay_loop(ReplayJobContext &context);

struct LIBMVME_EXPORT ReplayJobContext: public AbstractJobContext
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

// Goal: share a ReplayContext but have this wrapper provide access to the
// correct output writer counters for a specific crate.
struct LIBMVME_EXPORT CrateReplayWrapperContext: public JobContextInterface
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
    std::optional<LoopResult> lastResult() const override { return lastResult_; }
    void setLastResult(const LoopResult &result) override { lastResult_ = result; }
    void clearLastResult() override { lastResult_.reset(); }

    job_function function() override
    {
        return [this] { return replay_loop(*replayContext); };
    }

    private:
        std::string name_;
        std::optional<LoopResult> lastResult_;
};

class TestConsumerContext;

LoopResult LIBMVME_EXPORT test_consumer_loop(TestConsumerContext &context);

class LIBMVME_EXPORT TestConsumerContext: public AbstractJobContext
{
    public:
        std::shared_ptr<spdlog::logger> logger;

        job_function function() override
        {
            return [this] { return test_consumer_loop(*this); };
        }
};

// Data stream readout working on a mvlc::MVLC instance. Works for both ETH and
// USB connections.
struct MvlcInstanceReadoutContext;

LoopResult LIBMVME_EXPORT readout_loop(MvlcInstanceReadoutContext &context);

struct LIBMVME_EXPORT MvlcInstanceReadoutContext: public AbstractJobContext
{
    // This is put into output ReadoutDataMessageHeader messages and passed
    // to ReadoutLoopPlugins.
    u8 crateId = 0;
    mvlc::MVLC mvlc;

    job_function function() override
    {
        return [this] { return readout_loop(*this); };
    }
};

struct ListfileWriterContext;

LoopResult LIBMVME_EXPORT listfile_writer_loop(ListfileWriterContext &context);

struct LIBMVME_EXPORT ListfileWriterContext: public AbstractJobContext
{
    // This is where readout data is written to if non-null. If the handle is
    // null readout data is read from the input socket and discarded.
    std::unique_ptr<mvlc::listfile::WriteHandle> lfh;

    // Per crate data input counters.
    mvlc::Protected<std::array<SocketWorkPerformanceCounters, mvlc::MaxVMECrates>> dataInputCounters;

    job_function function() override
    {
        return [this] { return listfile_writer_loop(*this); };
    }
};

struct LIBMVME_EXPORT CratePipelineStep
{
    nng::SocketLink inputLink;
    nng::SocketLink outputLink;
    //int nngError = 0;
    std::shared_ptr<nng::InputReader> reader;
    std::shared_ptr<nng::MultiOutputWriter> writer;
    std::shared_ptr<JobContextInterface> context;
};

using CratePipeline = std::vector<CratePipelineStep>;

// Graceful shutdown: does not use setQuit() but only waits for jobs to finish.
// A shutdown message is sent through each steps outputLink. Resets each jobs quit flag.
std::vector<LoopResult> LIBMVME_EXPORT shutdown_pipeline(CratePipeline &pipeline);

// Hard shutdown: uses the quit flag to force all jobs to finish.
// There may be messages left in the pipeline inputLinks after this function returns!
std::vector<LoopResult> LIBMVME_EXPORT quit_pipeline(CratePipeline &pipeline); // hard shutdown

// Read from all input links in the pipeline until error. Returns the total
// number of messages read.
size_t LIBMVME_EXPORT empty_pipeline_inputs(CratePipeline &pipeline);

// Closes the sockets in the given pipeline.
int LIBMVME_EXPORT close_pipeline(CratePipeline &pipeline);

CratePipelineStep LIBMVME_EXPORT make_replay_step(const std::shared_ptr<ReplayJobContext> &replayContext, u8 crateId, nng::SocketLink outputLink);
CratePipelineStep LIBMVME_EXPORT make_readout_step(const std::shared_ptr<MvlcInstanceReadoutContext> &ctx, nng::SocketLink outputLink);
CratePipelineStep LIBMVME_EXPORT make_readout_parser_step(const std::shared_ptr<ReadoutParserContext> &context, nng::SocketLink inputLink, nng::SocketLink outputLink);
CratePipelineStep LIBMVME_EXPORT make_multievent_splitter_step(const std::shared_ptr<MultiEventSplitterContext> &context, nng::SocketLink inputLink, nng::SocketLink outputLink);
CratePipelineStep LIBMVME_EXPORT make_analysis_step(const std::shared_ptr<AnalysisProcessingContext> &context, nng::SocketLink inputLink);
CratePipelineStep LIBMVME_EXPORT make_test_consumer_step(const std::shared_ptr<TestConsumerContext> &context, nng::SocketLink inputLink);
CratePipelineStep LIBMVME_EXPORT make_listfile_writer_step(const std::shared_ptr<ListfileWriterContext> &context, nng::SocketLink inputLink);

class Executor;

class LIBMVME_EXPORT JobObserverInterface
{
    public:
        virtual ~JobObserverInterface() = default;
        virtual void onJobStarted(const std::shared_ptr<JobContextInterface> &ctx) = 0;
        virtual void onJobFinished(const std::shared_ptr<JobContextInterface> &ctx) = 0;
};

class LIBMVME_EXPORT Executor
{
    public:
        void startJob(const std::shared_ptr<JobContextInterface> &context)
        {
            auto fWrap = [context, this] () -> LoopResult
            {
                assert(!context->lastResult().has_value());
                std::unique_lock<std::mutex> lock(mutex_);
                for (auto &observer : observers_)
                    observer->onJobStarted(context);
                lock.unlock();

                LoopResult result;

                try
                {
                    result = context->function()();
                }
                catch (const std::exception &e)
                {
                    result.exception = std::current_exception();
                }

                context->setQuit(false);

                lock.lock();
                for (auto &observer : observers_)
                    observer->onJobFinished(context);

                return result;
            };

            auto task = std::packaged_task<LoopResult()>(fWrap);
            JobRuntime rt;
            rt.context = context.get();
            rt.result = task.get_future();
            context->clearLastResult();
            context->setQuit(false);
            context->readerCounters().access()->start();
            context->writerCounters().access()->start();
            rt.thread = std::thread([t = std::move(task)]() mutable { t.make_ready_at_thread_exit(); });
            context->setJobRuntime(std::move(rt));
        }

        void addObserver(const std::shared_ptr<JobObserverInterface> &observer)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            observers_.push_back(observer);
        }

        void removeObserver(const std::shared_ptr<JobObserverInterface> &observer)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            observers_.erase(std::remove(observers_.begin(), observers_.end(), observer), observers_.end());
        }

    private:
        std::mutex mutex_;
        std::vector<std::shared_ptr<JobObserverInterface>> observers_;
};

struct LIBMVME_EXPORT LoggingJobObserver: public JobObserverInterface
{
    void onJobStarted(const std::shared_ptr<JobContextInterface> &ctx) override
    {
        spdlog::info("LoggingJobObserver: Job started: {}", ctx->name());
    }

    void onJobFinished(const std::shared_ptr<JobContextInterface> &ctx) override
    {
        spdlog::info("LoggingJobObserver: Job finished: {}", ctx->name());
    }
};

// Crate ids > 8 are used for system streams. Currently 0xff is the combined stage2 stream.
// Note that contexts may be null: splitter and eventbuilder are optional in stage1,
// stage2 normally consists of event_builder -> analysis only.

struct LIBMVME_EXPORT CrateProcessor
{
    u8 crateId;
    std::shared_ptr<ReadoutParserContext> parserContext;
    std::shared_ptr<MultiEventSplitterContext> splitterContext;
    std::shared_ptr<EventBuilderContext> eventBuilderContext;
    std::shared_ptr<AnalysisProcessingContext> analysisContext;
    CratePipeline pipeline;
};

struct LIBMVME_EXPORT ReplayProcessor: public CrateProcessor
{
    std::shared_ptr<ReplayJobContext> replayContext;
};

struct LIBMVME_EXPORT ReadoutProcessor: public CrateProcessor
{
    std::shared_ptr<MvlcInstanceReadoutContext> readoutContext;
};

struct LIBMVME_EXPORT VmeConfigs
{
    std::unique_ptr<VMEConfig> vmeConfig;
    mvlc::CrateConfig crateConfig;
};

struct LIBMVME_EXPORT ReplayModel
{
    std::vector<ReplayProcessor> processors;
    std::vector<VmeConfigs> vmeConfigs;
};

struct LIBMVME_EXPORT ReadoutModel
{
    std::vector<ReadoutProcessor> processors;
    std::vector<VmeConfigs> vmeConfigs;
};

// stage0: raw data streams
// stage1: parser -> splitter -> eventbuilder -> to analysis
//                                            -> to stage2
// stage2: eventbuilder -> analysis

struct LIBMVME_EXPORT Stage1BuildInfo
{
    std::string uniqueUrlPart = "";
    bool isReplay = false;
    bool withSplitter = false;
    bool withEventBuilder = false;
    u8 crateId = 0;
};

struct LIBMVME_EXPORT Stage2BuildInfo
{
    std::string uniqueUrlPart = "";
    bool withEventBuilder = false;
    u8 crateId = 255u;
};

// Returns the following links: raw_data, parsed_data[, split_data][, time_matched_data]
std::pair<std::vector<nng::SocketLink>, int> LIBMVME_EXPORT build_stage1_socket_links(const Stage1BuildInfo &buildInfo);

// Returns at least one link: (stage1_output, stage2_input). If stage2 event
// building is enabled a second link is created and returned.
// The single stage1->stage2 socket is written to by all stage1 crate pipelines.
// The single reader is either the stage2 event builder or directly the stage2
// analysis.
std::pair<std::vector<nng::SocketLink>, int> LIBMVME_EXPORT build_stage2_socket_links(const Stage2BuildInfo &buildInfo);

}

#endif /* DF704338_EE9D_465F_9467_11BAD11A0DDF */
