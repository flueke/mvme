#ifndef UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44
#define UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44

#include <QObject>
#include <QMap>
#include <QFile>

class DataBuffer;
class MVMEContext;
class EventConfig;
class BufferIterator;

class VMUSBBufferProcessor: public QObject
{
    Q_OBJECT
    signals:
        void mvmeEventBufferReady(DataBuffer *);

    public:
        VMUSBBufferProcessor(MVMEContext *context, QObject *parent = 0);

        bool processBuffer(DataBuffer *buffer);

    public slots:
        void beginRun();
        void endRun();
        void resetRunState(); // call this when a new DAQ run starts
        void addFreeBuffer(DataBuffer *buffer); // put processed buffers back into the queue
        void setListFileOutputEnabled(bool b) { m_listFileOutputEnabled = b; }

    private:
        DataBuffer *getFreeBuffer();
        bool processEvent(BufferIterator &iter);

        MVMEContext *m_context = 0;
        DataBuffer *m_currentBuffer = 0;
        QMap<int, EventConfig *> m_eventConfigByStackID;
        QFile m_listFileOut;
        bool m_listFileOutputEnabled = true;
};

#endif
