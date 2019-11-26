#ifndef __MVME_MVLC_LISTFILE_H__
#define __MVME_MVLC_LISTFILE_H__

#include <QByteArray>
#include <QIODevice>

#include "typedefs.h"

namespace mvlc_listfile
{

// Magic bytes at the start of the listfile. The terminating zero is not
// written so that the marker uses 8 bytes.
static const char *FileMagic_ETH = "MVLC_ETH";
static const char *FileMagic_USB = "MVLC_USB";
static const size_t FileMagicLen = 8;

QByteArray read_file_magic(QIODevice &listfile);
QByteArray read_vme_config_data(QIODevice &listfile);

}

#endif /* __MVME_MVLC_LISTFILE_H__ */
