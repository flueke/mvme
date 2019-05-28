#ifndef __MVLC_STREAM_WORKERS_H__
#define __MVLC_STREAM_WORKERS_H__

#include "stream_worker_base.h"

#include "data_buffer_queue.h"

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

        MVMEStreamWorkerState getState() const override;

        void setStartPaused(bool startPaused) override;
        bool getStartPaused() const override;

        void attachBufferConsumer(IMVMEStreamBufferConsumer *consumer) override;
        void removeBufferConsumer(IMVMEStreamBufferConsumer *consumer) override;

        void attachModuleConsumer(IMVMEStreamModuleConsumer *consumer) override;
        void removeModuleConsumer(IMVMEStreamModuleConsumer *consumer) override;

    public slots:
        void startupConsumers() override;
        void shutdownConsumers() override;

    protected:
        MVMEContext *getContext() { return m_context; }
        ThreadSafeDataBufferQueue *getFreeBuffers() { return m_freeBuffers; }
        ThreadSafeDataBufferQueue *getFullBuffers() { return m_fullBuffers; }

    private:
        MVMEContext *m_context;
        ThreadSafeDataBufferQueue *m_freeBuffers,
                                  *m_fullBuffers;
};

class LIBMVME_EXPORT MVLC_ETH_StreamWorker: public MVLC_StreamWorkerBase
{
    Q_OBJECT
    public:
};

class LIBMVME_EXPORT MVLC_USB_StreamWorker: public MVLC_StreamWorkerBase
{
    Q_OBJECT
    public:
};

#endif /* __MVLC_STREAM_WORKERS_H__ */
