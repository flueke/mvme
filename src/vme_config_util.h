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
#ifndef __MVME_VME_CONFIG_UTIL_H__
#define __MVME_VME_CONFIG_UTIL_H__

#include <memory>
#include <QJsonDocument>
#include <QMimeData>

#include "libmvme_export.h"
#include "vme_config.h"
#include "multi_crate.h"

namespace mesytec::mvme::vme_config
{

// Creates a symbol table containing the standard vme event variables:
// sys_irq, mesy_mcst, mesy_reaodut_num_events, mesy_eoe_marker
vme_script::SymbolTable LIBMVME_EXPORT make_standard_event_variables(u8 irq = 1, u8 mcst = 0xbb);

// Factory funcion to setup a new EventConfig instance which is to be added to
// the given VMEConfig.
// - A new unique objectName is set on the returned value.
// - The trigger IRQ value is set to the first unused irq in the system or '0' if
//   all irqs are in use.
// - The set of default variables for the event is created: sys_irq, mesy_mcst,
//   mesy_readout_num_events, mesy_eoe_marker.

std::unique_ptr<EventConfig> LIBMVME_EXPORT make_new_event_config(const VMEConfig *parentVMEConfig);

// Returns the first unused irq number in the given config or 0 if there are no
// unused irqs.
u8 LIBMVME_EXPORT get_next_free_irq(const VMEConfig *vmeConfig);

//
// Copy/Paste and Drag/Drop support via QMimeData
//

static const QString MIMEType_JSON_ContainerObject = QSL("application/x-mvme-vmeconf-container");
static const QString MIMEType_JSON_VMEEventConfig  = QSL("application/x-mvme-vmeconf-event");
static const QString MIMEType_JSON_VMEModuleConfig = QSL("application/x-mvme-vmeconf-module");
static const QString MIMEType_JSON_VMEScriptConfig = QSL("application/x-mvme-vmeconf-script");

QString LIBMVME_EXPORT get_mime_type(const ConfigObject *obj);
bool LIBMVME_EXPORT can_mime_copy_object(const ConfigObject *obj);
bool LIBMVME_EXPORT contains_object_mime_type(const QMimeData *mimeData);
std::unique_ptr<QMimeData> LIBMVME_EXPORT make_mime_data(const ConfigObject *obj);
std::unique_ptr<ConfigObject> LIBMVME_EXPORT make_object_from_mime_data(const QMimeData *mimeData);
std::unique_ptr<ConfigObject> LIBMVME_EXPORT make_object_from_json_text(const QByteArray &jsonText);

// Combines the two above: the known MIME types are checked first, then
// "application/json" and finally "text/plain".
std::unique_ptr<ConfigObject> LIBMVME_EXPORT make_object_from_mime_data_or_json_text(
    const QMimeData *mimeData);

void LIBMVME_EXPORT generate_new_object_ids(ConfigObject *root);

TriggerCondition LIBMVME_EXPORT trigger_condition_from_string(const QString &str);
QString LIBMVME_EXPORT trigger_condition_to_string(const TriggerCondition &str);

// Serializes the VMEConfig to JSON and returns the resulting QJsonDocument.
QJsonDocument LIBMVME_EXPORT serialize_vme_config_to_json_document(const VMEConfig &config);

// Serializes the VMEConfig to JSON and writes the JSON string to the given QIODevice.
// Returns false on error. Use out.errorString() to retrieve error information.
bool LIBMVME_EXPORT serialize_vme_config_to_device(QIODevice &out, const VMEConfig &config);

QJsonDocument LIBMVME_EXPORT serialize_multicrate_config_to_json_document(const multi_crate::MulticrateVMEConfig &config);
bool LIBMVME_EXPORT serialize_multicrate_config_to_device(QIODevice &out, const multi_crate::MulticrateVMEConfig &config);

std::unique_ptr<ModuleConfig> LIBMVME_EXPORT moduleconfig_from_modulejson(const QJsonObject &json);
void LIBMVME_EXPORT load_moduleconfig_from_modulejson(ModuleConfig &dest, const QJsonObject &json);

std::unique_ptr<EventConfig> LIBMVME_EXPORT eventconfig_from_eventjson(const QJsonObject &json);
void LIBMVME_EXPORT load_eventconfig_from_eventjson(EventConfig &ev, const QJsonObject &json);
std::unique_ptr<EventConfig> LIBMVME_EXPORT eventconfig_from_file(const QString &filename);

bool LIBMVME_EXPORT gui_save_vme_script_config_to_file(const VMEScriptConfig *script, QWidget *dialogParent = nullptr);
bool LIBMVME_EXPORT gui_save_vme_script_to_file(const QString &scriptText, const QString &proposedFilename = {}, QWidget *dialogParent = nullptr);

template<typename ObjectType, typename StringType>
std::unique_ptr<ObjectType> LIBMVME_EXPORT configobject_from_json(const QJsonObject &json, const StringType &jsonRoot)
{
    auto result = std::make_unique<ObjectType>();
    result->read(json[jsonRoot].toObject());
    return result;
}

// Parses the given mvlc url and returns the VMEControllerType and controller
// settings compatible with VMEConfig::setVMEController(). Logic is similar to
// the mvlc_factory code.
std::pair<VMEControllerType, QVariantMap> LIBMVME_EXPORT mvlc_settings_from_url(const std::string &mvlcUrl);

std::unique_ptr<ConfigObject> LIBMVME_EXPORT deserialize_object(const QJsonObject &json);
QJsonDocument LIBMVME_EXPORT serialize_object(const ConfigObject *obj);

template<typename ConfigObjectType>
std::unique_ptr<ConfigObjectType> LIBMVME_EXPORT clone_config_object(const ConfigObjectType &source)
{
    QJsonObject json;
    source.write(json);
    auto result = std::make_unique<ConfigObjectType>();
    result->read(json);
    generate_new_object_ids(result.get());
    return result;
}

void LIBMVME_EXPORT move_module(ModuleConfig *module, EventConfig *destEvent, int destIndex);
void LIBMVME_EXPORT copy_module(ModuleConfig *module, EventConfig *destEvent, int destIndex);

inline void store_configobject_expanded_state(const QUuid &objectId, bool isExpanded)
{
    QSettings settings("vme_tree_ui_state.ini", QSettings::IniFormat);
    auto expandedObjects = settings.value("ExpandedObjects").toMap();

    if (isExpanded)
    {
        qDebug() << "ConfigObject expanded, id =" << objectId;
        expandedObjects.insert(objectId.toString(), true);
    }
    else
    {
        qDebug() << "ConfigObject collapsed, id =" << objectId;
        expandedObjects.remove(objectId.toString());
    }

    settings.setValue("ExpandedObjects", expandedObjects);
}

inline bool was_configobject_expanded(const QUuid &objectId)
{
    QSettings settings("vme_tree_ui_state.ini", QSettings::IniFormat);
    auto expandedObjects = settings.value("ExpandedObjects").toMap();
    return expandedObjects.value(objectId.toString(), false).toBool();
}

// Create a SymbolTable from the information stored in the given VMEModuleMeta object.
// Basically loads variables from the VMEModuleMeta JSON data and stores them in
// the resulting symbol table.
vme_script::SymbolTable LIBMVME_EXPORT variable_symboltable_from_module_meta(const vats::VMEModuleMeta &moduleMeta);

}

#endif /* __MVME_VME_CONFIG_UTIL_H__ */
