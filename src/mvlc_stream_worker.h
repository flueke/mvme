#ifndef __MVLC_STREAM_WORKERS_H__
#define __MVLC_STREAM_WORKERS_H__

#include "stream_worker_base.h"

#include "data_buffer_queue.h"
#include "mvlc/mvlc_threading.h"
#include "mvlc/mvlc_readout_parsers.h"

class MVMEContext;

class MVLC_StreamWorker: public StreamWorkerBase
{
    Q_OBJECT
    signals:
        void debugInfoReady(
            const DataBuffer &buffer,
            const mesytec::mvlc::ReadoutParserState &parserState);

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

        mutable mesytec::mvlc::TicketMutex m_countersMutex;
        MVMEStreamProcessorCounters m_counters = {};

        mutable mesytec::mvlc::TicketMutex m_parserMutex;
        mesytec::mvlc::ReadoutParserCallbacks m_parserCallbacks;
        mesytec::mvlc::ReadoutParserState m_parser;

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

        void setupParserCallbacks(analysis::Analysis *analysis);

        void processBuffer(
            DataBuffer *buffer,
            const RunInfo &runInfo,
            const VMEConfig *vmeConfig,
            analysis::Analysis *analysis);

        MVMEContext *m_context;
        ThreadSafeDataBufferQueue *m_freeBuffers,
                                  *m_fullBuffers;

        QVector<IMVMEStreamBufferConsumer *> m_bufferConsumers;
        QVector<IMVMEStreamModuleConsumer *> m_moduleConsumers;

        std::atomic<MVMEStreamWorkerState> m_state;
        std::atomic<MVMEStreamWorkerState> m_desiredState;
        std::atomic<bool> m_startPaused;
        std::atomic<StopFlag> m_stopFlag;


        std::atomic<DebugInfoRequest> m_debugInfoRequest;
};

#endif /* __MVLC_STREAM_WORKERS_H__ */
