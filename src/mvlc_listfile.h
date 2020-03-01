#ifndef __MVME_MVLC_LISTFILE_H__
#define __MVME_MVLC_LISTFILE_H__

#include <QByteArray>
#include <QIODevice>

#include "typedefs.h"

namespace mvlc_listfile
{

// Magic bytes at the start of the listfile. The terminating zero is not
// written to file, so the marker uses 8 bytes.
size_t get_filemagic_len();
const char *get_filemagic_eth();
const char *get_filemagic_usb();

QByteArray read_file_magic(QIODevice &listfile);
QByteArray read_vme_config_data(QIODevice &listfile);

}

#endif /* __MVME_MVLC_LISTFILE_H__ */
