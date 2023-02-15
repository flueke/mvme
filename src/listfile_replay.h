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
#ifndef __MVME_LISTFILE_REPLAY_H__
#define __MVME_LISTFILE_REPLAY_H__

#include "libmvme_export.h"

#include <memory>
#include <quazip.h>
#include <QDebug>

#include "globals.h"
#include "vme_config.h"

struct LIBMVME_EXPORT ListfileReplayHandle
{
    // The actual listfile. This is a file inside the archive if replaying from
    // ZIP. As long as this file is open no other file member of the archive
    // can be opened. This is a restriction of the ZIP library.
    // If replaying from flat file this is a plain QFile instance.
    // XXX: Not used for MVLC listfiles stored inside ZIP archives. Still used
    // for flat MVLC listfiles.
    std::unique_ptr<QIODevice> listfile;

    // The ZIP archive containing the listfile or nullptr if playing directly
    // from a listfile.
    std::unique_ptr<QuaZip> archive;

    // Format of the data stored in the listfile. Detected by looking at the
    // first 8 bytes of the file. Defaults to the old MVMELST format if none of
    // the newer MVLC types match.
    ListfileBufferFormat format;

    QString inputFilename;      // For ZIP archives this is the name of the ZIP file.
                                // For raw listfiles it's the filename that was
                                // passed to open_listfile().

    QString listfileFilename;   // For ZIP archives it's the name of the
                                // listfile inside the archive. Otherwise the
                                // same as inputFilename.

    QByteArray messages;        // Contents of messages.log if found.
    QByteArray analysisBlob;    // Analysis config contents if present in the archive.
    QString runNotes;           // Contents of the mvme_run_notes.txt file stored in the archive.

    ListfileReplayHandle() = default;

    ~ListfileReplayHandle()
    {
        // This needs to be done manually because the default destruction order
        // will destroy the QuaZIP archive member first which will close any
        // open file inside the archive. Thus the QuaZipFile data stored in the
        // listfile member will become invalid.
        if (listfile)
            listfile->close();
    }

    ListfileReplayHandle(ListfileReplayHandle &&) = default;
    ListfileReplayHandle &operator=(ListfileReplayHandle &&) = default;

    ListfileReplayHandle(const ListfileReplayHandle &) = delete;
    ListfileReplayHandle &operator=(const ListfileReplayHandle &) = delete;
};

// IMPORTANT: throws QString on error :-(
ListfileReplayHandle LIBMVME_EXPORT open_listfile(const QString &filename);

std::pair<std::unique_ptr<VMEConfig>, std::error_code>
LIBMVME_EXPORT read_vme_config_from_listfile(
        ListfileReplayHandle &handle,
        std::function<void (const QString &msg)> logger = {});

#endif /* __MVME_LISTFILE_REPLAY_H__ */
