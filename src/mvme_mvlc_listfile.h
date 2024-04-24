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
#ifndef __MVME_MVLC_LISTFILE_H__
#define __MVME_MVLC_LISTFILE_H__

#include <QByteArray>
#include <QIODevice>

#include "libmvme_export.h"
#include "typedefs.h"
#include "vme_config.h"
#include <mesytec-mvlc/mvlc_listfile.h>

namespace mesytec::mvme_mvlc
{

// Magic bytes at the start of the listfile. The terminating zero is not
// written to file, so the marker uses 8 bytes.
size_t get_filemagic_len();
LIBMVME_EXPORT const char * get_filemagic_eth();
LIBMVME_EXPORT const char * get_filemagic_usb();

LIBMVME_EXPORT QByteArray read_file_magic(QIODevice &listfile);
LIBMVME_EXPORT QByteArray read_vme_config_data(QIODevice &listfile);

void LIBMVME_EXPORT listfile_write_mvme_config(
    mesytec::mvlc::listfile::WriteHandle &lf_out,
    u8 crateId,
    const VMEConfig &vmeConfig);

// Works for ZipCreator and SplitZipCreator

template<typename ZipCreatorType>
void add_file_to_archive(ZipCreatorType &zipCreator, const QString &filename, const QByteArray &fileData)
{
    auto writeHandle = zipCreator->createZIPEntry(filename, 0); // uncompressed zip entry
    writeHandle->write(reinterpret_cast<const u8 *>(fileData.data()), fileData.size());
    zipCreator->closeCurrentEntry();
}

}

#endif /* __MVME_MVLC_LISTFILE_H__ */
