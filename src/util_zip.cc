#include "util_zip.h"

QString make_zip_error(const QString &message, const QuaZip *zip)
{
  auto m = QString("%1\narchive=%2, code=%3")
      .arg(message)
      .arg(zip->getZipName())
      .arg(zip->getZipError());

  return m;
}

QString make_zip_error(const QString &message, QuaZipFile *zipFile)
{
    auto m = QString("%1\narchive=%2, file=%3, code=%4")
        .arg(message)
        .arg(zipFile->getZipName())
        .arg(zipFile->getFileName())
        .arg(zipFile->getZipError())
        ;

    return m;
}
