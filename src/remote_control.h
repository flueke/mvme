#ifndef __MVME_REMOTE_CONTROL_H__
#define __MVME_REMOTE_CONTROL_H__

#include <QObject>
#include "mvme_context.h"

namespace remote_control
{

enum ErrorCodes: s32
{
    NotInDAQMode                = 101,
    ReadoutWorkerBusy           = 102,
    AnalysisWorkerBusy          = 103,
    ControllerNotConnected      = 104,
};

class RemoteControl: public QObject
{
    Q_OBJECT
    public:
        RemoteControl(MVMEContext *context, QObject *parent = nullptr);
        ~RemoteControl();

    public slots:
        void start();
        void stop();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

class DAQControlService: public QObject
{
    Q_OBJECT
    public:
        DAQControlService(MVMEContext *context);

    public slots:
        QString getDAQState();
        bool startDAQ();
        bool stopDAQ();

    private:
        MVMEContext *m_context;
};

class InfoService: public QObject
{
    Q_OBJECT
    public:
        InfoService(MVMEContext *context);

    public slots:
        QString getMVMEVersion();
        QStringList getLogMessages();
        QVariantMap getDAQStats();
        QVariantMap getAnalysisStats();
        QString getVMEControllerType();
        QVariantMap getVMEControllerStats();

    private:
        MVMEContext *m_context;
};

} // end namespace remote_control

#endif /* __MVME_REMOTE_CONTROL_H__ */
