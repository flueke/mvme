#ifndef UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44
#define UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44

#include "databuffer.h"

#include <QObject>
#include <QMap>
#include <QFile>

class DataBuffer;
class MVMEContext;
class EventConfig;
class BufferIterator;
class DAQStats;
class ListFileWriter;

class VMUSBBufferProcessor: public QObject
{
    Q_OBJECT
    signals:
        void mvmeEventBufferReady(DataBuffer *);
        void logMessage(const QString &);

    public:
        VMUSBBufferProcessor(MVMEContext *context, QObject *parent = 0);

        bool processBuffer(DataBuffer *buffer);

    public slots:
        void beginRun();
        void endRun();
        void resetRunState(); // call this when a new DAQ run starts
        void addFreeBuffer(DataBuffer *buffer); // put processed buffers back into the queue

    private:
        DataBuffer *getFreeBuffer();
        bool processEvent(BufferIterator &iter, DataBuffer *outputBuffer);
        DAQStats *getStats();

        MVMEContext *m_context = nullptr;
        DataBuffer *m_currentBuffer = nullptr;
        QMap<int, EventConfig *> m_eventConfigByStackID;
        QFile m_listFileOut;
        DataBuffer m_localEventBuffer;
        ListFileWriter *m_listFileWriter = nullptr;
};

#endif
