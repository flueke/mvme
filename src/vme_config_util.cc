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
#include "vme_config_util.h"

#include <QFileDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSet>
#include <QStandardPaths>

#include "vme.h"

namespace
{

u8 get_next_mcst(const VMEConfig *vmeConfig)
{
    QVector<unsigned> mcsts;
    for (auto event: vmeConfig->getEventConfigs())
    {
        auto vars = event->getVariables();
        mcsts.push_back(vars["mesy_mcst"].value.toUInt(nullptr, 16));
    }

    std::sort(mcsts.begin(), mcsts.end());

    u8 result = 0;

    if (!mcsts.isEmpty())
        result = mcsts.back() + 1;

    if (result == 0)
        result = 0xbb;

    return result;
}

} // end anon namespace

namespace mvme
{
namespace vme_config
{

u8 get_next_free_irq(const VMEConfig *vmeConfig)
{
    QSet<u8> irqsInUse;

    for (auto event: vmeConfig->getEventConfigs())
    {
        if (event->triggerCondition == TriggerCondition::Interrupt)
            irqsInUse.insert(event->irqLevel);
    }

    for (u8 irq=vme::MinIRQ; irq <= vme::MaxIRQ; ++irq)
        if (!irqsInUse.contains(irq))
            return irq;

    return 0u;
}

vme_script::SymbolTable make_standard_event_variables(u8 irq, u8 mcst)
{
    vme_script::SymbolTable vars;

    vars["sys_irq"] = vme_script::Variable(
        QString::number(irq), {},
        "IRQ value set for the VME Controller for this event.");

    vars["mesy_mcst"] = vme_script::Variable(
        QString::number(mcst, 16), {},
        "The most significant byte of the 32-bit multicast address to be used by this event.");

    vars["mesy_readout_num_events"] = vme_script::Variable(
        "1", {},
        "Number of events to read out in each cycle.");

    vars["mesy_eoe_marker"] = vme_script::Variable(
        "1", {},
        "EndOfEvent marker for mesytec modules (0: eventcounter, 1: timestamp, 3: extended_timestamp).");

    return vars;
}

std::unique_ptr<EventConfig> make_new_event_config(const VMEConfig *vmeConfig)
{
    auto eventConfig = std::make_unique<EventConfig>();

    u8 irq = get_next_free_irq(vmeConfig);
    u8 mcst = get_next_mcst(vmeConfig);

    // If there's no free irq reuse the first valid one.
    if (irq == 0) irq = vme::MinIRQ;;

    eventConfig->setObjectName(QString("event%1").arg(vmeConfig->getEventConfigs().size()));
    eventConfig->triggerCondition = TriggerCondition::Interrupt;
    eventConfig->irqLevel = irq;

    auto vars = make_standard_event_variables(irq, mcst);
    eventConfig->setVariables(vars);

    return eventConfig;
}

QString get_mime_type(const ConfigObject *obj)
{
    const bool isContainer = qobject_cast<const ContainerObject *>(obj);
    const bool isEvent = qobject_cast<const EventConfig *>(obj);
    const bool isModule = qobject_cast<const ModuleConfig *>(obj);
    const bool isScript = qobject_cast<const VMEScriptConfig *>(obj);

    if (isScript)
        return MIMEType_JSON_VMEScriptConfig;

    if (isModule)
        return MIMEType_JSON_VMEModuleConfig;

    if (isEvent)
        return MIMEType_JSON_VMEEventConfig;

    if (isContainer)
        return MIMEType_JSON_ContainerObject;

    return {};
}

bool can_mime_copy_object(const ConfigObject *obj)
{
    return !get_mime_type(obj).isNull();
}

bool contains_object_mime_type(const QMimeData *mimeData)
{
    static const auto types =
    {
        MIMEType_JSON_ContainerObject,
        MIMEType_JSON_VMEEventConfig,
        MIMEType_JSON_VMEModuleConfig,
        MIMEType_JSON_VMEScriptConfig
    };

    assert(mimeData);

    for (auto mimeType: types)
    {
        if (mimeData->hasFormat(mimeType))
            return true;
    }

    return false;
}

std::unique_ptr<QMimeData> make_mime_data(const ConfigObject *obj)
{
    if (!can_mime_copy_object(obj))
        return {};

    const auto mimeType = get_mime_type(obj);
    assert(!mimeType.isEmpty());

    // Let the object create its JSON representation, then wrap it in another
    // JSON object using the objects MIME type as the key. This way when
    // copy/pasting as text the object type can still be easily identified.
    QJsonObject objJson;
    obj->write(objJson);

    QJsonObject rootJson;
    rootJson[mimeType] = objJson;

    auto buffer = QJsonDocument(rootJson).toJson();

    auto mimeData = std::make_unique<QMimeData>();
    mimeData->setData(mimeType, buffer);
    mimeData->setText(buffer);

    return mimeData;
}

template<typename T>
std::unique_ptr<T> make_object(const QString &mimeType, const QMimeData *mimeData)
{
    if (!mimeData->hasFormat(mimeType))
        return {};

    auto objData = mimeData->data(mimeType);
    auto objJson = QJsonDocument::fromJson(objData).object().value(mimeType).toObject();
    auto obj = std::make_unique<T>();
    obj->read(objJson);
    return obj;
}

std::unique_ptr<ConfigObject> make_object_from_mime_data(const QMimeData *mimeData)
{
    if (auto obj = make_object<ContainerObject>(MIMEType_JSON_ContainerObject, mimeData))
        return obj;

    if (auto obj = make_object<EventConfig>(MIMEType_JSON_VMEEventConfig, mimeData))
        return obj;

    if (auto obj = make_object<ModuleConfig>(MIMEType_JSON_VMEModuleConfig, mimeData))
        return obj;

    if (auto obj = make_object<VMEScriptConfig>(MIMEType_JSON_VMEScriptConfig, mimeData))
        return obj;

    return {};
}

std::unique_ptr<ConfigObject> make_object_from_json_text(const QByteArray &jsonText)
{
    auto jsonDoc = QJsonDocument::fromJson(jsonText);

    if (jsonDoc.isNull())
        return {};

    auto rootObj = jsonDoc.object();

    if (rootObj.contains(MIMEType_JSON_VMEEventConfig))
    {
        auto obj = std::make_unique<EventConfig>();
        obj->read(rootObj.value(MIMEType_JSON_VMEEventConfig).toObject());
        return obj;
    }

    if (rootObj.contains(MIMEType_JSON_VMEModuleConfig))
    {
        auto obj = std::make_unique<ModuleConfig>();
        obj->read(rootObj.value(MIMEType_JSON_VMEModuleConfig).toObject());
        return obj;
    }

    if (rootObj.contains(MIMEType_JSON_VMEScriptConfig))
    {
        auto obj = std::make_unique<VMEScriptConfig>();
        obj->read(rootObj.value(MIMEType_JSON_VMEScriptConfig).toObject());
        return obj;
    }

    return {};
}

std::unique_ptr<ConfigObject> make_object_from_mime_data_or_json_text(
    const QMimeData *mimeData)
{
    if (auto obj = make_object_from_mime_data(mimeData))
        return obj;

    if (mimeData->hasFormat("application/json"))
        if (auto obj = make_object_from_json_text(mimeData->data("application/json")))
            return obj;

    if (mimeData->hasFormat("text/plain"))
        if (auto obj = make_object_from_json_text(mimeData->data("text/plain")))
            return obj;

    return {};
}

void generate_new_object_ids(ConfigObject *root)
{
    assert(root);
    root->generateNewId();
    auto children = root->findChildren<ConfigObject *>();
    std::for_each(children.begin(), children.end(),
                  [] (ConfigObject *child) { generate_new_object_ids(child);; });
}

TriggerCondition trigger_condition_from_string(const QString &str)
{
        auto it = std::find_if(
            TriggerConditionNames.begin(), TriggerConditionNames.end(),
            [str](const auto &testName) {
            return str == testName;
        });

        return (it == TriggerConditionNames.end()
                ? TriggerCondition::Interrupt
                : it.key());
}

QString trigger_condition_to_string(const TriggerCondition &str)
{
    return TriggerConditionNames.value(str);
}

QJsonDocument serialize_vme_config_to_json_document(const VMEConfig &config)
{
    QJsonObject configJson;
    config.write(configJson);

    QJsonObject outerJson;
    outerJson["DAQConfig"] = configJson;

    return QJsonDocument(outerJson);
}

bool serialize_vme_config_to_device(QIODevice &out, const VMEConfig &config)
{
    auto doc = serialize_vme_config_to_json_document(config);
    return out.write(doc.toJson()) >= 0;
}

std::unique_ptr<ModuleConfig> moduleconfig_from_modulejson(const QJsonObject &json)
{
    auto mod = std::make_unique<ModuleConfig>();
    load_moduleconfig_from_modulejson(*mod, json);
    return mod;
}

void LIBMVME_EXPORT load_moduleconfig_from_modulejson(ModuleConfig &mod, const QJsonObject &json)
{
    mod.read(json["ModuleConfig"].toObject());
    mvme::vme_config::generate_new_object_ids(&mod);
    auto mm = vats::modulemeta_from_json(json["ModuleMeta"].toObject());
    mod.setModuleMeta(mm);
    mod.setObjectName(mm.typeName);

    // Restore the variables on the module instance.
    // FIXME: why is this needed? the variables should have been stored when the
    // module was saved and reloaded when mod.read() was called.
    for (auto it=mm.variables.begin(); it!=mm.variables.end(); ++it)
    {
        auto varJ = it->toObject();
        auto varName = varJ["name"].toString();
        vme_script::Variable var;
        var.value = varJ["value"].toString();
        var.comment = varJ["comment"].toString();
        mod.setVariable(varName, var);
    }
}

std::unique_ptr<EventConfig> eventconfig_from_eventjson(const QJsonObject &json)
{
    auto ev = std::make_unique<EventConfig>();
    load_eventconfig_from_eventjson(*ev, json);
    return ev;
}

void load_eventconfig_from_eventjson(EventConfig &ev, const QJsonObject &json)
{
    ev.read(json["EventConfig"].toObject());
    mvme::vme_config::generate_new_object_ids(&ev);
}

bool gui_save_vme_script_config_to_file(const VMEScriptConfig *script, QWidget *dialogParent)
{
    assert(script);
    return gui_save_vme_script_to_file(
        script->getScriptContents(),
        script->objectName(),
        dialogParent);
}

bool gui_save_vme_script_to_file(const QString &scriptText, const QString &proposedFilename, QWidget *dialogParent)
{
    QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QSettings settings;

    if (settings.contains("Files/LastVMEScriptDirectory"))
        path = settings.value("Files/LastVMEScriptDirectory").toString();

    if (!proposedFilename.isEmpty())
        path += "/" + proposedFilename;

    if (QFileInfo(path).completeSuffix().isEmpty())
        path += ".vmescript";

    QString fileName = QFileDialog::getSaveFileName(
        dialogParent,
        QSL("Save vme script file"),
        path,
        QSL("VME scripts (*.vmescript);; All Files (*)"));

    if (fileName.isEmpty())
        return false;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(dialogParent, "File error", QString("Error opening \"%1\" for writing").arg(fileName));
        return false;
    }

    QTextStream stream(&file);
    stream << scriptText;

    if (stream.status() != QTextStream::Ok)
    {
        QMessageBox::critical(dialogParent, "File error", QString("Error writing to \"%1\"").arg(fileName));
        return false;
    }

    settings.setValue("Files/LastVMEScriptDirectory", QFileInfo(fileName).absolutePath());

    return true;
}

} // end namespace vme_config
} // end namespace mvme
