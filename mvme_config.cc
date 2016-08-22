#include "mvme_config.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

//
// ModuleConfig
//
QString ModuleConfig::getFullPath() const
{
    if (event)
    {
        return QString("%1.%2")
            .arg(event->getName())
            .arg(m_name);
    }

    return m_name;
}

int ModuleConfig::getNumberOfChannels() const
{
    switch (type)
    {
        case VMEModuleType::MADC32:
        case VMEModuleType::MQDC32:
        case VMEModuleType::MTDC32:
        case VMEModuleType::MDPP16:
        case VMEModuleType::MDPP32:
            return 32;

        case VMEModuleType::MDI2:
        case VMEModuleType::Invalid:
        case VMEModuleType::Generic:
            return -1;
    }
    return -1;
}

namespace MADC
{
    static const int adc_resolution = 0x6042;
    static const int adc_override = 0x6046;
    static const std::array<int, 5> adc_resolutions = {
        1 << 11, // 2k
        1 << 12, // 4k
        1 << 12, // 4k hires
        1 << 13, // 8k
        1 << 13  // 8k hires
    };
    static const int adc_resolution_default = 2;
}

namespace MDPP
{
    static const int adc_resolution = 0x6046;
    static const std::array<int, 5> adc_resolutions = {
        1 << 16,
        1 << 15,
        1 << 14,
        1 << 13,
        1 << 12 
    };
    static const int adc_resolution_default = 4;
}

int ModuleConfig::getADCResolution() const
{
    RegisterList allRegisters;
    allRegisters += parseRegisterList(initReset);
    allRegisters += parseRegisterList(initParameters);
    allRegisters += parseRegisterList(initReadout);
    allRegisters += parseRegisterList(initStartDaq);

    QHash<u32, u32> registerHash;
    for (auto p: allRegisters)
    {
        registerHash[p.first] = p.second;
    }


    switch (type)
    {
        case VMEModuleType::MADC32:
            {
                int regValue = registerHash.value(MADC::adc_resolution, MADC::adc_resolution_default);
                regValue = registerHash.value(MADC::adc_override, regValue);
                int res = MADC::adc_resolutions.at(regValue);
                return res;
            }

        case VMEModuleType::MDPP16:
        case VMEModuleType::MDPP32:
            {
                int index = registerHash.value(MDPP::adc_resolution, MDPP::adc_resolution_default);
                int res = MDPP::adc_resolutions.at(index);
                return res;
            }
        case VMEModuleType::MQDC32:
            return 4096;

        case VMEModuleType::MTDC32:
            // Note: does not have an ADC resolution. Produces 16-bit wide timestamps
            return 1 << 16;

        case VMEModuleType::MDI2:
            return 4096;

        case VMEModuleType::Invalid:
        case VMEModuleType::Generic:
            return -1;
    }
    return -1;
}

void ModuleConfig::setModified()
{
    auto eventConfig = qobject_cast<EventConfig *>(parent());
    if (eventConfig)
    {
        eventConfig->setModified();
    }
}

void ModuleConfig::read(const QJsonObject &json)
{
    type = VMEModuleShortNames.key(json["type"].toString(), VMEModuleType::Invalid);
    m_name = json["name"].toString();
    baseAddress = json["baseAddress"].toInt();
    mcstAddress = json["mcstAddress"].toInt();
    initReset = json["initReset"].toString();
    initParameters = json["initParameters"].toString();
    initReadout = json["initReadout"].toString();
    initStartDaq = json["initStartDaq"].toString();
    initStopDaq = json["initStopDaq"].toString();
    readoutStack = json["readoutStack"].toString();
}

void ModuleConfig::write(QJsonObject &json) const
{
    json["type"] = VMEModuleShortNames.value(type, "invalid");
    json["name"] = m_name;
    json["baseAddress"] = static_cast<qint64>(baseAddress);
    json["mcstAddress"] = static_cast<qint64>(mcstAddress);
    json["initReset"] = initReset;
    json["initParameters"] = initParameters;
    json["initReadout"] = initReadout;
    json["initStartDaq"] = initStartDaq;
    json["initStopDaq"] = initStopDaq;
    json["readoutStack"] = readoutStack;
}


//
// EventConfig
//

void EventConfig::setModified()
{
    auto daqConfig = qobject_cast<DAQConfig *>(parent());
    if (daqConfig)
    {
        daqConfig->setModified(true);
    }
}

void EventConfig::read(const QJsonObject &json)
{
    qDeleteAll(modules);
    modules.clear();

    m_name = json["name"].toString();
    triggerCondition = static_cast<TriggerCondition>(json["triggerCondition"].toInt());
    irqLevel = json["irqLevel"].toInt();
    irqVector = json["irqVector"].toInt();
    scalerReadoutPeriod = json["scalerReadoutPeriod"].toInt();
    scalerReadoutFrequency = json["scalerReadoutFrequency"].toInt();

    QJsonArray moduleArray = json["modules"].toArray();
    for (int i=0; i<moduleArray.size(); ++i)
    {
        QJsonObject moduleObject = moduleArray[i].toObject();
        ModuleConfig *moduleConfig = new ModuleConfig;
        moduleConfig->read(moduleObject);
        moduleConfig->event = this;
        modules.append(moduleConfig);
    }
}

void EventConfig::write(QJsonObject &json) const
{
    json["name"] = m_name;
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
}


//
// DAQConfig
//
void DAQConfig::setModified(bool b)
{
    if (m_isModified != b)
    {
        m_isModified = b;
        emit modifiedChanged(b);
    }
}

void DAQConfig::read(const QJsonObject &json)
{
    qDeleteAll(m_eventConfigs);
    m_eventConfigs.clear();

    listFileOutputDirectory = json["listFileOutputDirectory"].toString();
    listFileOutputEnabled = json["listFileOutputEnabled"].toBool();

    QJsonArray eventArray = json["events"].toArray();

    for (int eventIndex=0; eventIndex<eventArray.size(); ++eventIndex)
    {
        QJsonObject eventObject = eventArray[eventIndex].toObject();
        EventConfig *eventConfig = new EventConfig;
        eventConfig->read(eventObject);
        m_eventConfigs.append(eventConfig);
    }
}

void DAQConfig::write(QJsonObject &json) const
{
    json["listFileOutputDirectory"] = listFileOutputDirectory;
    json["listFileOutputEnabled"] = listFileOutputEnabled;

    QJsonArray eventArray;
    for (auto event: m_eventConfigs)
    {
        QJsonObject eventObject;
        event->write(eventObject);
        eventArray.append(eventObject);
    }
    json["events"] = eventArray;
}

#if 1
QByteArray DAQConfig::toJson() const
{
    QJsonObject configObject;
    write(configObject);
    QJsonDocument doc(configObject);
    return doc.toJson();
}
#endif

ModuleConfig *DAQConfig::getModuleConfig(int eventIndex, int moduleIndex)
{
    ModuleConfig *result = 0;
    auto eventConfig = m_eventConfigs.value(eventIndex);

    if (eventConfig)
    {
        result = eventConfig->modules.value(moduleIndex);
    }

    return result;
}

EventConfig *DAQConfig::getEventConfig(const QString &name) const
{
    for (auto cfg: m_eventConfigs)
    {
        if (cfg->getName() == name)
            return cfg;
    }
    return nullptr;
}

QVector<ModuleConfig *> DAQConfig::getAllModuleConfigs() const
{
    QVector<ModuleConfig *> result;

    for (auto eventConfig: m_eventConfigs)
    {
        for (auto moduleConfig: eventConfig->modules)
        {
            result.push_back(moduleConfig);
        }
    }

    return result;
}
