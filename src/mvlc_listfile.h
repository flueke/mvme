#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_H__

#include <vector>

#include "mesytec-mvlc_export.h"
#include "util/int_types.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

class Archive
{
};

class FileInArchive
{
};

// Magic bytes at the start of the listfile. The terminating zero is not
// written to file, so the marker uses 8 bytes.
constexpr size_t get_filemagic_len();
constexpr const char *get_filemagic_eth();
constexpr const char *get_filemagic_usb();
constexpr const char *get_filemagic_multicrate();

std::vector<u8> read_file_magic(FileInArchive &listfile);
std::vector<u8> read_vme_config_data(FileInArchive &listfile);

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_LISTFILE_H__ */
