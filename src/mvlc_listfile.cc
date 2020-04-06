#include "mvlc_listfile.h"

#include <cassert>
#include <chrono>
#include <cstring>

#include "mvlc_constants.h"
#include "mvlc_util.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

#if 0
ListfileHandle::~ListfileHandle()
{ }

std::string read_file_magic(ListfileHandle &listfile)
{
    listfile.seek(0);
    std::vector<u8> buffer(get_filemagic_len());
    listfile.read(buffer.data(), buffer.size());
    std::string result;
    std::copy(buffer.begin(), buffer.end(), std::back_inserter(result));
    return result;
}

namespace
{

template<typename T>
bool checked_read(ListfileHandle &listfile, T &dest)
{
    size_t bytesRead = listfile.read(reinterpret_cast<u8 *>(&dest), sizeof(dest));
    return bytesRead == sizeof(dest);
}

} // end anon namespace

std::vector<u8> read_vme_config(ListfileHandle &listfile, u8 subType)
{
    auto is_wanted_frame = [subType] (u32 frameHeader)
    {
        return (extract_frame_info(frameHeader).type == frame_headers::SystemEvent
                && system_event::extract_subtype(frameHeader) == subType);
    };

    listfile.seek(get_filemagic_len());

    std::vector<u8> buffer;
    u32 frameHeader = 0u;

    // Find the first SystemEvent with subtype VMEConfig
    while (!listfile.atEnd())
    {
        if (!checked_read(listfile, frameHeader))
            return {};

        if (is_wanted_frame(frameHeader))
            break;

        // Skip to the next frame
        buffer.resize(extract_frame_info(frameHeader).len * sizeof(u32));

        if (listfile.read(buffer.data(), buffer.size()) != buffer.size())
            return {};
    }

    buffer.resize(0);

    // Read all the adjacent matching frames.
    while (!listfile.atEnd() && is_wanted_frame(frameHeader))
    {
        auto frameInfo = extract_frame_info(frameHeader);
        size_t frameBytes = frameInfo.len * sizeof(u32);
        auto offset = buffer.size();
        buffer.resize(buffer.size() + frameBytes);

        if (listfile.read(buffer.data() + offset, frameBytes) != frameBytes)
            return {};

        // Read in the next frame header
        if (!checked_read(listfile, frameHeader))
            break;
    }

    return buffer;
}
#endif

WriteHandle::~WriteHandle()
{ }

void listfile_write_preamble(WriteHandle &lf_out, const CrateConfig &config)
{
    listfile_write_magic(lf_out, config.connectionType);
    listfile_write_endian_marker(lf_out);
    listfile_write_vme_config(lf_out, config);
    listfile_write_timestamp(lf_out);
}

void listfile_write_magic(WriteHandle &lf_out, ConnectionType ct)
{
    const char *magic = nullptr;

    switch (ct)
    {
        case ConnectionType::ETH:
            magic = get_filemagic_eth();
            break;

        case ConnectionType::USB:
            magic = get_filemagic_usb();
            break;
    }

    listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(magic), std::strlen(magic));
}

void listfile_write_endian_marker(WriteHandle &lf_out)
{
    listfile_write_system_event(
        lf_out, system_event::subtype::EndianMarker,
        &system_event::EndianMarkerValue, 1);
}

void listfile_write_vme_config(WriteHandle &lf_out, const CrateConfig &config)
{
}

void listfile_write_system_event(
    WriteHandle &lf_out, u8 subtype,
    const u32 *buffp, size_t totalWords)
{
    //if (!lf_out.isOpen())
    //    return;

    if (totalWords == 0)
    {
        listfile_write_system_event(lf_out, subtype);
        return;
    }

    if (subtype > system_event::subtype::SubtypeMax)
        throw std::runtime_error("system event subtype out of range");

    const u32 *endp  = buffp + totalWords;

    while (buffp < endp)
    {
        unsigned wordsLeft = endp - buffp;
        unsigned wordsInSection = std::min(
            wordsLeft, static_cast<unsigned>(system_event::LengthMask));

        bool isLastSection = (wordsInSection == wordsLeft);

        u32 sectionHeader = (frame_headers::SystemEvent << frame_headers::TypeShift)
            | ((subtype & system_event::SubtypeMask) << system_event::SubtypeShift);

        if (!isLastSection)
            sectionHeader |= 0b1 << system_event::ContinueShift;

        sectionHeader |= (wordsInSection & system_event::LengthMask) << system_event::LengthShift;

        listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(&sectionHeader),
                           sizeof(sectionHeader));

        listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(buffp),
                           wordsInSection * sizeof(u32));

        buffp += wordsInSection;
    }

    assert(buffp == endp);
}

void listfile_write_system_event(WriteHandle &lf_out, u8 subtype)
{
    if (subtype > system_event::subtype::SubtypeMax)
        throw std::runtime_error("system event subtype out of range");

    u32 sectionHeader = (frame_headers::SystemEvent << frame_headers::TypeShift)
        | ((subtype & system_event::SubtypeMask) << system_event::SubtypeShift);

    listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(&sectionHeader),
                       sizeof(sectionHeader));
}

void listfile_write_timestamp(WriteHandle &lf_out)
{
    //if (!lf_out.isOpen())
    //    return;

    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    u64 timestamp = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

    listfile_write_system_event(lf_out, system_event::subtype::UnixTimestamp,
                                reinterpret_cast<u32 *>(&timestamp),
                                sizeof(timestamp) / sizeof(u32));
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec
