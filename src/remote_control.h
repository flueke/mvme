#ifndef __MVME_REMOTE_CONTROL_H__
#define __MVME_REMOTE_CONTROL_H__

#include <QObject>
#include "mvme_context.h"

namespace remote_control
{

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
        void startDAQ();
        void stopDAQ();
        void pauseDAQ();
        void resumeDAQ();

    private:
        MVMEContext *m_context;
};

class StatusInfoService: public QObject
{
    Q_OBJECT
    public:
        StatusInfoService(MVMEContext *context);

    public slots:
        QVariantMap getDAQState();
        QVariantMap getDAQStats();
        QVariantMap getAnalysisStats();
        QString getLogMessages();

    private:
        MVMEContext *m_context;
};

} // end namespace remote_control

#endif /* __MVME_REMOTE_CONTROL_H__ */
