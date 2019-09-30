/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "vme_config.h"

#include "CVMUSBReadoutList.h"
#include "qt_util.h"
#include "util/qt_metaobject.h"
#include "vme_controller.h"

#include <cmath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

using namespace vats;

namespace
{

class VMEConfigReadResultErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "vme_config_read_error";
    }

    std::string message(int ev) const override
    {
        switch (static_cast<VMEConfigReadResult>(ev))
        {
            case VMEConfigReadResult::NoError:
                return "No Error";

            case VMEConfigReadResult::VersionTooNew:
                return "The file was generated by a newer version of mvme. Please upgrade.";
        }

        return "unrecognized error";
    }
};

const VMEConfigReadResultErrorCategory theVMEConfigReadResultErrorCategory {};

} // end anon namespace

std::error_code make_error_code(VMEConfigReadResult r)
{
    return { static_cast<int>(r), theVMEConfigReadResultErrorCategory };
}

//
// ConfigObject
//
ConfigObject::ConfigObject(QObject *parent, bool watchDynamicProperties)
    : ConfigObject(parent)
{
    if (watchDynamicProperties)
        setWatchDynamicProperties(true);
}

ConfigObject::ConfigObject(QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
{
    connect(this, &QObject::objectNameChanged, this, [this] {
        setModified(true);
    });

    connect(this, &ConfigObject::enabledChanged, this, [this] {
        setModified(true);
    });
}

void ConfigObject::setModified(bool b)
{
    emit modified(b);

    if (m_modified != b)
    {
        //qDebug() << __PRETTY_FUNCTION__ << this << m_modified << "->" << b;
        m_modified = b;
        emit modifiedChanged(b);

    }

    if (b)
    {
        if (auto parentConfig = qobject_cast<ConfigObject *>(parent()))
            parentConfig->setModified(true);
    }
}

void ConfigObject::setEnabled(bool b)
{
    if (m_enabled != b)
    {
        m_enabled = b;
        emit enabledChanged(b);
    }
}

QString ConfigObject::getObjectPath() const
{
    if (objectName().isEmpty())
        return QString();

    auto parentConfig = qobject_cast<ConfigObject *>(parent());

    if (!parentConfig)
        return objectName();

    auto result = parentConfig->getObjectPath();

    if (!result.isEmpty())
        result += QChar('/');

    result += objectName();

    return result;
}

void ConfigObject::read(const QJsonObject &json)
{
    m_id = QUuid(json["id"].toString());
    if (m_id.isNull())
        m_id = QUuid::createUuid();

    setObjectName(json["name"].toString());
    setEnabled(json["enabled"].toBool(true));
    loadDynamicProperties(json["properties"].toObject(), this);

    read_impl(json);

    setModified(false);
}

void ConfigObject::write(QJsonObject &json) const
{
    json["id"]   = m_id.toString();
    json["name"] = objectName();
    json["enabled"] = m_enabled;

    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;

    write_impl(json);
}

bool ConfigObject::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == this && event->type() == QEvent::DynamicPropertyChange)
        setModified();
    return QObject::eventFilter(obj, event);
}

void ConfigObject::setWatchDynamicProperties(bool doWatch)
{
    if (doWatch && !m_eventFilterInstalled)
    {
        installEventFilter(this);
        m_eventFilterInstalled = true;
    }
    else if (!doWatch && m_eventFilterInstalled)
    {
        removeEventFilter(this);
        m_eventFilterInstalled = false;
    }
}

//
// ContainerObject
//
ContainerObject::ContainerObject(QObject *parent)
    : ConfigObject(parent)
{
}

void ContainerObject::write_impl(QJsonObject &json) const
{
    QJsonArray childArray;

    for (auto child: m_children)
    {
        QJsonObject childDataJson;
        child->write(childDataJson);

        QJsonObject childJson;
        childJson["class"] = getClassName(child);
        childJson["data"] = childDataJson;

        childArray.append(childJson);
    }

    json["children"] = childArray;
}

