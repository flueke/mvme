#include "mvlc_listfile.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

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

std::vector<u8> read_file_magic(FileInArchive &listfile)
{
#if 0
    if (!seek_in_file(&listfile, 0))
        return {};

    QByteArray buffer;
    buffer.resize(get_filemagic_len());

    if (listfile.read(buffer.data(), buffer.size()) != buffer.size())
        return {};

    return buffer;
#endif
    return {};
}

std::vector<u8> read_vme_config_data(FileInArchive &listfile)
{
#if 0
    if (!seek_in_file(&listfile, get_filemagic_len()))
        return {};

    QByteArray buffer;
    u32 frameHeader = 0u;

    // Find the first SystemEvent with subtype VMEConfig
    while (!listfile.atEnd())
    {
        if (!checked_read(listfile, frameHeader))
            return {};

        if (is_vme_config_frame(frameHeader))
            break;

        // skip over the frame
        buffer.resize(extract_frame_info(frameHeader).len * sizeof(u32));

        if (listfile.read(buffer.data(), buffer.size()) != buffer.size())
            return {};
    }

    buffer.resize(0);

    while (!listfile.atEnd() && is_vme_config_frame(frameHeader))
    {
        auto frameInfo = extract_frame_info(frameHeader);
        qint64 frameBytes = frameInfo.len * sizeof(u32);
        auto offset = buffer.size();
        buffer.resize(buffer.size() + frameBytes);

        if (listfile.read(buffer.data() + offset, frameBytes) != frameBytes)
            return {};

        // Read in the next frame header
        if (!checked_read(listfile, frameHeader))
            break;
    }

    return buffer;
#endif
    return {};
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec
