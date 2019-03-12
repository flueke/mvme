#ifndef __MVME_MVLC_UTIL_H__
#define __MVME_MVLC_UTIL_H__

#include <iomanip>
#include "libmvme_export.h"
#include "mvlc/mvlc_constants.h"
#include "vme_script.h"

namespace mesytec
{
namespace mvlc
{

// vme_script -> mvlc constant
LIBMVME_EXPORT AddressMode convert_amod(vme_script::AddressMode mode);
LIBMVME_EXPORT VMEDataWidth convert_data_width(vme_script::DataWidth width);

// mvlc constant -> vme_script
LIBMVME_EXPORT vme_script::AddressMode convert_amod(AddressMode amod);
LIBMVME_EXPORT vme_script::DataWidth convert_data_width(VMEDataWidth dataWidth);

// AddressMode classification
LIBMVME_EXPORT bool is_block_amod(AddressMode amod);

// Returns the raw stack without any interleaved super commands.
// The stack result will be written to the given output pipe.
LIBMVME_EXPORT QVector<u32> build_stack(const vme_script::VMEScript &script,
                                        u8 outPipe);

// Returns a Command Buffer List which writes the contents of the given stack
// or VMEScript to the MVLC stack memory area.
LIBMVME_EXPORT QVector<u32> build_upload_commands(
    const vme_script::VMEScript &script, u8 outPipe,
    u16 startAddress);

LIBMVME_EXPORT QVector<u32> build_upload_commands(const QVector<u32> &stack,
                                                  u16 startAddress);

// Same as build_upload_commands but the returned list will be enclosed in
// CmdBufferStart and CmdBufferEnd. This is a form that can be parsed by the MVLC.
LIBMVME_EXPORT QVector<u32> build_upload_command_buffer(
    const vme_script::VMEScript &script, u8 outPipe,
    u16 startAddress);

LIBMVME_EXPORT QVector<u32> build_upload_command_buffer(
    const QVector<u32> &stack, u16 startAddress);

LIBMVME_EXPORT void log_buffer(const u32 *buffer, size_t size, const std::string &info = {});
LIBMVME_EXPORT void log_buffer(const std::vector<u32> &buffer, const std::string &info = {});
LIBMVME_EXPORT void log_buffer(const QVector<u32> &buffer, const QString &info = {});

LIBMVME_EXPORT const std::map<u32, std::string> &get_super_command_table();
LIBMVME_EXPORT const std::map<u32, std::string> &get_stack_command_table();

template<typename Out>
void log_buffer(Out &out, const u32 *buffer, size_t size, const char *info)
{
    using std::endl;

    out << "begin " << info << " (size=" << size << ")" << endl;

    for (size_t i=0; i < size; i++)
    {
        out << "  0x"
            << std::setfill('0') << std::setw(8) << std::hex
            << buffer[i]
            << std::dec << std::setw(0)
            << endl
            ;
    }

    out << "end " << info << endl;
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_UTIL_H__ */
