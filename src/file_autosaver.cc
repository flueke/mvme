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
#include "file_autosaver.h"

#include <cassert>
#include <QDebug>
#include <QTemporaryFile>
#include <QTime>

#include "qt_util.h"

FileAutoSaver::FileAutoSaver(Serializer serializer, const QString &outputFilename, s32 interval_ms,
                             QObject *parent)
    : QObject(parent)
    , m_serializer(serializer)
    , m_outputFilename(outputFilename)
    , m_timer(new QTimer(this))
{
    assert(m_serializer);

    m_timer->setInterval(interval_ms);

    connect(m_timer, &QTimer::timeout, this, &FileAutoSaver::saveNow);
}

void FileAutoSaver::start()
{
    if (isActive())
    {
        qDebug().noquote() << QTime::currentTime().toString()
            << "restarting" << objectName();
    }
    else
    {
        qDebug().noquote() << QTime::currentTime().toString()
            << "starting" << objectName();
    }

    m_timer->start();
}

void FileAutoSaver::stop()
{
    qDebug().noquote() << QTime::currentTime().toString()
        << "stopping" << objectName();

    m_timer->stop();
}

/* Note: nothing is race free here.
 * Also see
 * https://stackoverflow.com/questions/14935919/copy-file-even-when-destination-exists-in-qt
 * for a discussion about QFile and copying.
 */
void FileAutoSaver::saveNow()
{
    QTemporaryFile tempFile(QSL("mvme_autosave"));

    if (!tempFile.open())
    {
        emit writeError(m_outputFilename,
                        QSL("%1: Could not create temporary file")
                        .arg(objectName()));
        return;
    }

    auto data = m_serializer();

    //qDebug().noquote() << QTime::currentTime().toString()
    //    << objectName() << "writing" << data.size()
    //    << "bytes to temp file " << tempFile.fileName();

    if (tempFile.write(m_serializer()) == -1)
    {
        emit writeError(m_outputFilename,
                        QSL("%1: Could not write to temporary file %2")
                        .arg(objectName())
                        .arg(tempFile.fileName()));
        return;
    }

    tempFile.close(); // close to flush

    if (QFile::exists(m_outputFilename))
    {
        if (!QFile::remove(m_outputFilename))
        {
            emit writeError(m_outputFilename,
                            QSL("%1: Could not remove existing output file %2")
                            .arg(objectName())
                            .arg(m_outputFilename));
            return;
        }
    }

    if (!QFile::copy(tempFile.fileName(), m_outputFilename))
    {
        emit writeError(m_outputFilename,
                        QSL("%1: Could not copy temporary file %2 to output file %3")
                        .arg(objectName())
                        .arg(tempFile.fileName())
                        .arg(m_outputFilename)
                        );
        return;
    }

    //qDebug().noquote() << QTime::currentTime().toString()
    //    << objectName()
    //    << "copied temp file" << tempFile.fileName()
    //    << "to output file" << m_outputFilename;

    emit saved(m_outputFilename);
}

bool FileAutoSaver::removeOutputFile()
{
    if (QFile::exists(m_outputFilename))
    {
        return QFile::remove(m_outputFilename);
    }
    return true;
}
