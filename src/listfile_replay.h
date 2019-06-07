#ifndef __MVME_LISTFILE_REPLAY_H__
#define __MVME_LISTFILE_REPLAY_H__

#include "libmvme_export.h"

#include <memory>
#include <quazip.h>

struct LIBMVME_EXPORT ListfileReplayInfo
{
    // The ZIP archive containing the listfile or nullptr if playing directly
    // from a listfile.
    std::unique_ptr<QuaZip> archive;

    // The actual listfile. This is a file inside the archive if replaying from
    // ZIP. As long as this file is open no other file member of the archive
    // can be opened. This is a restriction of the ZIP library.
    std::unique_ptr<QIODevice> listfile;

    QString inputFilename;      // For ZIP archives this is the name of the ZIP file.
                                // For raw listfiles it's the filename that was
                                // passed to open_listfile().

    QString listfileFilename;   // For ZIP archives it's the name of the
                                // listfile inside the archive. Otherwise the
                                // same as listfileFilename.

    QByteArray messages;        // messages.log if found
    QByteArray analysisBlob;    // analysis config contents if present in the archive
    QString analysisFilename;   // analysis filename inside the archive
};

// IMPORTANT: throws QString on error :-(
ListfileReplayInfo open_listfile(const QString &filename);

#endif /* __MVME_LISTFILE_REPLAY_H__ */
