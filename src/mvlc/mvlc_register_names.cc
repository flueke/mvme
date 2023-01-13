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
#include "mvlc/mvlc_register_names.h"
#include <mutex>

namespace mesytec
{
namespace mvme_mvlc
{

const std::map<u16, std::string> &get_addr_2_name_map()
{
    static const std::map<u16, std::string> mapping =
    {
        { 0x4400, "own_ip_lo" },
        { 0x4402, "own_ip_hi" },
        { 0x4404, "StoreIPInFlash" },

        { 0x4406, "dhcp_active" },
        { 0x4408, "dhcp_ip_lo" },
        { 0x440a, "dhcp_ip_hi" },

        { 0x440c, "cmd_ip_lo" },
        { 0x440e, "cmd_ip_hi" },

        { 0x4410, "data_ip_lo" },
        { 0x4412, "data_ip_hi" },

        { 0x4414, "cmd_mac_0" },
        { 0x4416, "cmd_mac_1" },
        { 0x4418, "cmd_mac_2" },

        { 0x441a, "cmd_dest_port" },
        { 0x441c, "data_dest_port" },

        { 0x441e, "data_mac_0" },
        { 0x4420, "data_mac_1" },
        { 0x4422, "data_mac_2" },

        { 0x4424, "crc_good_ctr" },
        { 0x4426, "crc_bad_ctr" },
        { 0x4428, "skip_receive_frame_ctr" },
        { 0x442a, "receive_arp_ctr" },
        { 0x442c, "receive_ping_ctr" },
        { 0x442e, "receive_datin_ctr" },
        { 0x4430, "receive_cmdin_ctr" },

        { 0x4432, "arp_sender_mac_rx_0" },
        { 0x4434, "arp_sender_mac_rx_1" },
        { 0x4436, "arp_sender_mac_rx_2" },

        { 0x4438, "arp_sender_ip_rx_lo" },
        { 0x443a, "arp_sender_ip_rx_hi" },
    };

    return mapping;
}

const std::map<std::string, u16> &get_name_2_addr_map()
{
    static std::map<std::string, u16> reverse_mapping;
    static std::mutex mux;

    std::lock_guard<std::mutex> guard(mux);

    if (reverse_mapping.empty())
    {
        for (const auto &kv: get_addr_2_name_map())
        {
            reverse_mapping[kv.second] = kv.first;
        }
    }

    return reverse_mapping;
}

#ifdef QT_CORE_LIB
const QMap<u16, QString> &get_addr_2_name_qmap()
{
    static QMap<u16, QString> mapping;
    static std::mutex mux;

    std::lock_guard<std::mutex> guard(mux);

    if (mapping.isEmpty())
    {
        for (const auto &kv: get_addr_2_name_map())
        {
            mapping[kv.first] = QString::fromStdString(kv.second);
        }
    }

    return mapping;
}

const QMap<QString, u16> &get_name_2_addr_qmap()
{
    static QMap<QString, u16> mapping;
    static std::mutex mux;

    std::lock_guard<std::mutex> guard(mux);

    if (mapping.isEmpty())
    {
        for (const auto &kv: get_name_2_addr_map())
        {
            mapping[QString::fromStdString(kv.first)] = kv.second;
        }
    }

    return mapping;
}

#endif

} // end namespace mvme_mvlc
} // end namespace mesytec
