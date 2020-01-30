#include "vme_config_util.h"

#include <QJsonDocument>
#include <QSet>

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
                  [] (ConfigObject *child) { child->generateNewId(); });
}

} // end namespace vme_config
} // end namespace mvme
