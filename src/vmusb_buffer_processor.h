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

class VMUSBBufferProcessor: public QObject
{
    Q_OBJECT
    signals:
        void mvmeEventBufferReady(DataBuffer *);
        void logMessage(const QString &);

    public:
        VMUSBBufferProcessor(MVMEContext *context, QObject *parent = 0);

        bool processBuffer(DataBuffer *buffer);

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
        DAQStats *getStats();

        VMUSBBufferProcessorPrivate *m_d;

        MVMEContext *m_context = nullptr;
        DataBuffer *m_currentBuffer = nullptr;
        QMap<int, EventConfig *> m_eventConfigByStackID;
        DataBuffer m_localEventBuffer;
        ListFileWriter *m_listFileWriter = nullptr;
        bool m_logBuffers = false;
        VMUSB *m_vmusb = nullptr;
};

#endif
