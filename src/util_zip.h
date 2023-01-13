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
