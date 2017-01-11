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
        void logMessage(const QString &);
        void logMessages(const QStringList &, const QString &prefix = QString()); // messages, prefix

    public:
        VMUSBReadoutWorker(MVMEContext *context, QObject *parent = 0);
        ~VMUSBReadoutWorker();

        void setBufferProcessor(VMUSBBufferProcessor *processor) { m_bufferProcessor = processor; }
        VMUSBBufferProcessor *getBufferProcessor() const { return m_bufferProcessor; }
        QString getLastErrorMessage() const { return m_errorMessage; }

    public slots:
        void start(quint32 cycles = 0);
        void stop();
        void pause();
        void resume();

    private:
        void readoutLoop();
        void setState(DAQState state);
        void logError(const QString &);
        void clearError() { m_errorMessage.clear(); }
        int readBuffer(int timeout_ms);

        MVMEContext *m_context;
        DAQState m_state = DAQState::Idle;
        DAQState m_desiredState = DAQState::Idle;
        quint32 m_cyclesToRun = 0;
        VMUSBStack m_vmusbStack;
        DataBuffer *m_readBuffer = 0;
        QMap<u8, u32> m_eventCountPerStack;
        size_t m_nTotalEvents;
        VMUSBBufferProcessor *m_bufferProcessor = 0;
        QString m_errorMessage;
        VMUSB *m_vmusb = nullptr;
};

#endif
