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
        qDebug() << __PRETTY_FUNCTION__ << this << m_modified << "->" << b;
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
    loadDynamicProperties(json["properties"].toObject(), this);
}

void VMEScriptConfig::write_impl(QJsonObject &json) const
{
    json["vme_script"] = m_script;
    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;
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
    loadDynamicProperties(json["properties"].toObject(), this);
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
    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;
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

    loadDynamicProperties(json["properties"].toObject(), this);
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
    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;
}


//
// DAQConfig
//
void DAQConfig::addEventConfig(EventConfig *config)
{
    config->setParent(this);
    eventConfigs.push_back(config);
    emit eventAdded(config);
    setModified();
}

bool DAQConfig::removeEventConfig(EventConfig *config)
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

bool DAQConfig::contains(EventConfig *config)
{
    return eventConfigs.indexOf(config) >= 0;
}

void DAQConfig::addGlobalScript(VMEScriptConfig *config, const QString &category)
{
    config->setParent(this);
    vmeScriptLists[category].push_back(config);
    emit globalScriptAdded(config, category);
    setModified();
}

bool DAQConfig::removeGlobalScript(VMEScriptConfig *config)
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

void DAQConfig::read_impl(const QJsonObject &json)
{
    qDeleteAll(eventConfigs);
    eventConfigs.clear();

    QJsonArray eventArray = json["events"].toArray();

    for (int eventIndex=0; eventIndex<eventArray.size(); ++eventIndex)
    {
        QJsonObject eventObject = eventArray[eventIndex].toObject();
        EventConfig *eventConfig = new EventConfig(this);
        eventConfig->read(eventObject);
        eventConfigs.append(eventConfig);
    }
    qDebug() << __PRETTY_FUNCTION__ << "read" << eventConfigs.size() << "event configs";

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

    loadDynamicProperties(json["properties"].toObject(), this);
}

