#ifndef UUID_29c99c43_ffae_4ead_8003_c89c87696c15
#define UUID_29c99c43_ffae_4ead_8003_c89c87696c15

#include "mvme_context.h"
#include "vmusb_stack.h"
#include <QObject>

class VMUSBBufferProcessor;

class VMUSBReadoutWorker: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(DAQState);
        void error(const QString &);
        void eventReady(DataBuffer *buffer);

    public:
        VMUSBReadoutWorker(MVMEContext *context, QObject *parent = 0);
        ~VMUSBReadoutWorker();

        DAQState getState() const { return m_state; }
        void setBufferProcessor(VMUSBBufferProcessor *processor) { m_bufferProcessor = processor; }
        VMUSBBufferProcessor *getBufferProcessor() const { return m_bufferProcessor; }

        QString getStartupDebugString() const { return m_startupDebugString; }

    public slots:
        void start(quint32 cycles = 0);
        void stop();

    private:
        void readoutLoop();
        void setState(DAQState state);
        void setError(const QString &);

        MVMEContext *m_context;
        DAQState m_state = DAQState::Idle;
        quint32 m_cyclesToRun = 0;
        VMUSBStack m_vmusbStack;
        VMECommandList m_stopCommands;
        DataBuffer *m_readBuffer = 0;
        QMap<u8, u32> m_eventCountPerStack;
        size_t m_nTotalEvents;
        VMUSBBufferProcessor *m_bufferProcessor = 0;
        QString m_startupDebugString;
};

#endif
