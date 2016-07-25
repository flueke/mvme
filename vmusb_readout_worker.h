#ifndef UUID_29c99c43_ffae_4ead_8003_c89c87696c15
#define UUID_29c99c43_ffae_4ead_8003_c89c87696c15

#include "mvme_context.h"
#include "vmusb_stack.h"
#include <QObject>

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

    public slots:
        void start(quint32 cycles = 0);
        void stop();
        void addFreeBuffer(DataBuffer *buffer);

    private:
        void readoutLoop();
        void setState(DAQState state);
        void setError(const QString &);
        void processBuffer(DataBuffer *buffer);
        DataBuffer *getFreeBuffer();

        MVMEContext *m_context;
        DAQState m_state = DAQState::Idle;
        quint32 m_cyclesToRun = 0;
        VMUSBStack m_vmusbStack;
        VMECommandList m_stopCommands;
        DataBuffer *m_readBuffer;
};

#endif