void DAQConfig::write_impl(QJsonObject &json) const
{
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
    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;
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

QwtScaleMap DataFilterConfig::makeConversionMap(u32 address) const
{
    auto unitRange = m_unitRanges.value(address, m_baseUnitRange);
    QwtScaleMap result;
    result.setScaleInterval(0, std::pow(2.0, getDataBits()) - 1.0);
    result.setPaintInterval(unitRange.first, unitRange.second);
    return result;
}

void DataFilterConfig::setFilter(const DataFilter &filter)
{
    if (m_filter != filter)
    {
        m_filter = filter;
        m_baseUnitRange = qMakePair(0.0, (1 << getDataBits()) - 1.0);

        m_unitRanges.clear();

        for (u32 addr = 0; addr < getAddressCount(); ++addr)
            m_unitRanges.push_back(m_baseUnitRange);

        setModified();
    }
}

void DataFilterConfig::setAxisTitle(const QString &title)
{
    if (m_axisTitle != title)
    {
        m_axisTitle = title;
        setModified();
    }
}

void DataFilterConfig::setUnitString(const QString &unit)
{
    if (m_unitString != unit)
    {
        m_unitString = unit;
        setModified();
    }
}

double DataFilterConfig::getUnitMin(u32 address) const
{
    return m_unitRanges.value(address, m_baseUnitRange).first;
}

void DataFilterConfig::setUnitMin(u32 address, double value)
{
    if (address < getAddressCount() && m_unitRanges[address].first != value)
    {
        m_unitRanges[address].first = value;
        setModified();
    }
}


double DataFilterConfig::getUnitMax(u32 address) const
{
    return m_unitRanges.value(address, m_baseUnitRange).second;
}

void DataFilterConfig::setUnitMax(u32 address, double value)
{
    if (address < getAddressCount() && m_unitRanges[address].second != value)
    {
        m_unitRanges[address].second = value;
        setModified();
    }
}

QPair<double, double> DataFilterConfig::getUnitRange(u32 address) const
{
    return m_unitRanges.value(address, m_baseUnitRange);
}

void DataFilterConfig::setUnitRange(u32 address, double min, double max)
{
    setUnitRange(address, qMakePair(min, max));
}

void DataFilterConfig::setUnitRange(u32 address, QPair<double, double> range)
{
    if (address < getAddressCount() && m_unitRanges[address] != range)
    {
        m_unitRanges[address] = range;
        setModified();
    }
}

QPair<double, double> DataFilterConfig::getBaseUnitRange() const
{
    return m_baseUnitRange;
}

void DataFilterConfig::setBaseUnitRange(double min, double max)
{
    auto range = qMakePair(min, max);

    if (range != m_baseUnitRange)
    {
        m_baseUnitRange = range;
        setModified();
    }
}

void DataFilterConfig::resetToBaseUnits(u32 address)
{
    setUnitRange(address, getBaseUnitRange());
}

bool DataFilterConfig::isAddressValid(u32 address)
{
    return (address < (1u << getAddressBits()));
}

void DataFilterConfig::read_impl(const QJsonObject &json)
{
    auto filterString = json["filter"].toString().toLocal8Bit();
    auto filterWordIndex = json["filterWordIndex"].toInt(-1);
    setFilter(DataFilter(filterString, filterWordIndex));

    m_axisTitle = json["axisTitle"].toString();
    m_unitString = json["unitString"].toString();
    double baseMin = json["unitMinValue"].toDouble();
    double baseMax = json["unitMaxValue"].toDouble();
    m_baseUnitRange = qMakePair(baseMin, baseMax);

    auto array = json["unitRanges"].toArray();

    u32 addr = 0;
    for (auto it=array.begin();
         it != array.end();
         ++it, ++addr)
    {
        auto rangeJson = it->toObject();
        auto range = qMakePair(rangeJson["min"].toDouble(), rangeJson["max"].toDouble());
        setUnitRange(addr, range);
    }


    loadDynamicProperties(json["properties"].toObject(), this);
}

void DataFilterConfig::write_impl(QJsonObject &json) const
{
    json["filter"] = QString::fromLocal8Bit(m_filter.getFilter());
    json["filterWordIndex"] = m_filter.getWordIndex();
    json["axisTitle"] = getAxisTitle();
    json["unitString"] = getUnitString();
    json["unitMinValue"] = getBaseUnitRange().first;
    json["unitMaxValue"] = getBaseUnitRange().second;

    QJsonArray array;

    for (auto range: m_unitRanges)
    {
        QJsonObject rangeObject;

        rangeObject["min"] = range.first;
        rangeObject["max"] = range.second;

        array.append(rangeObject);
    }

    json["unitRanges"] = array;

    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;
}

//
// DualWordDataFilter
//
void DualWordDataFilterConfig::setFilter(const DualWordDataFilter &filter)
{
    if (m_filter != filter)
    {
        m_filter = filter;
        setModified();
    }
}

void DualWordDataFilterConfig::setAxisTitle(const QString &title)
{
    if (m_axisTitle != title)
    {
        m_axisTitle = title;
        setModified();
    }
}

void DualWordDataFilterConfig::setUnitString(const QString &unit)
{
    if (m_unitString != unit)
    {
        m_unitString = unit;
        setModified();
    }
}

QPair<double, double> DualWordDataFilterConfig::getUnitRange() const
{
    return m_unitRange;
}

void DualWordDataFilterConfig::setUnitRange(double min, double max)
{
    auto range = qMakePair(min, max);

    if (range != m_unitRange)
    {
        m_unitRange = range;
        setModified();
    }
}

void DualWordDataFilterConfig::read_impl(const QJsonObject &json)
{
    auto lowFilterString  = json["lowFilter"].toString().toLocal8Bit();
    auto lowFilterWordIndex  = json["lowFilterWordIndex"].toInt(-1);
    DataFilter lowFilter(lowFilterString, lowFilterWordIndex);

    auto highFilterString = json["highFilter"].toString().toLocal8Bit();
    auto highFilterWordIndex = json["highFilterWordIndex"].toInt(-1);
    DataFilter highFilter(highFilterString, highFilterWordIndex);

    m_filter = DualWordDataFilter(lowFilter, highFilter);

    m_axisTitle = json["axisTitle"].toString();
    m_unitString = json["unitString"].toString();
    double unitMin = json["unitMinValue"].toDouble();
    double unitMax = json["unitMaxValue"].toDouble();
    m_unitRange = qMakePair(unitMin, unitMax);

    loadDynamicProperties(json["properties"].toObject(), this);
}

void DualWordDataFilterConfig::write_impl(QJsonObject &json) const
{
    auto filters = m_filter.getFilters();

    json["lowFilter"]  = QString::fromLocal8Bit(filters[0].getFilter());
    json["lowFilterWordIndex"] = filters[0].getWordIndex();
    json["highFilter"] = QString::fromLocal8Bit(filters[1].getFilter());
    json["highFilterWordIndex"] = filters[1].getWordIndex();

    json["axisTitle"] = getAxisTitle();
    json["unitString"] = getUnitString();
    json["unitMinValue"] = getUnitRange().first;
    json["unitMaxValue"] = getUnitRange().second;

    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;
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
    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;
}

//
// Hist2DConfig
//

void Hist2DConfig::setAxisConfig(Qt::Axis axis, const Hist2DAxisConfig &config)
{
    if (m_axes[axis] != config)
    {
        m_axes[axis] = config;
        setModified();
    }
}

QUuid Hist2DConfig::getFilterId(Qt::Axis axis) const
{
    return m_axes[axis].filterId;
}

void Hist2DConfig::setFilterId(Qt::Axis axis, const QUuid &id)
{
    if (m_axes[axis].filterId != id)
    {
        m_axes[axis].filterId = id;
        setModified();
    }
}

u32 Hist2DConfig::getFilterAddress(Qt::Axis axis) const
{
    return m_axes[axis].filterAddress;
}
void Hist2DConfig::setFilterAddress(Qt::Axis axis, u32 address)
{
    if (m_axes[axis].filterAddress != address)
    {
        m_axes[axis].filterAddress = address;
        setModified();
    }
}

u32 Hist2DConfig::getBits(Qt::Axis axis) const
{
    return m_axes[axis].bits;
}
void Hist2DConfig::setBits(Qt::Axis axis, u32 bits)
{
    if (m_axes[axis].bits != bits)
    {
        m_axes[axis].bits = bits;
        setModified();
    }
}

u32 Hist2DConfig::getOffset(Qt::Axis axis) const
{
    return m_axes[axis].offset;
}
void Hist2DConfig::setOffset(Qt::Axis axis, u32 offset)
{
    if (m_axes[axis].offset != offset)
    {
        m_axes[axis].offset = offset;
        setModified();
    }
}

u32 Hist2DConfig::getShift(Qt::Axis axis) const
{
    return m_axes[axis].shift;
}
void Hist2DConfig::setShift(Qt::Axis axis, u32 shift)
{
    if (m_axes[axis].shift != shift)
    {
        m_axes[axis].shift = shift;
        setModified();
    }
}

QString Hist2DConfig::getAxisTitle(Qt::Axis axis) const
{
    return m_axes[axis].title;
}
void Hist2DConfig::setAxisTitle(Qt::Axis axis, const QString &title)
{
    if (m_axes[axis].title != title)
    {
        m_axes[axis].title = title;
        setModified();
    }
}

QString Hist2DConfig::getAxisUnitLabel(Qt::Axis axis) const
{
    return m_axes[axis].unit;
}
void Hist2DConfig::setAxisUnitLabel(Qt::Axis axis, const QString &unit)
{
    if (m_axes[axis].unit != unit)
    {
        m_axes[axis].unit = unit;
        setModified();
    }
}

double Hist2DConfig::getUnitMin(Qt::Axis axis) const
{
    return m_axes[axis].unitMin;
}
void Hist2DConfig::setUnitMin(Qt::Axis axis, double unitMin)
{
    if (m_axes[axis].unitMin != unitMin)
    {
        m_axes[axis].unitMin = unitMin;
        setModified();
    }
}

double Hist2DConfig::getUnitMax(Qt::Axis axis) const
{
    return m_axes[axis].unitMax;
}
void Hist2DConfig::setUnitMax(Qt::Axis axis, double unitMax)
{
    if (m_axes[axis].unitMax != unitMax)
    {
        m_axes[axis].unitMax = unitMax;
        setModified();
    }
}

void Hist2DConfig::read_impl(const QJsonObject &json)
{
    m_axes[Qt::XAxis].bits = json["xBits"].toInt();
    m_axes[Qt::YAxis].bits = json["yBits"].toInt();

    m_axes[Qt::XAxis].filterId = QUuid(json["xFilterId"].toString());
    m_axes[Qt::YAxis].filterId = QUuid(json["yFilterId"].toString());

    m_axes[Qt::XAxis].filterAddress = json["xAddress"].toInt();
    m_axes[Qt::YAxis].filterAddress = json["yAddress"].toInt();

    m_axes[Qt::XAxis].offset = json["xOffset"].toInt();
    m_axes[Qt::YAxis].offset = json["yOffset"].toInt();

    m_axes[Qt::XAxis].shift = json["xShift"].toInt();
    m_axes[Qt::YAxis].shift = json["yShift"].toInt();

    m_axes[Qt::XAxis].title = json["xTitle"].toString();
    m_axes[Qt::YAxis].title = json["yTitle"].toString();

    m_axes[Qt::XAxis].unit = json["xUnit"].toString();
    m_axes[Qt::YAxis].unit = json["yUnit"].toString();

    m_axes[Qt::XAxis].unitMin = json["xUnitMin"].toDouble();
    m_axes[Qt::YAxis].unitMin = json["yUnitMin"].toDouble();

    m_axes[Qt::XAxis].unitMax = json["xUnitMax"].toDouble();
    m_axes[Qt::YAxis].unitMax = json["yUnitMax"].toDouble();

    loadDynamicProperties(json["properties"].toObject(), this);
}

void Hist2DConfig::write_impl(QJsonObject &json) const
{
    json["xBits"] = static_cast<qint64>(m_axes[Qt::XAxis].bits);
    json["yBits"] = static_cast<qint64>(m_axes[Qt::YAxis].bits);

    json["xFilterId"] = m_axes[Qt::XAxis].filterId.toString();
    json["yFilterId"] = m_axes[Qt::YAxis].filterId.toString();

    json["xAddress"] = static_cast<qint64>(m_axes[Qt::XAxis].filterAddress);
    json["yAddress"] = static_cast<qint64>(m_axes[Qt::YAxis].filterAddress);

    json["xOffset"] = static_cast<qint64>(m_axes[Qt::XAxis].offset);
    json["yOffset"] = static_cast<qint64>(m_axes[Qt::YAxis].offset);

    json["xShift"] = static_cast<qint64>(m_axes[Qt::XAxis].shift);
    json["yShift"] = static_cast<qint64>(m_axes[Qt::YAxis].shift);

    json["xTitle"] = m_axes[Qt::XAxis].title;
    json["yTitle"] = m_axes[Qt::YAxis].title;

    json["xUnit"] = m_axes[Qt::XAxis].unit;
    json["yUnit"] = m_axes[Qt::YAxis].unit;

    json["xUnitMin"] = m_axes[Qt::XAxis].unitMin;
    json["yUnitMin"] = m_axes[Qt::YAxis].unitMin;

    json["xUnitMax"] = m_axes[Qt::XAxis].unitMax;
    json["yUnitMax"] = m_axes[Qt::YAxis].unitMax;

    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;
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

DualWordDataFilterConfigList AnalysisConfig::getDualWordFilters(int eventIndex, int moduleIndex) const
{
    return m_dualWordFilters.value(eventIndex).value(moduleIndex);
}

void AnalysisConfig::setDualWordFilters(int eventIndex, int moduleIndex, const DualWordDataFilterConfigList &filters)
{
    removeDualWordFilters(eventIndex, moduleIndex);
    m_dualWordFilters[eventIndex][moduleIndex] = filters;

    for (auto filter: filters)
    {
        qDebug() << __PRETTY_FUNCTION__ << filter;
        filter->setParent(this);
        emit objectAdded(filter);
    }

    setModified(true);
}

void AnalysisConfig::removeDualWordFilters(int eventIndex, int moduleIndex)
{
    auto filters = m_dualWordFilters[eventIndex].take(moduleIndex);

    for (auto filter: filters)
    {
        emit objectAboutToBeRemoved(filter);
        filter->setParent(nullptr);
        filter->deleteLater();
    }

    setModified(true);
}

void AnalysisConfig::addDualWordFilter(int eventIndex, int moduleIndex, DualWordDataFilterConfig *config)
{
    m_dualWordFilters[eventIndex][moduleIndex].push_back(config);
    config->setParent(this);
    emit objectAdded(config);
    setModified(true);
}

void AnalysisConfig::removeDualWordFilter(int eventIndex, int moduleIndex, DualWordDataFilterConfig *config)
{
    if (m_dualWordFilters[eventIndex][moduleIndex].removeOne(config))
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

QPair<int, int> AnalysisConfig::getEventAndModuleIndices(DualWordDataFilterConfig *cfg) const
{
    for (int eventIndex: m_dualWordFilters.keys())
    {
        for (int moduleIndex: m_dualWordFilters[eventIndex].keys())
        {
            if (m_dualWordFilters[eventIndex][moduleIndex].contains(cfg))
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
    m_dualWordFilters.clear();
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
        QJsonArray array = json["dualWordFilters"].toArray();

        for (auto it=array.begin();
             it != array.end();
             ++it)
        {
            auto filterJson = it->toObject();
            auto cfg = new DualWordDataFilterConfig(this);
            cfg->read(filterJson);
            int eventIndex = filterJson["eventIndex"].toInt();
            int moduleIndex = filterJson["moduleIndex"].toInt();
            m_dualWordFilters[eventIndex][moduleIndex].push_back(cfg);
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
    loadDynamicProperties(json["properties"].toObject(), this);
}

void AnalysisConfig::write_impl(QJsonObject &json) const
{
    {
        QJsonArray array;

        for (auto eventIter = m_filters.begin();
             eventIter != m_filters.end();
             ++eventIter)
        {
            int eventIndex = eventIter.key();

            for (auto moduleIter = eventIter.value().begin();
                 moduleIter != eventIter.value().end();
                 ++moduleIter)
            {
                int moduleIndex = moduleIter.key();

                for (const auto &filterConfig: moduleIter.value())
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

        for (auto eventIter = m_dualWordFilters.begin();
             eventIter != m_dualWordFilters.end();
             ++eventIter)
        {
            int eventIndex = eventIter.key();

            for (auto moduleIter = eventIter.value().begin();
                 moduleIter != eventIter.value().end();
                 ++moduleIter)
            {
                int moduleIndex = moduleIter.key();

                for (const auto &filterConfig: moduleIter.value())
                {
                    QJsonObject filterObject;
                    filterConfig->write(filterObject);
                    filterObject["eventIndex"] = eventIndex;
                    filterObject["moduleIndex"] = moduleIndex;
                    array.append(filterObject);
                }
            }
        }

        json["dualWordFilters"] = array;
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

    auto props = storeDynamicProperties(this);
    if (!props.isEmpty())
        json["properties"] = props;
}

void AnalysisConfig::updateHistogramsForFilter(DataFilterConfig *filterConfig)
{
    // 1d
    for (auto histoConfig: get1DHistogramConfigs())
    {
        if (histoConfig->getFilterId() == filterConfig->getId())
        {
            updateHistogramConfigFromFilterConfig(histoConfig, filterConfig);
        }
    }

    // 2d
    auto update_hist2d_axis = [](Qt::Axis axis, Hist2DConfig *histoConfig, DataFilterConfig *filterConfig)
    {
        u32 address = histoConfig->getFilterAddress(axis);
        histoConfig->setAxisTitle(axis, filterConfig->getAxisTitle());
        histoConfig->setAxisUnitLabel(axis, filterConfig->getUnitString());
        histoConfig->setUnitMin(axis, filterConfig->getUnitMin(address));
        histoConfig->setUnitMax(axis, filterConfig->getUnitMax(address));
    };

    for (auto histoConfig: get2DHistogramConfigs())
    {
        if (histoConfig->getFilterId(Qt::XAxis) == filterConfig->getId())
        {
            update_hist2d_axis(Qt::XAxis, histoConfig, filterConfig);
        }

        if (histoConfig->getFilterId(Qt::YAxis) == filterConfig->getId())
        {
            update_hist2d_axis(Qt::YAxis, histoConfig, filterConfig);
        }
    }
}

void updateHistogramConfigFromFilterConfig(Hist1DConfig *histoConfig, DataFilterConfig *filterConfig)
{
    auto axisTitle = filterConfig->getAxisTitle();
    u32 address = histoConfig->getFilterAddress();

    // TODO: move this into Hist1DWidget just as is done for 2D histograms.
    if (!axisTitle.isEmpty())
    {
        axisTitle.replace(QSL("%A"), QString::number(address));
        axisTitle.replace(QSL("%a"), QString::number(address));
    }
    else
    {
        axisTitle = QString("%1 %2").arg(filterConfig->objectName()).arg(address);
    }

    histoConfig->setProperty("xAxisTitle", axisTitle);
    histoConfig->setProperty("xAxisUnit", filterConfig->getUnitString());
    histoConfig->setProperty("xAxisUnitMin", filterConfig->getUnitMin(address));
    histoConfig->setProperty("xAxisUnitMax", filterConfig->getUnitMax(address));
}

void updateHistogramConfigFromFilterConfig(Hist1DConfig *histoConfig, DualWordDataFilterConfig *filterConfig)
{
    histoConfig->setProperty("xAxisTitle", filterConfig->getAxisTitle());
    histoConfig->setProperty("xAxisUnit", filterConfig->getUnitString());
    histoConfig->setProperty("xAxisUnitMin", filterConfig->getUnitRange().first);
    histoConfig->setProperty("xAxisUnitMax", filterConfig->getUnitRange().second);
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
