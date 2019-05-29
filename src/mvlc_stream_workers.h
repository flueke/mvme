#ifndef __MVLC_STREAM_WORKERS_H__
#define __MVLC_STREAM_WORKERS_H__

#include "stream_worker_base.h"

#include "data_buffer_queue.h"
#include "mvlc/mvlc_threading.h"

class MVMEContext;

class MVLC_StreamWorkerBase: public StreamWorkerBase
{
    Q_OBJECT
    public:
        MVLC_StreamWorkerBase(
            MVMEContext *context,
            ThreadSafeDataBufferQueue *freeBuffers,
            ThreadSafeDataBufferQueue *fullBuffers,
            QObject *parent = nullptr);

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
            CountersLock guard(m_countersMutex);
            return m_counters;
        }

    public slots:
        void startupConsumers() override;
        void shutdownConsumers() override;

        void start() override;

        void stop(bool whenQueueEmpty = true) override;
        void pause() override;
        void resume() override;
        void singleStep() override;

    protected:
        ThreadSafeDataBufferQueue *getFreeBuffers() { return m_freeBuffers; }
        ThreadSafeDataBufferQueue *getFullBuffers() { return m_fullBuffers; }
        void setState(MVMEStreamWorkerState newState);

        virtual void beginRun_(
            const RunInfo &runInfo,
            const VMEConfig *vmeConfig,
            analysis::Analysis *analysis) = 0;

        virtual bool processBuffer_(
            DataBuffer *buffer,
            const RunInfo &runInfo,
            const VMEConfig *vmeConfig,
            analysis::Analysis *analysis) = 0;

        using CountersLock = mesytec::mvlc::UniqueLock;
        mutable mesytec::mvlc::TicketMutex m_countersMutex;
        MVMEStreamProcessorCounters m_counters;

    private:
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

        // Used for the transition from non-Idle state to Idle state.
        enum StopFlag
        {
            StopImmediately,
            StopWhenQueueEmpty,
        };

        std::atomic<MVMEStreamWorkerState> m_state;
        std::atomic<MVMEStreamWorkerState> m_desiredState;
        std::atomic<bool> m_startPaused;
        std::atomic<StopFlag> m_stopFlag;
};

class LIBMVME_EXPORT MVLC_ETH_StreamWorker: public MVLC_StreamWorkerBase
{
    Q_OBJECT
    public:
        using MVLC_StreamWorkerBase::MVLC_StreamWorkerBase;
        /*
        MVLC_ETH_StreamWorker(
            MVMEContext *context,
            ThreadSafeDataBufferQueue *freeBuffers,
            ThreadSafeDataBufferQueue *fullBuffers,
            QObject *parent = nullptr);
            */

        ~MVLC_ETH_StreamWorker() override;

    protected:
        void beginRun_(
            const RunInfo &runInfo,
            const VMEConfig *vmeConfig,
            analysis::Analysis *analysis) override;

        bool processBuffer_(
            DataBuffer *buffer,
            const RunInfo &runInfo,
            const VMEConfig *vmeConfig,
            analysis::Analysis *analysis) override;
};

class LIBMVME_EXPORT MVLC_USB_StreamWorker: public MVLC_StreamWorkerBase
{
    Q_OBJECT
    public:
        using MVLC_StreamWorkerBase::MVLC_StreamWorkerBase;
        /*
        MVLC_USB_StreamWorker(
            MVMEContext *context,
            ThreadSafeDataBufferQueue *freeBuffers,
            ThreadSafeDataBufferQueue *fullBuffers,
            QObject *parent = nullptr);
            */

        ~MVLC_USB_StreamWorker() override;

    protected:
        void beginRun_(
            const RunInfo &runInfo,
            const VMEConfig *vmeConfig,
            analysis::Analysis *analysis) override;

        bool processBuffer_(
            DataBuffer *buffer,
            const RunInfo &runInfo,
            const VMEConfig *vmeConfig,
            analysis::Analysis *analysis) override;
};

#endif /* __MVLC_STREAM_WORKERS_H__ */
