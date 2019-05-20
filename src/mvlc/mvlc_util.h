#ifndef __MVME_MVLC_UTIL_H__
#define __MVME_MVLC_UTIL_H__

#include <iomanip>
#include "libmvme_mvlc_export.h"
#include "mvlc/mvlc_constants.h"
#include "vme_script.h"

namespace mesytec
{
namespace mvlc
{

// vme_script -> mvlc constant
LIBMVME_MVLC_EXPORT VMEDataWidth convert_data_width(vme_script::DataWidth width);

// mvlc constant -> vme_script
LIBMVME_MVLC_EXPORT vme_script::DataWidth convert_data_width(VMEDataWidth dataWidth);

// Returns the raw stack without any interleaved super (upload) commands.
// The stack result will be written to the given output pipe.
LIBMVME_MVLC_EXPORT QVector<u32> build_stack(const vme_script::VMEScript &script,
                                        u8 outPipe);

// Returns a Command Buffer List which writes the contents of the given stack
// or VMEScript to the MVLC stack memory area.
LIBMVME_MVLC_EXPORT QVector<u32> build_upload_commands(
    const vme_script::VMEScript &script,
    u8 outPipe,
    u16 startAddress);

LIBMVME_MVLC_EXPORT QVector<u32> build_upload_commands(
    const QVector<u32> &stack,
    u16 startAddress);

// Same as build_upload_commands but the returned list will be enclosed in
// CmdBufferStart and CmdBufferEnd. This is a form that can be parsed by the MVLC.
LIBMVME_MVLC_EXPORT QVector<u32> build_upload_command_buffer(
    const vme_script::VMEScript &script, u8 outPipe,
    u16 startAddress);

// Same as build_upload_command_buffer but instead of taking a VMEScript to
// build the stack data from this overload takes in raw (stack) data to be
// uploaded.
LIBMVME_MVLC_EXPORT QVector<u32> build_upload_command_buffer(
    const QVector<u32> &stack, u16 startAddress);

struct HeaderInfo
{
    u16 len;
    u8 type;
    u8 flags;
    u8 stack;
};

inline HeaderInfo extract_header_info(u32 header)
{
    using namespace buffer_headers;

    HeaderInfo result;

    result.len   = (header >> LengthShift) & LengthMask;
    result.type  = (header >> TypeShift) & TypeMask;
    result.flags = (header >> BufferFlagsShift) & BufferFlagsMask;
    result.stack = (header >> StackNumShift) & StackNumMask;

    return result;
}

//LIBMVME_MVLC_EXPORT const std::map<u32, std::string> &get_super_command_table();
//LIBMVME_MVLC_EXPORT const std::map<u32, std::string> &get_stack_command_table();

LIBMVME_MVLC_EXPORT QString decode_response_header(u32 header);

LIBMVME_MVLC_EXPORT void log_buffer(const u32 *buffer, size_t size, const std::string &info = {});
LIBMVME_MVLC_EXPORT void log_buffer(const std::vector<u32> &buffer, const std::string &info = {});
LIBMVME_MVLC_EXPORT void log_buffer(const QVector<u32> &buffer, const QString &info = {});

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
