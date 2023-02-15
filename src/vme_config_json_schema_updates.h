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
#ifndef __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_H__
#define __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_H__

#include <functional>
#include <QJsonObject>

namespace mvme
{
namespace vme_config
{
namespace json_schema
{
using Logger = std::function<void (const QString &msg)>;

struct SchemaUpdateOptions
{
    // If set to true the code adding VME Script variables to EventConfigs and
    // modifying VME Scripts to use these variables is skipped. Required
    // structural updates will still be done.
    bool skip_v4_VMEScriptVariableUpdate = false;
};

// Conversion from older VMEConfig JSON formats to the latest version.
// The given JSON must be the root of a VMEConfig object.
QJsonObject convert_vmeconfig_to_current_version(
    QJsonObject json, Logger logger, const SchemaUpdateOptions &options /* = {} */);

void set_vmeconfig_version(QJsonObject &json, int version);
int get_vmeconfig_version(const QJsonObject &json);

} // end namespace json_schema
} // end namespace vme_config
} // end namespace mvme

#endif /* __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_H__ */
