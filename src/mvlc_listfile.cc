#include "mvlc_listfile.h"
#include "mvlc_constants.h"
#include "mvlc_util.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

ListfileHandle::~ListfileHandle()
{ }

WriteHandle::~WriteHandle()
{ }

constexpr size_t get_filemagic_len()
{
    constexpr size_t FileMagicLen = 8;
    return FileMagicLen;
}

constexpr const char *get_filemagic_eth()
{
    constexpr const char *FileMagic_ETH = "MVLC_ETH";
    return FileMagic_ETH;
}

constexpr const char *get_filemagic_usb()
{
    constexpr const char *FileMagic_USB = "MVLC_USB";
    return FileMagic_USB;
}

constexpr const char *get_filemagic_multicrate()
{
    constexpr const char *FileMagic_Multicrate = "MVLC_MUL";
    return FileMagic_Multicrate;
}

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

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec
