#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_H__

#include <ios>
#include <system_error>
#include <vector>

#include "mesytec-mvlc_export.h"
#include "mvlc_constants.h"
#include "util/int_types.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

// Note: write, read and seek should throw std::runtime_error on error.
class ListfileHandle
{
    public:
        virtual ~ListfileHandle();

        virtual std::ios_base::openmode openMode() const = 0;

        virtual bool isOpen() const = 0;
        virtual bool atEnd() const = 0;

        virtual size_t write(const u8 *data, size_t size) = 0;
        virtual size_t read(u8 *data, size_t maxSize) = 0;

        virtual void seek(size_t pos) = 0;
        virtual void close() = 0;
};

class WriteHandle
{
    public:
        virtual ~WriteHandle();
        virtual size_t write(const u8 *data, size_t size) = 0;
};

// Constant magic bytes at the start of the listfile. The terminating zero is
// not written to file, so the markers use 8 bytes.
constexpr size_t get_filemagic_len();
constexpr const char *get_filemagic_eth();
constexpr const char *get_filemagic_usb();
constexpr const char *get_filemagic_multicrate();

// Seeks to the beginning of the listfile and attempts to read the first 8
// bytes containing the magic marker.
std::string read_file_magic(ListfileHandle &listfile);

// Seeks to the beginning of the listfile and starts reading the first
// SystemEvent section with the given subtype.
std::vector<u8> read_vme_config(
    ListfileHandle &listfile, u8 subType = system_event::subtype::MVLCConfig);

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_LISTFILE_H__ */
