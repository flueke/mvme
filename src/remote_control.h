#ifndef __MVME_REMOTE_CONTROL_H__
#define __MVME_REMOTE_CONTROL_H__

#include <QHostInfo>
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

    NoVMEControllerFound        = 201,
};

class RemoteControl: public QObject
{
    Q_OBJECT
    public:
        RemoteControl(MVMEContext *context, QObject *parent = nullptr);
        ~RemoteControl();

        void setListenAddress(const QString &address);
        void setListenPort(int port);

        QString getListenAddress() const;
        int getListenPort() const;

        MVMEContext *getContext() const;

    public slots:
        /** Opens the listening socket and starts accepting clients. */
        void start();
        /** Closes the listening socket. */
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
        QString reconnectVMEController();

    private:
        MVMEContext *m_context;
};

class InfoService: public QObject
{
    Q_OBJECT
    public:
        InfoService(MVMEContext *context);

    public slots:
        QString getVersion();
        QStringList getLogMessages();
        QVariantMap getDAQStats();
        QString getVMEControllerType();
        QVariantMap getVMEControllerStats();
        QString getVMEControllerState();

    private:
        MVMEContext *m_context;
};

class HostInfoWrapper: public QObject
{
    Q_OBJECT
    public:
        using Callback = std::function<void (const QHostInfo &)>;

        HostInfoWrapper(Callback callback, QObject *parent = nullptr);

        void lookupHost(const QString &name);

    private slots:
        void lookedUp(const QHostInfo &hi);

    private:
        Callback m_callback;
};

} // end namespace remote_control

#endif /* __MVME_REMOTE_CONTROL_H__ */
