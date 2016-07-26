#ifndef UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44
#define UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44

#include <QObject>
#include <QMap>

class DataBuffer;
class MVMEContext;
class DAQEventConfig;

class VMUSBBufferProcessor: public QObject
{
    Q_OBJECT
    public:
        VMUSBBufferProcessor(MVMEContext *context, QObject *parent = 0);

        bool processBuffer(DataBuffer *buffer);

    public slots:
        void resetRunState(); // call this when a new DAQ run starts
        void addFreeBuffer(DataBuffer *buffer);

    private:
        void resetRunState();
        DataBuffer *getFreeBuffer();

        MVMEContext *m_context = 0;
        DataBuffer *m_currentBuffer = 0;
        QMap<int, DAQEventConfig *> m_eventConfigByStackID;
};

#endif