void ContainerObject::read_impl(const QJsonObject &json)
{
    auto childArray = json["children"].toArray();

    for (const auto &jval: childArray)
    {
        auto jobj = jval.toObject();
        auto className = jobj["class"].toString() + "*";

        //qDebug() << __PRETTY_FUNCTION__ << "className =" << className;

        auto typeId = QMetaType::type(className.toLocal8Bit().constData());

        if (typeId == QMetaType::UnknownType)
        {
            qWarning() << "ContainerObject::read_impl: No QMetaType defined for className ="
                << className.toLocal8Bit().constData()
                << ", skipping child entry.";

            continue;
        }

        QMetaType mt(typeId);

        if (mt.flags() & QMetaType::PointerToQObject)
        {
            auto metaObject = mt.metaObject();

            if (!metaObject)
            {
                qWarning() << "No QMetaObject for class" << className;
                continue;
            }

            auto rawChild = metaObject->newInstance();
            std::unique_ptr<QObject> memGuard(rawChild);
            auto child = qobject_cast<ConfigObject *>(rawChild);

            if (!rawChild)
            {
                qWarning() << "Could not create child object of class" << className;
                continue;
            }

            if (!child)
            {
                qWarning() << "Child object is not a subclass of ConfigObject: "
                    << rawChild << ", className =" << className;
                continue;
            }

            child->read(jobj["data"].toObject());
            addChild(child);
            memGuard.release();
        }
        // maybe TODO: implement the case for non-qobject metatypes using mt.create()
    }
}

//
// VMEScriptConfig
//
VMEScriptConfig::VMEScriptConfig(QObject *parent)
    : ConfigObject(parent)
{}

VMEScriptConfig::VMEScriptConfig(const QString &name, const QString &contents, QObject *parent)
    : ConfigObject(parent)
{
    setObjectName(name);
    setScriptContents(contents);
    setModified(false);
}

void VMEScriptConfig::setScriptContents(const QString &str)
{
    if (m_script != str)
    {
        m_script = str;
        setModified(true);
    }
}

void VMEScriptConfig::addToScript(const QString &str)
{
    m_script += str;
    setModified(true);
}

vme_script::VMEScript VMEScriptConfig::getScript(u32 baseAddress) const
{
    auto script = vme_script::parse(m_script, baseAddress);
    return script;
}

void VMEScriptConfig::read_impl(const QJsonObject &json)
{
    m_script = json["vme_script"].toString();
}

void VMEScriptConfig::write_impl(QJsonObject &json) const
{
    json["vme_script"] = m_script;
}

QString VMEScriptConfig::getVerboseTitle() const
{
    auto module     = qobject_cast<ModuleConfig *>(parent());
    auto event      = qobject_cast<EventConfig *>(parent());
    auto daqConfig  = qobject_cast<VMEConfig *>(parent());

    QString title;

    if (module)
    {
        title = QString("%1 for module %2")
            .arg(objectName())
            .arg(module->objectName());
    }
    else if (event)
    {
        title = QString("%1 for event %2")
            .arg(objectName())
            .arg(event->objectName());
    }
    else if (daqConfig)
    {
        title = QString("Global Script %2")
            .arg(objectName());
    }
    else
    {
        title = QString("VMEScript %1")
            .arg(objectName());
    }

    return title;
}

//
// ModuleConfig
//
ModuleConfig::ModuleConfig(QObject *parent)
    : ConfigObject(parent)
    , m_resetScript(new VMEScriptConfig(this))
    , m_readoutScript(new VMEScriptConfig(this))
{
}

void ModuleConfig::setBaseAddress(uint32_t address)
{
    if (address != m_baseAddress)
    {
        m_baseAddress = address;
        setModified();
    }
}

void ModuleConfig::setModuleMeta(const vats::VMEModuleMeta &meta)
{
    if (m_meta != meta)
    {
        m_meta = meta;
        setModified();
    }
}

void ModuleConfig::addInitScript(VMEScriptConfig *script)
{
    Q_ASSERT(script);

    script->setParent(this);
    m_initScripts.push_back(script);
    setModified(true);
}

VMEScriptConfig *ModuleConfig::getInitScript(const QString &scriptName) const
{
    auto it = std::find_if(m_initScripts.begin(), m_initScripts.end(),
                           [scriptName] (const VMEScriptConfig *config) {
                               return config->objectName() == scriptName;
                           });

    return (it != m_initScripts.end() ? *it : nullptr);
}

VMEScriptConfig *ModuleConfig::getInitScript(s32 scriptIndex) const
{
    return m_initScripts.value(scriptIndex, nullptr);
}

