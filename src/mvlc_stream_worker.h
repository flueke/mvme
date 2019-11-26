#ifndef __MVLC_STREAM_WORKERS_H__
#define __MVLC_STREAM_WORKERS_H__

#include "stream_worker_base.h"

#include "data_buffer_queue.h"
#include "mvlc/mvlc_threading.h"
#include "mvlc/mvlc_readout_parsers.h"
#include "multi_event_splitter.h"

class MVMEContext;

class MVLC_StreamWorker: public StreamWorkerBase
{
    Q_OBJECT
    signals:
        void debugInfoReady(
            const DataBuffer &buffer,
            const mesytec::mvlc::ReadoutParserState &parserState,
            const VMEConfig *vmeConfig,
            const analysis::Analysis *analysis);

    public:
        MVLC_StreamWorker(
            MVMEContext *context,
            ThreadSafeDataBufferQueue *freeBuffers,
            ThreadSafeDataBufferQueue *fullBuffers,
            QObject *parent = nullptr);

        ~MVLC_StreamWorker() override;

        MVMEStreamWorkerState getState() const override
        {
            return m_state;
        }

        void setStartPaused(bool startPaused) override
        {
            m_startPaused = startPaused;
        }

        bool getStartPaused() const override
        {
            return m_startPaused;
        }

        void attachBufferConsumer(IMVMEStreamBufferConsumer *consumer) override
        {
            m_bufferConsumers.push_back(consumer);
        }

        void removeBufferConsumer(IMVMEStreamBufferConsumer *consumer) override
        {
            m_bufferConsumers.removeAll(consumer);
        }

        void attachModuleConsumer(IMVMEStreamModuleConsumer *consumer) override
        {
            m_moduleConsumers.push_back(consumer);
        }

        void removeModuleConsumer(IMVMEStreamModuleConsumer *consumer) override
        {
            m_moduleConsumers.removeAll(consumer);
        }

        virtual MVMEStreamProcessorCounters getCounters() const override
        {
            UniqueLock guard(m_countersMutex);
            return m_counters;
        }

        mesytec::mvlc::ReadoutParserCounters getReadoutParserCounters() const
        {
            UniqueLock guard(m_parserMutex);
            return m_parser.counters;
        }

    public slots:
        void startupConsumers() override;
        void shutdownConsumers() override;

        void start() override;

        void stop(bool whenQueueEmpty = true) override;
        void pause() override;
        void resume() override;
        void singleStep() override;

        void requestDebugInfoOnNextBuffer()
        {
            m_debugInfoRequest = DebugInfoRequest::OnNextBuffer;
        }

        void requestDebugInfoOnNextError()
        {
            m_debugInfoRequest = DebugInfoRequest::OnNextError;
        }

    private:
        ThreadSafeDataBufferQueue *getFreeBuffers() { return m_freeBuffers; }
        ThreadSafeDataBufferQueue *getFullBuffers() { return m_fullBuffers; }
        void setState(MVMEStreamWorkerState newState);

        using UniqueLock = mesytec::mvlc::UniqueLock;

        // Used for the transition from non-Idle state to Idle state.
        enum StopFlag
        {
            StopImmediately,
            StopWhenQueueEmpty,
        };

        enum class DebugInfoRequest
        {
            None,
            OnNextBuffer,
            OnNextError,
        };

        void setupParserCallbacks(const VMEConfig *vmeConfig, analysis::Analysis *analysis);

        void processBuffer(
            DataBuffer *buffer,
            const RunInfo &runInfo,
            const VMEConfig *vmeConfig,
            analysis::Analysis *analysis);

        void logParserInfo(const mesytec::mvlc::ReadoutParserState &parser);

        MVMEContext *m_context;
        ThreadSafeDataBufferQueue *m_freeBuffers,
                                  *m_fullBuffers;

        QVector<IMVMEStreamBufferConsumer *> m_bufferConsumers;
        QVector<IMVMEStreamModuleConsumer *> m_moduleConsumers;

        mutable mesytec::mvme::TicketMutex m_countersMutex;
        MVMEStreamProcessorCounters m_counters = {};

        mutable mesytec::mvme::TicketMutex m_parserMutex;
        mesytec::mvlc::ReadoutParserCallbacks m_parserCallbacks;
        mesytec::mvlc::ReadoutParserState m_parser;

        std::atomic<MVMEStreamWorkerState> m_state;
        std::atomic<MVMEStreamWorkerState> m_desiredState;
        std::atomic<bool> m_startPaused;
        std::atomic<StopFlag> m_stopFlag;
        std::atomic<DebugInfoRequest> m_debugInfoRequest;
        mvme::multi_event_splitter::State m_multiEventSplitter;
        mvme::multi_event_splitter::Callbacks m_multiEventSplitterCallbacks;
};

mesytec::mvlc::VMEConfReadoutScripts LIBMVME_EXPORT
    collect_readout_scripts(const VMEConfig &vmeConfig);

#endif /* __MVLC_STREAM_WORKERS_H__ */
