#include "qt_assistant_remote_control.h"

#include <cassert>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QProcess>

#include "util/qt_str.h"

namespace mesytec
{
namespace mvme
{

struct QtAssistantRemoteControl::Private
{
    QProcess *process = nullptr;

    bool startAssistant();
};

bool QtAssistantRemoteControl::Private::startAssistant()
{
    //qDebug() << __PRETTY_FUNCTION__ << process->state();

    if (process->state() == QProcess::Running)
        return true;

    QString cmd = QSL("assistant");

    QString collectionFile = QCoreApplication::applicationDirPath()
        + QDir::separator() + QSL("doc") + QDir::separator() + QSL("mvme.qhc");

    QStringList args =
    {
        QSL("-collectionFile"),
        collectionFile,
        QSL("-enableRemoteControl"),
    };

    qDebug() << __PRETTY_FUNCTION__ << "Starting assistant: cmd=" << cmd << ", args=" << args;

    process->start(cmd, args);

    if (!process->waitForStarted())
    {
        qDebug() << __PRETTY_FUNCTION__ << "Failed to start assistant: " << process->errorString();
        return false;
    }

    return true;
}

QtAssistantRemoteControl &QtAssistantRemoteControl::instance()
{
    static std::unique_ptr<QtAssistantRemoteControl> theInstance;

    if (!theInstance)
    {
        // Note: make_unique does not work here because it cannot access the
        // private constructor of QtAssistantRemoteControl.
        theInstance = std::unique_ptr<QtAssistantRemoteControl>(
            new QtAssistantRemoteControl());
    }

    return *theInstance;
}

QtAssistantRemoteControl::QtAssistantRemoteControl()
    : QObject()
    , d(std::make_unique<Private>())
{
    d->process = new QProcess(this);
#if 1
    connect(d->process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this] (int exitCode, QProcess::ExitStatus exitStatus)
            {
                qDebug() << __PRETTY_FUNCTION__ << "exitCode =" << exitCode << ", exitStatus =" << exitStatus;
            });
#endif

    assert(d->process);
}

QtAssistantRemoteControl::~QtAssistantRemoteControl()
{
    qDebug() << __PRETTY_FUNCTION__ << d->process;

    assert(d->process);

    if (d->process->state() == QProcess::Running)
    {
        d->process->terminate();
        d->process->waitForFinished(3000);
    }
}

bool QtAssistantRemoteControl::startAssistant()
{
    return d->startAssistant();
}

bool QtAssistantRemoteControl::isRunning() const
{
    return d->process->state() == QProcess::Running;
}

bool QtAssistantRemoteControl::sendCommand(const QString &cmd)
{
    if (d->startAssistant())
    {
        int res = d->process->write(cmd.toLocal8Bit() + '\n');

        if (res != -1)
            return true;

        qDebug() << __PRETTY_FUNCTION__ << "cmd =" << cmd << ", result=" << res;
    }

    return false;
}

} // end namespace mvme
} // end namespace mesytec