void ModuleConfig::read_impl(const QJsonObject &json)
{
    qDeleteAll(m_initScripts);
    m_initScripts.clear();

    QString typeName = json["type"].toString();

    const auto moduleMetas = read_templates().moduleMetas;
    auto it = std::find_if(moduleMetas.begin(), moduleMetas.end(), [typeName](const VMEModuleMeta &mm) {
        return mm.typeName == typeName;
    });

    m_meta = (it != moduleMetas.end() ? *it : VMEModuleMeta());

    // IMPORTANT: using json["baseAddress"].toInt() directly does not support
    // the full range of 32-bit unsigned integers!
    m_baseAddress = static_cast<u32>(json["baseAddress"].toDouble());

    m_resetScript->read(json["vmeReset"].toObject());
    m_readoutScript->read(json["vmeReadout"].toObject());

    auto initScriptsArray = json["initScripts"].toArray();

    for (auto it = initScriptsArray.begin();
         it != initScriptsArray.end();
         ++it)
    {
        auto cfg = new VMEScriptConfig(this);
        cfg->read(it->toObject());
        m_initScripts.push_back(cfg);
    }
}

void ModuleConfig::write_impl(QJsonObject &json) const
{
    json["type"] = m_meta.typeName;
    json["baseAddress"] = static_cast<qint64>(m_baseAddress);

    // readout script
    {
        QJsonObject dstObject;
        m_readoutScript->write(dstObject);
        json["vmeReadout"] = dstObject;
    }

    // reset script
    {
        QJsonObject dstObject;
        m_resetScript->write(dstObject);
        json["vmeReset"] = dstObject;
    }

    // init scripts
    {
        QJsonArray dstArray;
        for (auto scriptConfig: m_initScripts)
        {
            QJsonObject dstObject;
            scriptConfig->write(dstObject);
            dstArray.append(dstObject);
        }
        json["initScripts"] = dstArray;
    }
}

EventConfig *ModuleConfig::getEventConfig() const
{
    return qobject_cast<EventConfig *>(parent());
}

QUuid ModuleConfig::getEventId() const
{
    if (auto eventConfig = getEventConfig())
    {
        return eventConfig->getId();
    }

    return {};
}

//
// EventConfig
//

EventConfig::EventConfig(QObject *parent)
    : ConfigObject(parent)
{
    vmeScripts[QSL("daq_start")] = new VMEScriptConfig(this);
    vmeScripts[QSL("daq_start")]->setObjectName(QSL("DAQ Start"));

    vmeScripts[QSL("daq_stop")] = new VMEScriptConfig(this);
    vmeScripts[QSL("daq_stop")]->setObjectName(QSL("DAQ Stop"));

    vmeScripts[QSL("readout_start")] = new VMEScriptConfig(this);
    vmeScripts[QSL("readout_start")]->setObjectName(QSL("Cycle Start"));

    vmeScripts[QSL("readout_end")] = new VMEScriptConfig(this);
    vmeScripts[QSL("readout_end")]->setObjectName(QSL("Cycle End"));

    triggerOptions[QSL("sis3153.timer_period")] = 1.0;
}

void EventConfig::read_impl(const QJsonObject &json)
{
    qDeleteAll(modules);
    modules.clear();

    // triggerCondition and options
    {
        auto tcName = json["triggerCondition"].toString();
        auto it = std::find_if(TriggerConditionNames.begin(), TriggerConditionNames.end(),
                               [tcName](const auto &testName) {
            return tcName == testName;
        });

        // FIXME: report error on unknown trigger condition
        triggerCondition = (it != TriggerConditionNames.end()) ? it.key() : TriggerCondition::NIM1;
        triggerOptions = json["triggerOptions"].toObject().toVariantMap();
    }
    irqLevel = json["irqLevel"].toInt();
    irqVector = json["irqVector"].toInt();
    scalerReadoutPeriod = json["scalerReadoutPeriod"].toInt();
    scalerReadoutFrequency = json["scalerReadoutFrequency"].toInt();

    QJsonArray moduleArray = json["modules"].toArray();
    for (int i=0; i<moduleArray.size(); ++i)
    {
        QJsonObject moduleObject = moduleArray[i].toObject();
        ModuleConfig *moduleConfig = new ModuleConfig(this);
        moduleConfig->read(moduleObject);
        modules.append(moduleConfig);
    }

    for (auto scriptConfig: vmeScripts.values())
    {
        scriptConfig->setScriptContents(QString());
    }

    QJsonObject scriptsObject = json["vme_scripts"].toObject();

    for (auto it = scriptsObject.begin();
         it != scriptsObject.end();
         ++it)
    {
        if (vmeScripts.contains(it.key()))
        {
            vmeScripts[it.key()]->read(it.value().toObject());
        }
    }
}

