#include "mvme_config.h"
#include "vmecommandlist.h"
#include "CVMUSBReadoutList.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

//
// ConfigObject
//
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
        m_modified = b;
        emit modifiedChanged(b);

        if (b)
        {
            auto parentConfig = qobject_cast<ConfigObject *>(parent());

            if (parentConfig)
            {
                parentConfig->setModified(true);
            }
        }
    }
}

void ConfigObject::setEnabled(bool b)
{
    if (m_enabled != b)
    {
        qDebug() << __PRETTY_FUNCTION__ << b;
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
        result += QChar('.');

    result += objectName();

    qDebug() << __PRETTY_FUNCTION__ << this << result;

    return result;
}

void ConfigObject::read(const QJsonObject &json)
{
    m_id = QUuid(json["id"].toString());
    if (m_id.isNull())
        m_id = QUuid::createUuid();

    setObjectName(json["name"].toString());
    setEnabled(json["enabled"].toBool(true));

    read_impl(json);

    setModified(false);
}

void ConfigObject::write(QJsonObject &json) const
{
    json["id"]   = m_id.toString();
    json["name"] = objectName();
    json["enabled"] = m_enabled;

    write_impl(json);
}

//
// VariantMapConfig
//
void VariantMapConfig::set(const QString &key, const QVariant &value)
{
    if (!m_map.contains(key) || m_map[key] != value)
    {
        m_map[key] = value;
        setModified();
    }
}

QVariant VariantMapConfig::get(const QString &key)
{
    return m_map.value(key);
}

void VariantMapConfig::remove(const QString &key)
{
    if (m_map.remove(key) > 0)
        setModified();
}

void VariantMapConfig::read_impl(const QJsonObject &json)
{
    m_map = json["variantMap"].toObject().toVariantMap();
}

void VariantMapConfig::write_impl(QJsonObject &json) const
{
    json["variantMap"] = QJsonObject::fromVariantMap(m_map);
}

//
// VMEScriptConfig
//
void VMEScriptConfig::setScriptContents(const QString &str)
{
    if (m_script != str)
    {
        m_script = str;
        setModified(true);
    }
}

void VMEScriptConfig::read_impl(const QJsonObject &json)
{
    m_script = json["vme_script"].toString();
}

void VMEScriptConfig::write_impl(QJsonObject &json) const
{
    json["vme_script"] = m_script;
}

//
// ModuleConfig
//
ModuleConfig::ModuleConfig(QObject *parent)
    : ConfigObject(parent)
{
    vmeScripts["parameters"] = new VMEScriptConfig(this);
    vmeScripts["parameters"]->setObjectName(QSL("Module Init"));

    vmeScripts["readout_settings"] = new VMEScriptConfig(this);
    vmeScripts["readout_settings"]->setObjectName(QSL("VME Interface Settings"));

    vmeScripts["readout"] = new VMEScriptConfig(this);
    vmeScripts["readout"]->setObjectName(QSL("Readout"));
}

void ModuleConfig::updateRegisterCache()
{
    m_registerCache.clear();

    QString scriptString;

    auto scriptConfig = vmeScripts.value("parameters", nullptr);
    if (scriptConfig)
        scriptString += scriptConfig->getScriptContents();

    scriptConfig = vmeScripts.value("readout_settings", nullptr);
    if (scriptConfig)
        scriptString += scriptConfig->getScriptContents();

    if (!scriptString.isEmpty())
    {
        try
        {
            auto script = vme_script::parse(scriptString);

            for (auto cmd: script.commands)
            {
                if (cmd.type == vme_script::CommandType::Write)
                {
                    m_registerCache[cmd.address] = cmd.value;
                }
            }
        } catch (const vme_script::ParseError &)
        {}
    }
}

int ModuleConfig::getNumberOfChannels() const
{
    switch (type)
    {
        case VMEModuleType::MADC32:
        case VMEModuleType::MQDC32:
        case VMEModuleType::MTDC32:
        case VMEModuleType::MDI2:
            return 34; // TODO: verify

        case VMEModuleType::MDPP16:
        case VMEModuleType::MDPP32:
            return 34;

        case VMEModuleType::VHS4030p:
            return -1;

        case VMEModuleType::Invalid:
            return -1;
    }
    return -1;
}

namespace MADC
{
    static const int adc_resolution = 0x6042;
    static const int adc_override = 0x6046;
    static const std::array<int, 5> adc_bits = {
        11, // 2k
        12, // 4k
        12, // 4k hires
        13, // 8k
        13  // 8k hires
    };
    static const int adc_resolution_default = 2;
}

namespace MDPP
{
    static const int adc_resolution = 0x6046;
    static const std::array<int, 5> adc_bits = {
        16,
        15,
        14,
        13,
        12
    };
    static const int adc_resolution_default = 0;
}

int ModuleConfig::getDataBits() const
{
    switch (type)
    {
        case VMEModuleType::MADC32:
            {
                u32 regValue = m_registerCache.value(MADC::adc_resolution, MADC::adc_resolution_default);
                regValue = m_registerCache.value(MADC::adc_override, regValue);
                int bits = MADC::adc_bits.at(regValue);
                return bits;
            }

        case VMEModuleType::MDPP16:
        case VMEModuleType::MDPP32:
            {
                u32 index = m_registerCache.value(MDPP::adc_resolution, MDPP::adc_resolution_default);
                int bits = MDPP::adc_bits.at(index);
                return bits;
            }
        case VMEModuleType::MQDC32:
            return 12;

        case VMEModuleType::MTDC32:
            // Note: does not have an ADC resolution. Produces 16-bit wide timestamps
            return 16;

        case VMEModuleType::MDI2:
            return 12;

        case VMEModuleType::Invalid:
        case VMEModuleType::VHS4030p:
            break;
    }
    return -1;
}

u32 ModuleConfig::getDataExtractMask()
{
    switch (type)
    {
        case VMEModuleType::MADC32:
            return (1 << 13) - 1;

        case VMEModuleType::MDPP16:
        case VMEModuleType::MDPP32:
        case VMEModuleType::MTDC32:
            return (1 << 16) - 1;

        case VMEModuleType::MDI2:
        case VMEModuleType::MQDC32:
            return (1 << 12) - 1;

        case VMEModuleType::Invalid:
        case VMEModuleType::VHS4030p:
            break;
    }
    return 0;
}

void ModuleConfig::read_impl(const QJsonObject &json)
{
    type = VMEModuleShortNames.key(json["type"].toString(), VMEModuleType::Invalid);
    m_baseAddress = json["baseAddress"].toInt();

    QJsonObject scriptsObject = json["vme_scripts"].toObject();

    for (auto it = scriptsObject.begin();
         it != scriptsObject.end();
         ++it)
    {
        VMEScriptConfig *cfg(new VMEScriptConfig(this));
        cfg->read(it.value().toObject());
        vmeScripts[it.key()] = cfg;
    }

    updateRegisterCache();
}

void ModuleConfig::write_impl(QJsonObject &json) const
{
    json["type"] = VMEModuleShortNames.value(type, "invalid");
    json["baseAddress"] = static_cast<qint64>(m_baseAddress);

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


#if 0
void ModuleConfig::generateReadoutStack()
{
    VMECommandList readoutCmds;

    if (isMesytecModule(type))
    {
        readoutCmds.addFifoRead32(baseAddress, FifoReadTransferSize);
        readoutCmds.addMarker(EndMarker);
        readoutCmds.addWrite16(baseAddress + 0x6034, 1);
    }
    else
    {
        // Test to make a module without any readout work.
        // FIXME: This did result in errors in MVMEEventProcessor::processEventBuffer.
        //readoutCmds.addMarker(EndMarker);
    }

    CVMUSBReadoutList readoutList(readoutCmds);
    readoutStack = readoutList.toString();
    setModified();

#if 0
    if (type == VMEModuleType::VHS4030p)
    {
        // TODO: read channel voltages and currents here
        //VMECommandList cmds;

        // read channel 0 voltage
        readoutCmds.addRead16(baseAddress + 0x0060 + 16, getRegisterAddressModifier());
        readoutCmds.addRead16(baseAddress + 0x0060 + 16 + 2, getRegisterAddressModifier());

    }
#endif
}
#endif

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
}

void EventConfig::read_impl(const QJsonObject &json)
{
    qDeleteAll(modules);
    modules.clear();

    triggerCondition = static_cast<TriggerCondition>(json["triggerCondition"].toInt());
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
    json["triggerCondition"] = static_cast<int>(triggerCondition);
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
// DAQConfig
//
void DAQConfig::read_impl(const QJsonObject &json)
{
    qDeleteAll(eventConfigs);
    eventConfigs.clear();

    m_listFileOutputDirectory = json["listFileOutputDirectory"].toString();
    m_listFileOutputEnabled = json["listFileOutputEnabled"].toBool();

    QJsonArray eventArray = json["events"].toArray();

    for (int eventIndex=0; eventIndex<eventArray.size(); ++eventIndex)
    {
        QJsonObject eventObject = eventArray[eventIndex].toObject();
        EventConfig *eventConfig = new EventConfig(this);
        eventConfig->read(eventObject);
        eventConfigs.append(eventConfig);
    }

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
}

void DAQConfig::write_impl(QJsonObject &json) const
{
    json["listFileOutputDirectory"] = m_listFileOutputDirectory;
    json["listFileOutputEnabled"] = m_listFileOutputEnabled;

    QJsonArray eventArray;
    for (auto event: eventConfigs)
    {
        QJsonObject eventObject;
        event->write(eventObject);
        eventArray.append(eventObject);
    }
    json["events"] = eventArray;

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
}

ModuleConfig *DAQConfig::getModuleConfig(int eventIndex, int moduleIndex)
{
    ModuleConfig *result = 0;
    auto eventConfig = eventConfigs.value(eventIndex);

    if (eventConfig)
    {
        result = eventConfig->modules.value(moduleIndex);
    }

    return result;
}

EventConfig *DAQConfig::getEventConfig(const QString &name) const
{
    for (auto cfg: eventConfigs)
    {
        if (cfg->objectName() == name)
            return cfg;
    }
    return nullptr;
}

QVector<ModuleConfig *> DAQConfig::getAllModuleConfigs() const
{
    QVector<ModuleConfig *> result;

    for (auto eventConfig: eventConfigs)
    {
        for (auto moduleConfig: eventConfig->modules)
        {
            result.push_back(moduleConfig);
        }
    }

    return result;
}
