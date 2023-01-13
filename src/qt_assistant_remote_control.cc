/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "qt_assistant_remote_control.h"

#include <cassert>
#include <iostream>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QProcess>

#include "util/qt_str.h"

using std::cout;
using std::endl;

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

    QString collectionFile;
    auto appPath = QCoreApplication::applicationDirPath();

    if (appPath.endsWith("build"))
    {
        collectionFile = appPath + "/doc/sphinx/qthelp/mvme.qhc";
    }
    else
    {
        collectionFile = appPath
            + QDir::separator() + ".." + QDir::separator() + QSL("doc") + QDir::separator() + QSL("mvme.qhc");
    }

    QStringList args =
    {
        QSL("-collectionFile"),
        collectionFile,
        QSL("-enableRemoteControl"),
    };

    QString cmd = QCoreApplication::applicationDirPath() + QDir::separator() + QSL("assistant");

    cout << "Starting Qt Assistant: cmd=" << cmd.toStdString()
        << ", args=" << args.join(' ').toStdString() << endl;

    //qDebug() << __PRETTY_FUNCTION__ << "Starting assistant: cmd=" << cmd << ", args=" << args;

    process->start(cmd, args);

    if (!process->waitForStarted())
    {
        cout << "Failed to start Qt Assistant: " << process->errorString().toStdString() << endl;

        cmd = QSL("assistant");

        cout << "Trying again with cmd=" << cmd.toStdString() << endl;

        process->start(cmd, args);

        if (!process->waitForStarted())
        {
            cout << "Failed to start Qt Assistant: " << process->errorString().toStdString() << endl;
            return false;
        }
    }

    cout << "Qt Assistant is now running" << endl;

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
                cout << "Qt Assistant process terminated: exitCode=" << exitCode
                    << ", exitStatus=" << static_cast<int>(exitStatus) << endl;
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
        qDebug() << __PRETTY_FUNCTION__ << "sending cmd" << cmd << "to QtAssistant";
        int res = d->process->write(cmd.toLocal8Bit() + '\n');

        if (res != -1)
            return true;

        qDebug() << __PRETTY_FUNCTION__ << "cmd =" << cmd << ", result=" << res;
    }

    return false;
}

} // end namespace mvme
} // end namespace mesytec
