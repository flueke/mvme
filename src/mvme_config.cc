#include "mvme_config.h"
#include "CVMUSBReadoutList.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

static QJsonObject storeDynamicProperties(const QObject *object)
{
    QJsonObject json;

    for (auto name: object->dynamicPropertyNames())
       json[QString::fromLocal8Bit(name)] = QJsonValue::fromVariant(object->property(name.constData()));

    return json;
}

static void loadDynamicProperties(const QJsonObject &json, QObject *dest)
{
    auto properties = json.toVariantMap();

    for (auto propName: properties.keys())
    {
        const auto &value = properties[propName];
        dest->setProperty(propName.toLocal8Bit().constData(), value);
    }
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
        m_modified = b;
        emit modifiedChanged(b);

        if (b)
        {
            if (auto parentConfig = qobject_cast<ConfigObject *>(parent()))
                parentConfig->setModified(true);
        }
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
    auto daqConfig  = qobject_cast<DAQConfig *>(parent());

    QString title;

    if (module)
    {
        title = QString("%1 for %2")
            .arg(objectName())
            .arg(module->objectName());
    }
    else if (event)
    {
        title = QString("%1 for %2")
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
{
    vmeScripts["parameters"] = new VMEScriptConfig(this);
    vmeScripts["parameters"]->setObjectName(QSL("Module Init"));

    vmeScripts["readout_settings"] = new VMEScriptConfig(this);
    vmeScripts["readout_settings"]->setObjectName(QSL("VME Interface Settings"));

    vmeScripts["readout"] = new VMEScriptConfig(this);
    vmeScripts["readout"]->setObjectName(QSL("Readout"));

    vmeScripts["reset"] = new VMEScriptConfig(this);
    vmeScripts["reset"]->setObjectName(QSL("Module Reset"));
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

    qDebug() << __PRETTY_FUNCTION__ << "read" << eventConfigs.size() << "event configs";
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

QList<ModuleConfig *> DAQConfig::getAllModuleConfigs() const
{
    QList<ModuleConfig *> result;

    for (auto eventConfig: eventConfigs)
    {
        for (auto moduleConfig: eventConfig->modules)
        {
            result.push_back(moduleConfig);
        }
    }

    return result;
}

QPair<int, int> DAQConfig::getEventAndModuleIndices(ModuleConfig *cfg) const
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

//
// DataFilterConfig
//
void DataFilterConfig::setFilter(const DataFilter &filter)
{
    if (m_filter != filter)
    {
        m_filter = filter;
        setModified();
    }
}

void DataFilterConfig::read_impl(const QJsonObject &json)
{
    m_filter = DataFilter(json["filter"].toString().toLocal8Bit());
    loadDynamicProperties(json["properties"].toObject(), this);
}

void DataFilterConfig::write_impl(QJsonObject &json) const
{
    json["filter"] = QString::fromLocal8Bit(m_filter.getFilter());
    json["properties"] = storeDynamicProperties(this);
}

//
// Hist1DConfig
//
void Hist1DConfig::read_impl(const QJsonObject &json)
{
    m_bits = json["bits"].toInt();
    m_filterId = QUuid(json["filterId"].toString());
    m_filterAddress = json["filterAddress"].toInt();
    loadDynamicProperties(json["properties"].toObject(), this);
}

void Hist1DConfig::write_impl(QJsonObject &json) const
{
    json["bits"] = static_cast<qint64>(m_bits);
    json["filterId"] = m_filterId.toString();
    json["filterAddress"] = static_cast<qint64>(m_filterAddress);
    json["properties"] = storeDynamicProperties(this);
}

//
// Hist2DConfig
//
void Hist2DConfig::setXFilterId(const QUuid &id)
{
    if (id != m_xFilterId)
    {
        m_xFilterId = id;
        setModified();
    }
}

void Hist2DConfig::setYFilterId(const QUuid &id)
{
    if (id != m_yFilterId)
    {
        m_yFilterId = id;
        setModified();
    }
}

void Hist2DConfig::setXFilterAddress(u32 address)
{
    if (address != m_xAddress)
    {
        m_xAddress = address;
        setModified();
    }
}

void Hist2DConfig::setYFilterAddress(u32 address)
{
    if (address != m_yAddress)
    {
        m_yAddress = address;
        setModified();
    }
}

void Hist2DConfig::setXBits(u32 bits)
{
    if (m_xBits != bits)
    {
        m_xBits = bits;
        setModified();
    }
}

void Hist2DConfig::setYBits(u32 bits)
{
    if (m_yBits != bits)
    {
        m_yBits = bits;
        setModified();
    }
}

void Hist2DConfig::read_impl(const QJsonObject &json)
{
    m_xBits = json["xBits"].toInt();
    m_yBits = json["yBits"].toInt();
    m_xFilterId = QUuid(json["xFilterId"].toString());
    m_yFilterId = QUuid(json["yFilterId"].toString());
    m_xAddress = json["xAddress"].toInt();
    m_yAddress = json["yAddress"].toInt();
    loadDynamicProperties(json["properties"].toObject(), this);
}

void Hist2DConfig::write_impl(QJsonObject &json) const
{
    json["xBits"] = static_cast<qint64>(m_xBits);
    json["yBits"] = static_cast<qint64>(m_yBits);
    json["xFilterId"] = m_xFilterId.toString();
    json["yFilterId"] = m_yFilterId.toString();
    json["xAddress"] = static_cast<qint64>(m_xAddress);
    json["yAddress"] = static_cast<qint64>(m_yAddress);
    json["properties"] = storeDynamicProperties(this);
}

//
// AnalysisConfig
//
QList<DataFilterConfig *> AnalysisConfig::getFilters(int eventIndex, int moduleIndex) const
{
    return m_filters.value(eventIndex).value(moduleIndex);
}

void AnalysisConfig::setFilters(int eventIndex, int moduleIndex, const DataFilterConfigList &filters)
{
    removeFilters(eventIndex, moduleIndex);
    m_filters[eventIndex][moduleIndex] = filters;

    for (auto filter: filters)
    {
        filter->setParent(this);
        emit objectAdded(filter);
    }

    setModified(true);
}

void AnalysisConfig::removeFilters(int eventIndex, int moduleIndex)
{
    auto filters = m_filters[eventIndex].take(moduleIndex);

    for (auto filter: filters)
    {
        emit objectAboutToBeRemoved(filter);
        filter->setParent(nullptr);
        filter->deleteLater();
    }

    setModified(true);
}

void AnalysisConfig::addFilter(int eventIndex, int moduleIndex, DataFilterConfig *config)
{
    m_filters[eventIndex][moduleIndex].push_back(config);
    config->setParent(this);
    emit objectAdded(config);
    setModified(true);
}

void AnalysisConfig::removeFilter(int eventIndex, int moduleIndex, DataFilterConfig *config)
{
    if (m_filters[eventIndex][moduleIndex].removeOne(config))
    {
        emit objectAboutToBeRemoved(config);
        config->setParent(nullptr);
        config->deleteLater();
        setModified(true);
    }
}

QPair<int, int> AnalysisConfig::getEventAndModuleIndices(DataFilterConfig *cfg) const
{
    for (int eventIndex: m_filters.keys())
    {
        for (int moduleIndex: m_filters[eventIndex].keys())
        {
            if (m_filters[eventIndex][moduleIndex].contains(cfg))
                return qMakePair(eventIndex, moduleIndex);
        }
    }

    return qMakePair(-1, -1);
}

void AnalysisConfig::addHist1DConfig(Hist1DConfig *config)
{
    m_1dHistograms.push_back(config);
    config->setParent(this);
    emit objectAdded(config);
    setModified(true);
}

void AnalysisConfig::addHist2DConfig(Hist2DConfig *config)
{
    m_2dHistograms.push_back(config);
    config->setParent(this);
    emit objectAdded(config);
    setModified(true);
}

void AnalysisConfig::removeHist1DConfig(Hist1DConfig *config)
{
    if (m_1dHistograms.removeOne(config))
    {
        emit objectAboutToBeRemoved(config);
        config->setParent(nullptr);
        config->deleteLater();
        setModified(true);
    }
}

void AnalysisConfig::removeHist2DConfig(Hist2DConfig *config)
{
    if (m_2dHistograms.removeOne(config))
    {
        emit objectAboutToBeRemoved(config);
        config->setParent(nullptr);
        config->deleteLater();
        setModified(true);
    }
}

void AnalysisConfig::read_impl(const QJsonObject &json)
{
    m_filters.clear();
    m_1dHistograms.clear();
    m_2dHistograms.clear();

    {
        QJsonArray array = json["filters"].toArray();

        for (auto it=array.begin();
             it != array.end();
             ++it)
        {
            auto filterJson = it->toObject();
            auto cfg = new DataFilterConfig(this);
            cfg->read(filterJson);
            int eventIndex = filterJson["eventIndex"].toInt();
            int moduleIndex = filterJson["moduleIndex"].toInt();
            m_filters[eventIndex][moduleIndex].push_back(cfg);
        }
    }

    {
        QJsonArray array = json["1dHistograms"].toArray();

        for (auto it=array.begin();
             it != array.end();
             ++it)
        {
            auto histoJson = it->toObject();
            auto cfg = new Hist1DConfig(this);
            cfg->read(histoJson);
            m_1dHistograms.push_back(cfg);
        }
    }

    {
        QJsonArray array = json["2dHistograms"].toArray();

        for (auto it=array.begin();
             it != array.end();
             ++it)
        {
            auto histoJson = it->toObject();
            auto cfg = new Hist2DConfig(this);
            cfg->read(histoJson);
            m_2dHistograms.push_back(cfg);
        }
    }

    setModified(false);
}

void AnalysisConfig::write_impl(QJsonObject &json) const
{
    {
        QJsonArray array;

        for (int eventIndex = 0;
             eventIndex < m_filters.size();
             ++eventIndex)
        {
            for (int moduleIndex = 0;
                 moduleIndex < m_filters[eventIndex].size();
                 ++moduleIndex)
            {
                for (const auto &filterConfig: m_filters[eventIndex][moduleIndex])
                {
                    QJsonObject filterObject;
                    filterConfig->write(filterObject);
                    filterObject["eventIndex"] = eventIndex;
                    filterObject["moduleIndex"] = moduleIndex;
                    array.append(filterObject);
                }
            }
        }

        json["filters"] = array;
    }

    {
        QJsonArray array;

        for (auto histoConfig: m_1dHistograms)
        {
            QJsonObject histoJson;
            histoConfig->write(histoJson);
            array.append(histoJson);
        }

        json["1dHistograms"] = array;
    }

    {
        QJsonArray array;

        for (auto histoConfig: m_2dHistograms)
        {
            QJsonObject histoJson;
            histoConfig->write(histoJson);
            array.append(histoJson);
        }

        json["2dHistograms"] = array;
    }
}

#if 0
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
#endif
