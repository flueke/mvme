#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_H__

#include <ios>
#include <system_error>
#include <vector>

#include "mesytec-mvlc_export.h"

#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "mvlc_readout.h"
#include "util/int_types.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

#if 0
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
// Seeks to the beginning of the listfile and attempts to read the first 8
// bytes containing the magic marker.
std::string read_file_magic(ListfileHandle &listfile);

// Seeks to the beginning of the listfile and starts reading the first
// SystemEvent section with the given subtype.
std::vector<u8> read_vme_config(
    ListfileHandle &listfile, u8 subType = system_event::subtype::MVLCConfig);

#endif

class MESYTEC_MVLC_EXPORT WriteHandle
{
    public:
        virtual ~WriteHandle();
        virtual size_t write(const u8 *data, size_t size) = 0;
};

class MESYTEC_MVLC_EXPORT ReadHandle
{
    public:
        virtual ~ReadHandle();
        virtual size_t read(u8 *dest, size_t maxSize) = 0;
        virtual void seek(size_t pos) = 0;
};

void MESYTEC_MVLC_EXPORT listfile_write_preamble(WriteHandle &lf_out, const CrateConfig &config);
void MESYTEC_MVLC_EXPORT listfile_write_magic(WriteHandle &lf_out, ConnectionType ct);
void MESYTEC_MVLC_EXPORT listfile_write_endian_marker(WriteHandle &lf_out);
void MESYTEC_MVLC_EXPORT listfile_write_vme_config(WriteHandle &lf_out, const CrateConfig &config);
void MESYTEC_MVLC_EXPORT listfile_write_system_event(
    WriteHandle &lf_out, u8 subtype,
    const u32 *buffp, size_t totalWords);

// Writes an empty system section
void MESYTEC_MVLC_EXPORT listfile_write_system_event(WriteHandle &lf_out, u8 subtype);

void MESYTEC_MVLC_EXPORT listfile_write_timestamp(WriteHandle &lf_out);

inline size_t listfile_write_raw(WriteHandle &lf_out, const u8 *buffer, size_t size)
{
    return lf_out.write(buffer, size);
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_LISTFILE_H__ */
