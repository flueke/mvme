#ifndef __UTIL_ZIP_H__
#define __UTIL_ZIP_H__

#include <memory>
#include <quazip.h>
#include <quazipfile.h>

QString make_zip_error_string(const QString &message, const QuaZip *zip);
QString make_zip_error_string(const QString &message, QuaZipFile *zipFile);
std::runtime_error make_zip_error(const QString &msg, const QuaZip &zip);

/* Note: this is very inefficient for ZIP files and should be used sparingly
 * and only to look for a position near the start of the file. */
bool seek_in_file(QIODevice *input, qint64 pos);
QString get_filename(const QIODevice *dev);

void throw_io_device_error(QIODevice *device);
void throw_io_device_error(std::unique_ptr<QIODevice> &device);


#endif /* __UTIL_ZIP_H__ */