void EventConfig::write_impl(QJsonObject &json) const
{
    json["triggerCondition"] = TriggerConditionNames.value(triggerCondition);
    json["triggerOptions"]   = QJsonObject::fromVariantMap(triggerOptions);
    json["irqLevel"] = irqLevel;
    json["irqVector"] = irqVector;
    json["scalerReadoutPeriod"] = scalerReadoutPeriod;
    json["scalerReadoutFrequency"] = scalerReadoutFrequency;



    QJsonArray moduleArray;

    for (auto module: modules)
    {
        QJsonObject moduleObject;
        module->write(moduleObject);
        moduleArray.append(moduleObject);
    }
    json["modules"] = moduleArray;

    QJsonObject scriptsObject;

    for (auto it = vmeScripts.begin();
         it != vmeScripts.end();
         ++it)
    {
        QJsonObject scriptJson;
        if (it.value())
        {
            it.value()->write(scriptJson);
            scriptsObject[it.key()] = scriptJson;
        }
    }

    json["vme_scripts"] = scriptsObject;
}

//
// VMEConfig
//

// Versioning of the DAQ config in case incompatible changes need to be made.
static const int CurrentDAQConfigVersion = 3;

/* Module script storage changed:
 * vme_scripts.readout              -> vmeReadout
 * vme_scripts.reset                -> vmeReset
 * vme_scripts.parameters           -> initScripts[0]
 * vme_scripts.readout_settings     -> initScripts[1]
 */
static QJsonObject v1_to_v2(QJsonObject json)
{
    qDebug() << "VME config conversion" << __PRETTY_FUNCTION__;

    auto eventsArray = json["events"].toArray();

    for (int eventIndex = 0;
         eventIndex < eventsArray.size();
         ++eventIndex)
    {
        QJsonObject eventJson = eventsArray[eventIndex].toObject();
        auto modulesArray = eventJson["modules"].toArray();

        for (int moduleIndex = 0;
             moduleIndex < modulesArray.size();
             ++moduleIndex)
        {
            QJsonObject moduleJson = modulesArray[moduleIndex].toObject();

            moduleJson["vmeReadout"] = moduleJson["vme_scripts"].toObject()["readout"];
            moduleJson["vmeReset"]   = moduleJson["vme_scripts"].toObject()["reset"];

            QJsonArray initScriptsArray;
            initScriptsArray.append(moduleJson["vme_scripts"].toObject()["parameters"]);
            initScriptsArray.append(moduleJson["vme_scripts"].toObject()["readout_settings"]);
            moduleJson["initScripts"] = initScriptsArray;

            modulesArray[moduleIndex] = moduleJson;
        }

        eventJson["modules"] = modulesArray;
        eventsArray[eventIndex] = eventJson;
    }

    json["events"] = eventsArray;

    return json;
}

/* Instead of numeric TriggerCondition values string representations are stored
 * now. */
static QJsonObject v2_to_v3(QJsonObject json)
{
    qDebug() << "VME config conversion" << __PRETTY_FUNCTION__;

    auto eventsArray = json["events"].toArray();

    for (int eventIndex = 0;
         eventIndex < eventsArray.size();
         ++eventIndex)
    {
        QJsonObject eventJson = eventsArray[eventIndex].toObject();

        auto triggerCondition = static_cast<TriggerCondition>(eventJson["triggerCondition"].toInt());
        eventJson["triggerCondition"] = TriggerConditionNames.value(triggerCondition);

        eventsArray[eventIndex] = eventJson;
    }

    json["events"] = eventsArray;

    return json;
}

using VMEConfigConverter = std::function<QJsonObject (QJsonObject)>;

static QVector<VMEConfigConverter> VMEConfigConverters =
{
    nullptr,
    v1_to_v2,
    v2_to_v3
};

static int get_version(const QJsonObject &json)
{
    return json["properties"].toObject()["version"].toInt(1);
};

static QJsonObject convert_vmeconfig_to_current_version(QJsonObject json)
{
    int version;

    while ((version = get_version(json)) < CurrentDAQConfigVersion)
    {
        auto converter = VMEConfigConverters.value(version);

        if (!converter)
            break;

        json = converter(json);
        json["properties"] = QJsonObject({{"version", version+1}});

        qDebug() << __PRETTY_FUNCTION__ << "converted VMEConfig from version"
            << version << "to version" << version+1;
    }

    return json;
}

