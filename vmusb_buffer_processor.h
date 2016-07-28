#ifndef UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44
#define UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44

#include <QObject>
#include <QMap>

class DataBuffer;
class MVMEContext;
class EventConfig;
class BufferIterator;

class VMUSBBufferProcessor: public QObject
{
    Q_OBJECT
    signals:
        void mvmeEventReady(DataBuffer *);

    public:
        VMUSBBufferProcessor(MVMEContext *context, QObject *parent = 0);

        bool processBuffer(DataBuffer *buffer);

    public slots:
        void resetRunState(); // call this when a new DAQ run starts
        void addFreeBuffer(DataBuffer *buffer); // put processed buffers back into the queue

    private:
        DataBuffer *getFreeBuffer();
        bool processEvent(BufferIterator &iter);

        MVMEContext *m_context = 0;
        DataBuffer *m_currentBuffer = 0;
        QMap<int, EventConfig *> m_eventConfigByStackID;
};

#endif
