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
#include "util_zip.h"
#include <cassert>

QString make_zip_error_string(const QString &message, const QuaZip *zip)
{
  auto m = QString("%1\narchive=%2, code=%3")
      .arg(message)
      .arg(zip->getZipName())
      .arg(zip->getZipError());

  return m;
}

QString make_zip_error_string(const QString &message, QuaZipFile *zipFile)
{
    auto m = QString("%1\narchive=%2, file=%3, code=%4")
        .arg(message)
        .arg(zipFile->getZipName())
        .arg(zipFile->getFileName())
        .arg(zipFile->getZipError())
        ;

    return m;
}

std::runtime_error make_zip_error(const QString &msg, const QuaZip &zip)
{
    auto m = QString("Error: archive=%1, error=%2")
        .arg(msg)
        .arg(zip.getZipError());

    return std::runtime_error(m.toStdString());
}

bool seek_in_file(QIODevice *input, qint64 pos)
{
    if (auto inZipFile = qobject_cast<QuaZipFile *>(input))
    {
        // Reopen the file then read until the desired position is reached.

        inZipFile->close();

        if (!inZipFile->open(QIODevice::ReadOnly))
            return false;

        while (pos > 0)
        {
            char c;

            if (inZipFile->read(&c, sizeof(c)) != sizeof(c))
                return false;

            --pos;
        }

        return true;
    }

    return input->seek(pos);
}

QString get_filename(const QIODevice *dev)
{
    assert(dev);

    if (auto inFile = qobject_cast<const QFile *>(dev))
        return inFile->fileName();

    if (auto inZipFile = qobject_cast<const QuaZipFile *>(dev))
        return inZipFile->getZipName();

    return QString();
}

void throw_io_device_error(QIODevice *device)
{
    if (auto zipFile = qobject_cast<QuaZipFile *>(device))
    {
        throw make_zip_error(zipFile->getZip()->getZipName(),
                             *(zipFile->getZip()));
    }
    else if (auto file = qobject_cast<QFile *>(device))
    {
        throw QString("Error: file=%1, error=%2")
            .arg(file->fileName())
            .arg(file->errorString())
            ;
    }
    else
    {
        throw QString("IO Error: %1")
            .arg(device->errorString());
    }
}

void throw_io_device_error(std::unique_ptr<QIODevice> &device)
{
    throw_io_device_error(device.get());
}