VMEConfig::VMEConfig(QObject *parent)
    : ConfigObject(parent)
{
    setProperty("version", CurrentDAQConfigVersion);
}

std::error_code VMEConfig::readVMEConfig(const QJsonObject &json)
{
    int version = get_version(json);

    if (version > CurrentDAQConfigVersion)
    {
        return make_error_code(VMEConfigReadResult::VersionTooNew);
    }

    ConfigObject::read(json); // calls read_impl() on this

    return {};
}

void VMEConfig::addEventConfig(EventConfig *config)
{
    config->setParent(this);
    eventConfigs.push_back(config);
    emit eventAdded(config);
    setModified();
}

bool VMEConfig::removeEventConfig(EventConfig *config)
{
    bool ret = eventConfigs.removeOne(config);
    if (ret)
    {
        emit eventAboutToBeRemoved(config);
        config->setParent(nullptr);
        config->deleteLater();
        setModified();
    }

    return ret;
}

bool VMEConfig::contains(EventConfig *config)
{
    return eventConfigs.indexOf(config) >= 0;
}

void VMEConfig::addGlobalScript(VMEScriptConfig *config, const QString &category)
{
    config->setParent(this);
    vmeScriptLists[category].push_back(config);
    emit globalScriptAdded(config, category);
    setModified();
}

bool VMEConfig::removeGlobalScript(VMEScriptConfig *config)
{
    for (auto category: vmeScriptLists.keys())
    {
        if (vmeScriptLists[category].removeOne(config))
        {
            emit globalScriptAboutToBeRemoved(config);
            config->setParent(nullptr);
            config->deleteLater();
            setModified();
            return true;
        }
    }

    return false;
}

void VMEConfig::setVMEController(VMEControllerType type, const QVariantMap &settings)
{
    m_controllerType = type;

    //m_controllerSettings = settings;
    // Note: unite() doesn't work because it uses insertMulti() instead of
    // overwriting the values.

    for (const auto &key: settings.keys())
        m_controllerSettings[key] = settings.value(key);

    setModified();
}

void VMEConfig::addGlobalObject(ConfigObject *obj)
{
    m_globalObjects.addChild(obj);
}

bool VMEConfig::removeGlobalObject(ConfigObject *obj)
{
    return m_globalObjects.removeChild(obj);
}

QVector<ConfigObject *> VMEConfig::getGlobalObjects() const
{
    return m_globalObjects.getChildren();
}

const ContainerObject &VMEConfig::getGlobalObjectRoot() const
{
    return m_globalObjects;
}

ContainerObject &VMEConfig::getGlobalObjectRoot()
{
    return m_globalObjects;
}

void VMEConfig::write_impl(QJsonObject &json) const
{
    QJsonArray eventArray;
    for (auto event: eventConfigs)
    {
        QJsonObject eventObject;
        event->write(eventObject);
        eventArray.append(eventObject);
    }
    json["events"] = eventArray;

    // script objects
    QJsonObject scriptsObject;

    for (auto mapIter = vmeScriptLists.begin();
         mapIter != vmeScriptLists.end();
         ++mapIter)
    {
        const auto list(mapIter.value());

        QJsonArray scriptsArray;

        for (auto listIter = list.begin();
             listIter != list.end();
             ++listIter)
        {
            QJsonObject scriptsObject;
            (*listIter)->write(scriptsObject);
            scriptsArray.append(scriptsObject);
        }

        scriptsObject[mapIter.key()] = scriptsArray;
    }

    json["vme_script_lists"] = scriptsObject;

    // global objects
    {
        QJsonObject globalsJson;
        m_globalObjects.write(globalsJson);
        json["global_objects"] = globalsJson;
    }

    // vme controller
    QJsonObject controllerJson;
    controllerJson["type"] = to_string(m_controllerType);
    controllerJson["settings"] = QJsonObject::fromVariantMap(m_controllerSettings);
    json["vme_controller"] = controllerJson;
}

