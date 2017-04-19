#ifndef UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44
#define UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44

#include "databuffer.h"
#include "threading.h"

#include <QObject>
#include <QMap>
#include <QFile>

class DataBuffer;
class MVMEContext;
class EventConfig;
class BufferIterator;
class DAQStats;
class ListFileWriter;
class VMUSB;

struct VMUSBBufferProcessorPrivate;
struct ProcessorState;

class VMUSBBufferProcessor: public QObject
{
    Q_OBJECT
    public:
        VMUSBBufferProcessor(MVMEContext *context, QObject *parent = 0);

        bool processBuffer(DataBuffer *buffer);
        void processBuffer2(DataBuffer *buffer);

        ThreadSafeDataBufferQueue *m_freeBufferQueue;
        ThreadSafeDataBufferQueue *m_filledBufferQueue;

    public slots:
        void setLogBuffers(bool b) { m_logBuffers = b; }
        void beginRun();
        void endRun();
        void resetRunState(); // call this when a new DAQ run starts

    private:
        DataBuffer *getFreeBuffer();
        bool processEvent(BufferIterator &iter, DataBuffer *outputBuffer, u64 bufferNumber, u16 eventIndex);
        u32 processEvent2(BufferIterator &inIter, DataBuffer *outputBuffer, ProcessorState *state, u16 eventIndex);
        DAQStats *getStats();
        void logMessage(const QString &message);

        friend class VMUSBBufferProcessorPrivate;
        VMUSBBufferProcessorPrivate *m_d;

        MVMEContext *m_context = nullptr;
        QMap<int, EventConfig *> m_eventConfigByStackID;
        DataBuffer m_localEventBuffer;
        ListFileWriter *m_listFileWriter = nullptr;
        bool m_logBuffers = false;
        VMUSB *m_vmusb = nullptr;
};

#endif
