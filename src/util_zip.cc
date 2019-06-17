#include "util_zip.h"

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