void VMEConfig::read_impl(const QJsonObject &inputJson)
{
    qDeleteAll(eventConfigs);
    eventConfigs.clear();

    QJsonObject json = convert_vmeconfig_to_current_version(inputJson);

    QJsonArray eventArray = json["events"].toArray();

    for (int eventIndex=0; eventIndex<eventArray.size(); ++eventIndex)
    {
        QJsonObject eventObject = eventArray[eventIndex].toObject();
        EventConfig *eventConfig = new EventConfig(this);
        eventConfig->read(eventObject);
        eventConfigs.append(eventConfig);
    }
    qDebug() << __PRETTY_FUNCTION__ << "read" << eventConfigs.size() << "event configs";

    // script objects
    QJsonObject scriptsObject = json["vme_script_lists"].toObject();

    for (auto it = scriptsObject.begin();
         it != scriptsObject.end();
         ++it)
    {
        auto &list(vmeScriptLists[it.key()]);

        QJsonArray scriptsArray = it.value().toArray();

        for (auto arrayIter = scriptsArray.begin();
             arrayIter != scriptsArray.end();
             ++arrayIter)
        {
            VMEScriptConfig *cfg(new VMEScriptConfig(this));
            cfg->read((*arrayIter).toObject());
            list.push_back(cfg);
        }
    }

    // global objects
    m_globalObjects.read(json["global_objects"].toObject());

    // vme controller
    auto controllerJson = json["vme_controller"].toObject();
    m_controllerType = from_string(controllerJson["type"].toString());
    m_controllerSettings = controllerJson["settings"].toObject().toVariantMap();
}

ModuleConfig *VMEConfig::getModuleConfig(int eventIndex, int moduleIndex) const
{
    ModuleConfig *result = nullptr;
    auto eventConfig = eventConfigs.value(eventIndex);

    if (eventConfig)
    {
        result = eventConfig->getModuleConfigs().value(moduleIndex);
    }

    return result;
}

EventConfig *VMEConfig::getEventConfig(const QString &name) const
{
    for (auto cfg: eventConfigs)
    {
        if (cfg->objectName() == name)
            return cfg;
    }
    return nullptr;
}

EventConfig *VMEConfig::getEventConfig(const QUuid &id) const
{
    for (auto cfg: eventConfigs)
    {
        if (cfg->getId() == id)
            return cfg;
    }
    return nullptr;
}

QList<ModuleConfig *> VMEConfig::getAllModuleConfigs() const
{
    QList<ModuleConfig *> result;

    for (auto eventConfig: eventConfigs)
    {
        for (auto moduleConfig: eventConfig->getModuleConfigs())
        {
            result.push_back(moduleConfig);
        }
    }

    return result;
}

QPair<int, int> VMEConfig::getEventAndModuleIndices(ModuleConfig *cfg) const
{
    for (int eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto moduleConfigs = eventConfigs[eventIndex]->getModuleConfigs();
        int moduleIndex = moduleConfigs.indexOf(cfg);
        if (moduleIndex >= 0)
            return qMakePair(eventIndex, moduleIndex);
    }

    return qMakePair(-1, -1);
}

std::pair<std::unique_ptr<VMEConfig>, QString>
    read_vme_config_from_file(const QString &filename)
{
    std::pair<std::unique_ptr<VMEConfig>, QString> result;

    QFile inFile(filename);
    if (!inFile.open(QIODevice::ReadOnly))
    {
        result.second = inFile.errorString();
        return result;
    }

    auto data = inFile.readAll();
    QJsonParseError parseError;
    QJsonDocument doc(QJsonDocument::fromJson(data, &parseError));

    if (parseError.error != QJsonParseError::NoError)
    {
        result.second = parseError.errorString();
        return result;
    }

    auto vmeConfig = std::make_unique<VMEConfig>();
    if (auto ec = vmeConfig->readVMEConfig(doc.object()["DAQConfig"].toObject()))
    {
        result.second = ec.message().c_str();
    }

    result.first  = std::move(vmeConfig);
    return result;
}

QString make_unique_module_name(const QString &prefix, const VMEConfig *vmeConfig)
{
    auto moduleConfigs = vmeConfig->getAllModuleConfigs();
    QSet<QString> moduleNames;

    for (auto cfg: moduleConfigs)
    {
        if (cfg->objectName().startsWith(prefix))
        {
            moduleNames.insert(cfg->objectName());
        }
    }

    QString result = prefix;
    u32 suffix = 1;
    while (moduleNames.contains(result))
    {
        result = QString("%1_%2").arg(prefix).arg(suffix++);
    }
    return result;
}
