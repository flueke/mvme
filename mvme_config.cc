#include "mvme_config.h"
#include <QJsonObject>
#include <QJsonArray>


void ModuleConfig::read(const QJsonObject &json)
{
    type = VMEModuleShortNames.key(json["type"].toString(), VMEModuleType::Invalid);
    name = json["name"].toString();
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
    json["name"] = name;
    json["baseAddress"] = static_cast<qint64>(baseAddress);
    json["mcstAddress"] = static_cast<qint64>(mcstAddress);
    json["initReset"] = initReset;
    json["initParameters"] = initParameters;
    json["initReadout"] = initReadout;
    json["initStartDaq"] = initStartDaq;
    json["initStopDaq"] = initStopDaq;
    json["readoutStack"] = readoutStack;
}


void EventConfig::read(const QJsonObject &json)
{
    qDeleteAll(modules);
    modules.clear();

    name = json["name"].toString();
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
        modules.append(moduleConfig);
    }
}

void EventConfig::write(QJsonObject &json) const
{
    json["name"] = name;
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


void DAQConfig::read(const QJsonObject &json)
{
    qDeleteAll(eventConfigs);
    eventConfigs.clear();

    QJsonArray eventArray = json["events"].toArray();

    for (int eventIndex=0; eventIndex<eventArray.size(); ++eventIndex)
    {
        QJsonObject eventObject = eventArray[eventIndex].toObject();
        EventConfig *eventConfig = new EventConfig;
        eventConfig->read(eventObject);
        eventConfigs.append(eventConfig);
    }
}

void DAQConfig::write(QJsonObject &json) const
{
    QJsonArray eventArray;
    for (auto event: eventConfigs)
    {
        QJsonObject eventObject;
        event->write(eventObject);
        eventArray.append(eventObject);
    }
    json["events"] = eventArray;
}
