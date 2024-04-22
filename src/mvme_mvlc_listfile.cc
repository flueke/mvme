/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <QJsonDocument>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "mvme_mvlc_listfile.h"
#include "util_zip.h"
#include "mvlc/mvlc_util.h"

using namespace mesytec::mvlc;
using namespace mesytec::mvme_mvlc;

namespace
{
    template<typename T>
    bool checked_read(QIODevice &input, T &dest)
    {
        ssize_t bytesRead = input.read(reinterpret_cast<char *>(&dest), sizeof(dest));

        return bytesRead == sizeof(dest);
    }

    bool is_mvme_config_frame(u32 frameHeader)
    {
        return (extract_frame_info(frameHeader).type == frame_headers::SystemEvent
            && system_event::extract_subtype(frameHeader) == system_event::subtype::MVMEConfig);
    }
}

namespace mesytec::mvme_mvlc
{

size_t get_filemagic_len()
{
    static const size_t FileMagicLen = 8;
    return FileMagicLen;
}

const char *get_filemagic_eth()
{
    static const char *FileMagic_ETH = "MVLC_ETH";
    return FileMagic_ETH;
}

const char *get_filemagic_usb()
{
    static const char *FileMagic_USB = "MVLC_USB";
    return FileMagic_USB;
}

QByteArray read_file_magic(QIODevice &listfile)
{
    if (!seek_in_file(&listfile, 0))
        return {};

    QByteArray buffer;
    buffer.resize(get_filemagic_len());

    if (listfile.read(buffer.data(), buffer.size()) != buffer.size())
        return {};

    return buffer;
}

QByteArray read_vme_config_data(QIODevice &listfile)
{
    if (!seek_in_file(&listfile, get_filemagic_len()))
        return {};

    QByteArray buffer;
    u32 frameHeader = 0u;

    // Find the first SystemEvent with subtype VMEConfig
    while (!listfile.atEnd())
    {
        if (!checked_read(listfile, frameHeader))
            return {};

        if (is_mvme_config_frame(frameHeader))
            break;

        // skip over the frame
        buffer.resize(extract_frame_info(frameHeader).len * sizeof(u32));

        if (listfile.read(buffer.data(), buffer.size()) != buffer.size())
            return {};
    }

    buffer.resize(0);

    while (!listfile.atEnd() && is_mvme_config_frame(frameHeader))
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
}

void listfile_write_mvme_config(
    mesytec::mvlc::listfile::WriteHandle &lf_out,
    u8 crateId,
    const VMEConfig &vmeConfig)
{
    QJsonObject json;
    vmeConfig.write(json);
    QJsonObject parentJson;
    parentJson["VMEConfig"] = json;
    QJsonDocument doc(parentJson);
    QByteArray bytes(doc.toJson());

    // Pad using spaces. The Qt JSON parser will handle this without error when
    // reading it back.
    while (bytes.size() % sizeof(u32))
        bytes.append(' ');

    mesytec::mvlc::listfile::listfile_write_system_event(
        lf_out, crateId, mesytec::mvlc::system_event::subtype::MVMEConfig,
        reinterpret_cast<const u32 *>(bytes.data()),
        bytes.size() / sizeof(u32));
}

} // end namespace mvme_mvlc_listfile
