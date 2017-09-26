#ifndef __UTIL_ZIP_H__
#define __UTIL_ZIP_H__

#include <quazip.h>
#include <quazipfile.h>

QString make_zip_error(const QString &message, const QuaZip *zip);
QString make_zip_error(const QString &message, QuaZipFile *zipFile);

#endif /* __UTIL_ZIP_H__ */
