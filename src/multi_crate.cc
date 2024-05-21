#include "multi_crate.h"

#include <cassert>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/util/udp_sockets.h>
#include <QDir>
#include <stdexcept>

#include "mvlc_daq.h"
#include "mvme_workspace.h"
#include "util/mesy_nng.h"
#include "util/qt_fs.h"
#include "util/stopwatch.h"
#include "util/thread_name.h"
#include "vme_config_scripts.h"
#include "vme_config_util.h"

using namespace mesytec::mvlc;
using namespace mesytec::nng;
using namespace mesytec::util;

namespace mesytec::mvme::multi_crate
{

MulticrateVMEConfig::MulticrateVMEConfig(QObject *parent)
    : ConfigObject(parent)
    , m_mergedConfig(new VMEConfig(this))
{
}

MulticrateVMEConfig::~MulticrateVMEConfig()
{
}

void MulticrateVMEConfig::addCrateConfig(VMEConfig *cfg)
{
    cfg->setParent(this);
    m_crateConfigs.push_back(cfg);
    emit crateConfigAdded(cfg);
    setModified();
}

void MulticrateVMEConfig::removeCrateConfig(VMEConfig *cfg)
{
    if (containsCrateConfig(cfg))
    {
        emit crateConfigAboutToBeRemoved(cfg);

        m_crateConfigs.erase(
            std::remove_if(std::begin(m_crateConfigs), std::end(m_crateConfigs),
                           [cfg] (const VMEConfig *c) { return cfg == c; }),
            std::end(m_crateConfigs));

        setModified();
    }
}

bool MulticrateVMEConfig::containsCrateConfig(const VMEConfig *cfg) const
{
    return (std::find_if(std::begin(m_crateConfigs), std::end(m_crateConfigs),
                         [cfg] (const VMEConfig *c) { return cfg == c; })
            != std::end(m_crateConfigs));
}

VMEConfig *MulticrateVMEConfig::getCrateConfig(int crateIndex) const
{
    try
    {
        return m_crateConfigs.at(crateIndex);
    }
    catch (const std::out_of_range &)
    { }

    return nullptr;
}

void MulticrateVMEConfig::setIsCrossCrateEvent(int eventIndex, bool isCrossCrate)
{
    if (isCrossCrate)
    {
        m_crossCrateEventIndexes.insert(eventIndex);
    }
    else
    {
        auto pos = m_crossCrateEventIndexes.find(eventIndex);
        if (pos != std::end(m_crossCrateEventIndexes))
            m_crossCrateEventIndexes.erase(pos);
    }
}

bool MulticrateVMEConfig::isCrossCrateEvent(int eventIndex) const
{
    return m_crossCrateEventIndexes.find(eventIndex) != std::end(m_crossCrateEventIndexes);
}

void MulticrateVMEConfig::setCrossCrateEventMainModuleId(int eventIndex, const QUuid &moduleId)
{
    m_crossCrateEventMainModules[eventIndex] = moduleId;
}

QUuid MulticrateVMEConfig::getCrossCrateEventMainModuleId(int eventIndex) const
{
    try
    {
        return m_crossCrateEventMainModules.at(eventIndex);
    }
    catch (const std::out_of_range &)
    {
        return {};
    }
}

void MulticrateVMEConfig::setObjectSettings(const QUuid &objectId, const QVariantMap &settings)
{
    bool modifies = (settings != getObjectSettings(objectId));
    m_objectSettings[objectId] = settings;
    if (modifies) setModified();
}

QVariantMap MulticrateVMEConfig::getObjectSettings(const QUuid &objectId) const
{
    return m_objectSettings.value(objectId);
}

void MulticrateVMEConfig::clearObjectSettings(const QUuid &objectId)
{
    m_objectSettings.remove(objectId);
    setModified();
}

QJsonObject to_json(const MultiCrateObjectMappings &mappings)
{
    QJsonObject dstJson;

    for (auto it=mappings.cratesToMerged.begin();
         it != mappings.cratesToMerged.end();
         ++it)
    {
        dstJson[it.key().toString()] = it.value().toString();
    }

    return dstJson;
}

MultiCrateObjectMappings object_mappings_from_json(const QJsonObject &json)
{
    MultiCrateObjectMappings ret;

    for (auto it=json.begin();
         it != json.end();
         ++it)
    {
        auto crateId = QUuid::fromString(it.key());
        auto mergedId = QUuid::fromString(it.value().toString());

        ret.insertMapping(crateId, mergedId);
    }

    return ret;
}

std::error_code MulticrateVMEConfig::write_impl(QJsonObject &json) const
{
    // Serialize crate configs and to a json array.
    QJsonArray cratesArray;
    for (auto crateConfig: getCrateConfigs())
    {
        QJsonObject dst;
        crateConfig->write(dst);
        cratesArray.append(dst);
    }

    json["crateConfigs"] = cratesArray;

    // cross crate event indexes
    QJsonArray crossEventsArray;
    for (auto it=std::begin(m_crossCrateEventIndexes); it!=std::end(m_crossCrateEventIndexes); ++it)
        crossEventsArray.append(static_cast<qint64>(*it));

    json["crossCrateEvents"] = crossEventsArray;

    // per event main/reference modules
    QJsonArray mainModulesArray;
    for (auto it=std::begin(m_crossCrateEventMainModules); it!=std::end(m_crossCrateEventMainModules); ++it)
    {
        QJsonObject dst;
        dst["eventIndex"] = static_cast<qint64>(it->first);
        dst["moduleId"] = it->second.toString();
        mainModulesArray.append(dst);
    }

    json["mainModules"] = mainModulesArray;

    // write out the merged vme config
    if (m_mergedConfig)
    {
        QJsonObject dst;
        m_mergedConfig->write(dst);
        json["mergedVMEConfig"] = dst;
    }

    // object mappings
    json["objectMappings"] = to_json(m_objectMappings);

    // object settings
    QJsonObject objectSettingsJson;

    for (auto it=m_objectSettings.begin();
         it!=m_objectSettings.end();
         ++it)
    {
        objectSettingsJson[it.key().toString()] = QJsonObject::fromVariantMap(it.value());
    }

    json["objectSettings"] = objectSettingsJson;

    return {};
}

std::error_code MulticrateVMEConfig::read_impl(const QJsonObject &json)
{
    auto cratesArray = json["crateConfigs"].toArray();

    for (int ci=0; ci<cratesArray.size(); ++ci)
    {
        auto jobj = cratesArray[ci].toObject();
        auto crateConfig = new VMEConfig(this);
        crateConfig->read(jobj);
        m_crateConfigs.push_back(crateConfig);
    }

    auto crossEventsArray = json["crossCrateEvents"].toArray();

    for (int i=0; i<crossEventsArray.size(); ++i)
        m_crossCrateEventIndexes.insert(crossEventsArray[i].toInt());

    auto mainModulesArray = json["mainModules"].toArray();

    for (int i=0; i<mainModulesArray.size(); ++i)
    {
        auto jobj = mainModulesArray[i].toObject();
        int eventIndex = jobj["eventIndex"].toInt();
        auto moduleId = QUuid::fromString(jobj["moduleId"].toString());
        m_crossCrateEventMainModules[eventIndex] = moduleId;
    }

    m_mergedConfig->read(json["mergedVMEConfig"].toObject());

    m_objectMappings = object_mappings_from_json(json["objectMappings"].toObject());

    auto objectSettingsJson = json["objectSettings"].toObject();

    for (auto it=objectSettingsJson.begin();
         it!=objectSettingsJson.end();
         ++it)
    {
        auto id = QUuid::fromString(it.key());
        auto settings = it.value().toObject().toVariantMap();

        m_objectSettings[id] = settings;
    }

    return {};
}

namespace
{

// Generates new QUuids for a hierarchy of ConfigObjects
void generate_new_ids(ConfigObject *parentObject)
{
    parentObject->generateNewId();

    for (auto child: parentObject->children())
        if (auto childObject = qobject_cast<ConfigObject *>(child))
            generate_new_ids(childObject);
}

// Copies a ConfigObject by first serializing to json, then creating the copy
// via deserialization. The copied object and its children are assigned new ids.
template<typename T>
std::unique_ptr<T> copy_config_object(const T *obj, bool generateNewIds = true)
{
    assert(obj);

    QJsonObject json;
    obj->write(json);

    auto ret = std::make_unique<T>();
    ret->read(json);

    if (generateNewIds)
        generate_new_ids(ret.get());

    return ret;
}

std::unique_ptr<ModuleConfig> copy_module_config(const ModuleConfig *src, bool generateNewIds = true)
{
    auto moduleCopy = copy_config_object(src, generateNewIds);

    // Symbol tables from innermost scope to outermost scope. Traverse them in
    // reverse order setting symbols on the moduleCopy object. This way the
    // symbols that had precendence due to scoping will overwrite symbols
    // defined in an outer scope.
    auto symtabs = mesytec::mvme::collect_symbol_tables(src);

    for (auto tabIter=symtabs.rbegin(); tabIter!=symtabs.rend(); ++tabIter)
    {
        auto &symbols = tabIter->symbols;

        for (auto varIter=symbols.begin(); varIter!=symbols.end(); ++varIter)
        {
            moduleCopy->setVariable(varIter.key(), varIter.value());
        }
    }

    return moduleCopy;
}

} // end anon namespace

std::pair<std::unique_ptr<VMEConfig>, MultiCrateObjectMappings> make_merged_vme_config(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents,
    const MultiCrateObjectMappings &prevMappings
    )
{
    assert(!crateConfigs.empty());

    MultiCrateObjectMappings mappings;
    std::vector<std::unique_ptr<EventConfig>> mergedEvents;

    // Create the cross crate merged events.
    // Mapping from crate events to merged events uses the main crates event id
    // as the source of the mapping.

    for (auto crossEventIndex: crossCrateEvents)
    {
        auto outEv = std::make_unique<EventConfig>();
        //outEv->setObjectName(QSL("event%1").arg(crossEventIndex));
        outEv->triggerCondition = TriggerCondition::TriggerIO;

        for (size_t ci=0; ci<crateConfigs.size(); ++ci)
        {
            auto crateEvent = crateConfigs[ci]->getEventConfig(crossEventIndex);

            if (!crateEvent)
                throw std::runtime_error(fmt::format(
                        "cross crate event {} not present in crate config {}",
                        crossEventIndex, ci));

            // Use the event name from the main crate for the cross event name
            // and for the mappings.
            if (ci == 0)
            {
                outEv->setObjectName(crateEvent->objectName());
                if (prevMappings.cratesToMerged.contains(crateEvent->getId()))
                    outEv->setId(prevMappings.cratesToMerged[crateEvent->getId()]);
                mappings.insertMapping(crateEvent, outEv.get());
            }

            auto moduleConfigs = crateEvent->getModuleConfigs();

            for (auto moduleConf: moduleConfigs)
            {
                auto moduleCopy = copy_module_config(moduleConf);

                // Reuse the previously mapped module id.
                if (prevMappings.cratesToMerged.contains(moduleConf->getId()))
                    moduleCopy->setId(prevMappings.cratesToMerged[moduleConf->getId()]);
                mappings.insertMapping(moduleConf, moduleCopy.get());
                outEv->addModuleConfig(moduleCopy.release());
            }
        }

        mergedEvents.emplace_back(std::move(outEv));
    }

    // Copy over the non-merged events from each of the crate configs.

    std::vector<std::unique_ptr<EventConfig>> singleCrateEvents;

    for (size_t ci=0; ci<crateConfigs.size(); ++ci)
    {
        auto crateConf = crateConfigs[ci];
        auto crateEvents = crateConf->getEventConfigs();

        for (int ei=0; ei<crateEvents.size(); ++ei)
        {
            auto eventConf = crateEvents[ei];

            if (!crossCrateEvents.count(ei))
            {
                // Create a recursive copy of the event, then update the mappings table by
                // iterating over the modules.
                auto outEv = copy_config_object(eventConf);
                outEv->setObjectName(QSL("crate%1_%2")
                                     .arg(ci)
                                     .arg(eventConf->objectName())
                                     );

                if (prevMappings.cratesToMerged.contains(eventConf->getId()))
                    outEv->setId(prevMappings.cratesToMerged[eventConf->getId()]);

                mappings.insertMapping(eventConf, outEv.get());

                assert(eventConf->getModuleConfigs().size() == outEv->getModuleConfigs().size());

                for (int mi=0; mi<eventConf->moduleCount(); ++mi)
                {
                    auto inMod = eventConf->getModuleConfigs().at(mi);
                    auto outMod = outEv->getModuleConfigs().at(mi);

                    if (prevMappings.cratesToMerged.contains(inMod->getId()))
                        outMod->setId(prevMappings.cratesToMerged[inMod->getId()]);

                    mappings.insertMapping(inMod, outMod);
                }

                singleCrateEvents.emplace_back(std::move(outEv));
            }
        }
    }

    auto merged = std::make_unique<VMEConfig>();

    // Add the merged events first, then the non-merged ones.

    for (auto &eventConf: mergedEvents)
        merged->addEventConfig(eventConf.release());

    for (auto &eventConf: singleCrateEvents)
        merged->addEventConfig(eventConf.release());

    return std::make_pair(std::move(merged), mappings);
}

std::unique_ptr<VMEConfig> make_merged_vme_config_keep_ids(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents)
{
    assert(!crateConfigs.empty());

    std::vector<std::unique_ptr<EventConfig>> mergedEvents;

    // Create the cross crate merged events.
    for (auto crossEventIndex: crossCrateEvents)
    {
        auto outEv = std::make_unique<EventConfig>();
        //outEv->setObjectName(QSL("event%1").arg(crossEventIndex));
        outEv->triggerCondition = TriggerCondition::TriggerIO;

        for (size_t ci=0; ci<crateConfigs.size(); ++ci)
        {
            auto crateEvent = crateConfigs[ci]->getEventConfig(crossEventIndex);

            if (!crateEvent)
                throw std::runtime_error(fmt::format(
                        "cross crate event {} not present in crate config {}",
                        crossEventIndex, ci));

            // Use the event name from the main crate for the cross event name
            // and for the mappings.
            if (ci == 0)
            {
                outEv->setObjectName(crateEvent->objectName());
            }

            auto moduleConfigs = crateEvent->getModuleConfigs();

            for (auto moduleConf: moduleConfigs)
            {
                auto moduleCopy = copy_module_config(moduleConf, false);
                outEv->addModuleConfig(moduleCopy.release());
            }
        }

        mergedEvents.emplace_back(std::move(outEv));
    }

    // Copy over the non-merged events from each of the crate configs.

    std::vector<std::unique_ptr<EventConfig>> singleCrateEvents;

    for (size_t ci=0; ci<crateConfigs.size(); ++ci)
    {
        auto crateConf = crateConfigs[ci];
        auto crateEvents = crateConf->getEventConfigs();

        for (int ei=0; ei<crateEvents.size(); ++ei)
        {
            auto eventConf = crateEvents[ei];

            if (!crossCrateEvents.count(ei))
            {
                // Create a recursive copy of the event, then update the mappings table by
                // iterating over the modules.
                auto outEv = copy_config_object(eventConf);
                outEv->setObjectName(QSL("crate%1_%2")
                                     .arg(ci)
                                     .arg(eventConf->objectName())
                                     );

                assert(eventConf->getModuleConfigs().size() == outEv->getModuleConfigs().size());

                for (int mi=0; mi<eventConf->moduleCount(); ++mi)
                {
                    auto inMod = eventConf->getModuleConfigs().at(mi);
                    auto outMod = outEv->getModuleConfigs().at(mi);
                }

                singleCrateEvents.emplace_back(std::move(outEv));
            }
        }
    }

    auto merged = std::make_unique<VMEConfig>();

    // Add the merged events first, then the non-merged ones.

    for (auto &eventConf: mergedEvents)
        merged->addEventConfig(eventConf.release());

    for (auto &eventConf: singleCrateEvents)
        merged->addEventConfig(eventConf.release());

    return std::move(merged);
}

void multi_crate_playground()
{
    CrateReadout crateReadout;

    CrateReadout crateReadout2(std::move(crateReadout));
}

MulticrateTemplates read_multicrate_templates()
{
    MulticrateTemplates result;
    auto dir = QDir(vats::get_template_path());

    result.mainStartEvent = vme_config::eventconfig_from_file(dir.filePath("multicrate/main_start_event.mvmeevent"));
    result.secondaryStartEvent = vme_config::eventconfig_from_file(dir.filePath("multicrate/secondary_start_event.mvmeevent"));
    result.stopEvent = vme_config::eventconfig_from_file(dir.filePath("multicrate/stop_event.mvmeevent"));
    result.dataEvent = vme_config::eventconfig_from_file(dir.filePath("multicrate/data_event0.mvmeevent"));

    result.setMasterModeScript = read_text_file(dir.filePath("multicrate/set_master_mode.vmescript"));
    result.setSlaveModeScript  = read_text_file(dir.filePath("multicrate/set_slave_mode.vmescript"));
    result.triggerIoScript     = read_text_file(dir.filePath("multicrate/mvlc_trigger_io.vmescript"));

    return result;
}

std::unique_ptr<MulticrateVMEConfig> make_multicrate_config(size_t numCrates)
{
    auto templates = multi_crate::read_multicrate_templates();
    auto result = std::make_unique<MulticrateVMEConfig>();

    {
        auto mainCrate = std::make_unique<VMEConfig>();
        mainCrate->setObjectName("crate0");
        mainCrate->setVMEController(VMEControllerType::MVLC_ETH);
        mainCrate->addEventConfig(vme_config::clone_config_object(*templates.mainStartEvent).release());
        mainCrate->addEventConfig(vme_config::clone_config_object(*templates.stopEvent).release());
        mainCrate->addEventConfig(vme_config::clone_config_object(*templates.dataEvent).release());
        auto setModeScript = std::make_unique<VMEScriptConfig>();
        setModeScript->setObjectName("set master mode");
        setModeScript->setScriptContents(templates.setMasterModeScript);
        mainCrate->addGlobalScript(setModeScript.release(), "daq_start");
        if (auto triggerIo = mainCrate->getMVLCTriggerIOScript())
            triggerIo->setScriptContents(templates.triggerIoScript);
        result->addCrateConfig(mainCrate.release());
    }

    for (size_t crateId = 1; crateId < numCrates; ++crateId)
    {
        auto crateConfig = std::make_unique<VMEConfig>();
        crateConfig->setObjectName(fmt::format("crate{}", crateId).c_str());
        crateConfig->setVMEController(VMEControllerType::MVLC_ETH);
        crateConfig->addEventConfig(vme_config::clone_config_object(*templates.secondaryStartEvent).release());
        crateConfig->addEventConfig(vme_config::clone_config_object(*templates.stopEvent).release());
        crateConfig->addEventConfig(vme_config::clone_config_object(*templates.dataEvent).release());
        auto setModeScript = std::make_unique<VMEScriptConfig>();
        setModeScript->setObjectName("set slave mode");
        setModeScript->setScriptContents(templates.setSlaveModeScript);
        crateConfig->addGlobalScript(setModeScript.release(), "daq_start");
        if (auto triggerIo = crateConfig->getMVLCTriggerIOScript())
            triggerIo->setScriptContents(templates.triggerIoScript);
        result->addCrateConfig(crateConfig.release());
    }

    return result;
}

int send_shutdown_message(nng_socket socket)
{
    multi_crate::BaseMessageHeader header{};
    header.messageType = multi_crate::MessageType::GracefulShutdown;
    auto msg = nng::alloc_message(sizeof(header));
    std::memcpy(nng_msg_body(msg), &header, sizeof(header));
    if (int res = nng::send_message_retry(socket, msg))
    {
        nng_msg_free(msg);
        return res;
    }

    return 0;
}

void send_shutdown_messages(std::initializer_list<nng_socket> sockets)
{
    for (auto socket: sockets)
    {
        send_shutdown_message(socket);
    }
}

bool LIBMVME_EXPORT is_shutdown_message(nng_msg *msg)
{
    const auto msgLen = nng_msg_len(msg);

    if (msgLen >= sizeof(multi_crate::BaseMessageHeader))
    {
        auto header = *reinterpret_cast<multi_crate::BaseMessageHeader *>(nng_msg_body(msg));
        return (header.messageType == multi_crate::MessageType::GracefulShutdown);
    }

    return false;
}

size_t fixup_listfile_buffer_message(const mvlc::ConnectionType &bufferType, nng_msg *msg, std::vector<u8> &tmpBuf)
{
    size_t bytesMoved = 0u;
    const u8 *msgBufferData = reinterpret_cast<const u8 *>(nng_msg_body(msg)) + sizeof(ReadoutDataMessageHeader);
    const auto msgBufferSize = nng_msg_len(msg) - sizeof(ReadoutDataMessageHeader);

    if (bufferType == mvlc::ConnectionType::USB)
        bytesMoved = mvlc::fixup_buffer_mvlc_usb(msgBufferData, msgBufferSize, tmpBuf);
    else
        bytesMoved = mvlc::fixup_buffer_mvlc_eth(msgBufferData, msgBufferSize, tmpBuf);

    nng_msg_chop(msg, bytesMoved);

    return bytesMoved;
}

struct TimetickGenerator
{
    public:
        const std::chrono::seconds TimetickInterval = std::chrono::seconds(1);

        void readoutStart(listfile::WriteHandle &wh, u8 crateId)
        {
            // Write the initial timestamp in a BeginRun section
            listfile_write_timestamp_section(wh, crateId, system_event::subtype::BeginRun);
            tLastTick_ = std::chrono::steady_clock::now();
        }

        void readoutStop(listfile::WriteHandle &wh, u8 crateId)
        {
            // Write the final timestamp in an EndRun section.
            listfile_write_timestamp_section(wh, crateId, system_event::subtype::EndRun);
        }

        void operator()(listfile::WriteHandle &wh, u8 crateId)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - tLastTick_;

            if (elapsed >= TimetickInterval)
            {
                listfile_write_timestamp_section(wh, crateId, system_event::subtype::UnixTimetick);
                tLastTick_ = now;
            }
        }

    private:
        std::chrono::time_point<std::chrono::steady_clock> tLastTick_ = {};
};

void allocate_prepare_output_message(ReadoutProducerContext &context, ReadoutDataMessageHeader &header)
{
    // Header + space for data plus some margin so that readout_usb() does not
    // have to realloc.
    constexpr size_t allocSize = sizeof(header) +  mvlc::util::Megabytes(1) + 256;

    if (auto res = allocate_reserve_message(&context.outputMessage, allocSize))
        throw std::runtime_error(fmt::format("mvlc_readout_loop: error allocating output message: {}", nng_strerror(res)));

    if (auto res = nng_msg_append(context.outputMessage, &header, sizeof(header)))
        throw std::runtime_error(fmt::format("mvlc_readout_loop: error allocating output message: {}", nng_strerror(res)));

    context.msgWriteHandle.setMessage(context.outputMessage);
    ++header.messageNumber;
}

void flush_output_message(ReadoutProducerContext &context)
{
    if (auto res = nng_sendmsg(context.outputSocket, context.outputMessage, 0))
        throw std::runtime_error(fmt::format("mvlc_readout_loop: error flushing output message: {}", nng_strerror(res)));

    context.outputMessage = nullptr;
    context.msgWriteHandle.setMessage(nullptr);
}

static constexpr std::chrono::milliseconds FlushBufferTimeout(500);
static const size_t DefaultOutputMessageReserve = mvlc::util::Megabytes(1) + sizeof(multi_crate::ReadoutDataMessageHeader);

// Assumptions:
// - the data pipe is locked, so read_unbuffered() can be called freely
// - the output message has enough reserved space available to store
//   usb::USBStreamPipeReadSize bytes.
std::error_code readout_usb(
    usb::MVLC_USB_Interface *mvlcUSB,
    nng_msg *msg,
    size_t &totalBytesTransferred,
    std::vector<u8> &tmpBuf)
{
    assert(allocated_free_space(msg) >= usb::USBStreamPipeReadSize);

    auto tStart = std::chrono::steady_clock::now();
    size_t msgUsed = nng_msg_len(msg);
    size_t msgCapacity = nng_msg_capacity(msg);

    // Resize the message to the reserved space. This does not cause a realloc.
    nng_msg_realloc(msg, msgCapacity);

    while ((msgCapacity - msgUsed) >= usb::USBStreamPipeReadSize)
    {
        if (auto elapsed = std::chrono::steady_clock::now() - tStart;
            elapsed >= FlushBufferTimeout)
        {
            break;
        }

        size_t bytesTransferred = 0u;

        auto ec = mvlcUSB->read_unbuffered(
            Pipe::Data,
            static_cast<u8 *>(nng_msg_body(msg)) + msgUsed,
            usb::USBStreamPipeReadSize,
            bytesTransferred);

        if (ec == ErrorType::ConnectionError)
            return ec;

        msgUsed += bytesTransferred;
        totalBytesTransferred += bytesTransferred;
    }

    // Resize the message to the space that's actually used.
    nng_msg_realloc(msg, msgUsed);

    // Move trailing data to tmpBuf
    const u8 *msgBufferData = reinterpret_cast<const u8 *>(nng_msg_body(msg))
        + sizeof(ReadoutDataMessageHeader);
    const auto msgBufferSize = nng_msg_len(msg) - sizeof(ReadoutDataMessageHeader);

    size_t bytesMoved = fixup_buffer_mvlc_usb(msgBufferData, msgBufferSize, tmpBuf);

    nng_msg_chop(msg, bytesMoved);

    return {};
}

// Assumptions:
// - the data pipe is locked, so read_packet() can be called freely
// - the output message has reserved space available to store packet data
std::error_code readout_eth(
    eth::MVLC_ETH_Interface *mvlcETH,
    nng_msg *msg,
    size_t &totalBytesTransferred)
{
    auto tStart = std::chrono::steady_clock::now();
    size_t msgUsed = nng_msg_len(msg);
    size_t msgCapacity = nng_msg_capacity(msg);

    // Resize the message to the reserved space. This does not cause a realloc.
    nng_msg_realloc(msg, msgCapacity);

    while ((msgCapacity - msgUsed) >= eth::JumboFrameMaxSize)
    {
        if (auto elapsed = std::chrono::steady_clock::now() - tStart;
            elapsed >= FlushBufferTimeout)
        {
            break;
        }

        auto readResult = mvlcETH->read_packet(
            Pipe::Data,
            static_cast<u8 *>(nng_msg_body(msg)) + msgUsed,
            msgCapacity - msgUsed);

        if (readResult.ec == ErrorType::ConnectionError)
            return readResult.ec;

        if (readResult.ec == MVLCErrorCode::ShortRead)
        {
            // TODO: counters.access()->ethShortReads++;
            continue;
        }

        msgUsed += readResult.bytesTransferred;
        totalBytesTransferred += readResult.bytesTransferred;
        // TODO: count_stack_hits(result, stackHits);
    }

    // Resize the message to the space that's actually used.
    nng_msg_realloc(msg, msgUsed);

    //
    return {};
}

void mvlc_readout_loop(ReadoutProducerContext &context, std::atomic<bool> &quit) // throws on error
{
    auto logger = mvlc::get_logger("mvlc_readout_loop");
    logger->info("mvlc_readout_loop starting for crate{} with MVLC {}", context.crateId, context.mvlc.connectionInfo());

    std::vector<u8> tmpBuf;
    auto &mvlc = context.mvlc;
    eth::MVLC_ETH_Interface *mvlcEth = nullptr;
    usb::MVLC_USB_Interface *mvlcUsb = nullptr;

    if (mvlc.connectionType() == ConnectionType::ETH)
        mvlcEth = dynamic_cast<eth::MVLC_ETH_Interface *>(mvlc.getImpl());
    else if (mvlc.connectionType() == ConnectionType::USB)
        mvlcUsb = dynamic_cast<usb::MVLC_USB_Interface *>(mvlc.getImpl());

    if (!mvlcEth && !mvlcUsb)
        throw std::runtime_error("Could not determine MVLC type. Expected USB or ETH.");

    ReadoutDataMessageHeader header{};
    header.messageType = MessageType::ReadoutData;
    header.messageNumber = 1;
    // TODO: don't actually need to know the buffer type. Can detect when parsing.
    header.bufferType = static_cast<u32>(mvlcEth ? ConnectionType::ETH : ConnectionType::USB);

    allocate_prepare_output_message(context, header);
    TimetickGenerator timetickGen;
    timetickGen.readoutStart(context.msgWriteHandle, context.crateId);

    while (!quit)
    {
        if (!context.outputMessage)
            allocate_prepare_output_message(context, header);

        // run the timetick generator
        timetickGen(context.msgWriteHandle, context.crateId);

        // move trailing data from last readout cycle to the current output message
        nng_msg_append(context.outputMessage, tmpBuf.data(), tmpBuf.size());
        tmpBuf.clear();

        // The actual readout. The readout_* functions will return when either
        // the output message is filled up to the reserved space or the flush
        // buffer timeout is exceeded.
        {
            auto dataGuard = mvlc.getLocks().lockData();
            std::error_code ec;
            size_t bytesTransferred = 0u;

            if (mvlcEth)
                ec = readout_eth(mvlcEth, context.outputMessage, bytesTransferred);
            else
                ec = readout_usb(mvlcUsb, context.outputMessage, bytesTransferred, tmpBuf);

            // TODO: handle ec: timeouts, fatal errors

            flush_output_message(context);
        }
    }

    timetickGen.readoutStop(context.msgWriteHandle, context.crateId);
}

void mvlc_readout_consumer(ReadoutConsumerContext &context, std::atomic<bool> &quit)
{
    u32 lastMessageNumber = 0;

    while (!quit)
    {
        nng_msg *msg = {};

        if (auto res = receive_message(context.inputSocket, &msg, 0))
        {
            if (res != NNG_ETIMEDOUT)
                throw std::runtime_error(fmt::format("mvlc_readout_consumer: internal error: {}", nng_strerror(res)));
            continue;
        }

        if (nng_msg_len(msg) < sizeof(ReadoutDataMessageHeader))
        {
            // TODO: count this error (should not happen)
            nng_msg_free(msg);
            msg = {};
            continue;
        }

        auto header = *reinterpret_cast<ReadoutDataMessageHeader *>(nng_msg_body(msg));
        if (auto loss = readout_parser::calc_buffer_loss(header.messageNumber, lastMessageNumber))
            spdlog::warn("mvlc_readout_consumer: lost {} messages!", loss);
        lastMessageNumber = header.messageNumber;
        nng_msg_trim(msg, sizeof(ReadoutDataMessageHeader));

        auto dataPtr = reinterpret_cast<const u32 *>(nng_msg_body(msg));
        size_t dataSize = nng_msg_len(msg) / sizeof(u32);

        std::basic_string_view<u32> dataView(dataPtr, dataSize);
    }
}

QString MinimalAnalysisServiceProvider::getWorkspaceDirectory()
{
    return {};
}

QString MinimalAnalysisServiceProvider::getWorkspacePath(
    const QString &settingsKey,
    const QString &defaultValue,
    bool setIfDefaulted) const
{
    return QDir().absolutePath();
}

std::shared_ptr<QSettings> MinimalAnalysisServiceProvider::makeWorkspaceSettings() const
{
    return make_workspace_settings(QDir().absolutePath());
}

// VMEConfig
VMEConfig *MinimalAnalysisServiceProvider::getVMEConfig()
{
    return vmeConfig_;
}

QString MinimalAnalysisServiceProvider::getVMEConfigFilename()
{
    return vmeConfigFilename_;
}

void MinimalAnalysisServiceProvider::setVMEConfigFilename(const QString &filename)
{
    vmeConfigFilename_ = filename;
}

void MinimalAnalysisServiceProvider::vmeConfigWasSaved()
{
}

// Analysis
analysis::Analysis *MinimalAnalysisServiceProvider::getAnalysis()
{
    return analysis_.get();
}
QString MinimalAnalysisServiceProvider::getAnalysisConfigFilename()
{
    return analysisConfigFilename_;
}
void MinimalAnalysisServiceProvider::setAnalysisConfigFilename(const QString &filename)
{
    analysisConfigFilename_ = filename;
}
void MinimalAnalysisServiceProvider::analysisWasSaved()
{
}
void MinimalAnalysisServiceProvider::analysisWasCleared()
{
}
void MinimalAnalysisServiceProvider::stopAnalysis()
{
}
void MinimalAnalysisServiceProvider::resumeAnalysis(analysis::Analysis::BeginRunOption runOption)
{
}
bool MinimalAnalysisServiceProvider::loadAnalysisConfig(const QString &filename)
{
}
bool MinimalAnalysisServiceProvider::loadAnalysisConfig(const QJsonDocument &doc, const QString &inputInfo,
    AnalysisLoadFlags flags)
{
}

// Widget registry
mesytec::mvme::WidgetRegistry *MinimalAnalysisServiceProvider::getWidgetRegistry()
{
    return widgetRegistry_.get();
}

// Worker states
AnalysisWorkerState MinimalAnalysisServiceProvider::getAnalysisWorkerState()
{
    return AnalysisWorkerState::Idle;
}
StreamWorkerBase *MinimalAnalysisServiceProvider::getMVMEStreamWorker()
{
    return nullptr;
}

void MinimalAnalysisServiceProvider::logMessage(const QString &msg)
{
    spdlog::info("MinimalAnalysisServiceProvider: {}", msg.toStdString());
}

GlobalMode MinimalAnalysisServiceProvider::getGlobalMode() // DAQ or Listfile
{
    return GlobalMode::DAQ;
}

const ListfileReplayHandle &MinimalAnalysisServiceProvider::getReplayFileHandle() const
{
    return listfileReplayHandle_;
}

DAQStats MinimalAnalysisServiceProvider::getDAQStats() const
{
    return daqStats_.copy();
}

RunInfo MinimalAnalysisServiceProvider::getRunInfo() const
{
    return runInfo_;
}

DAQState MinimalAnalysisServiceProvider::getDAQState() const
{
    return DAQState::Running;
}

void MinimalAnalysisServiceProvider::addAnalysisOperator(QUuid eventId, const std::shared_ptr<analysis::OperatorInterface> &op,
    s32 userLevel)
{
}

void MinimalAnalysisServiceProvider::setAnalysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op)
{
}

void log_socket_work_counters(const SocketWorkPerformanceCounters &counters, const std::string &info)
{
    spdlog::info("{}: time budget: "
                "tReceive = {} ms, "
                "tProcess = {} ms, "
                "tSend = {} ms, "
                "tTotal = {} ms",
                info,
                counters.tReceive.count() / 1000.0,
                counters.tProcess.count() / 1000.0,
                counters.tSend.count() / 1000.0,
                counters.tTotal.count() / 1000.0);

    auto mibReceived = counters.bytesReceived * 1.0 / mvlc::util::Megabytes(1);
    auto mibSent = counters.bytesSent * 1.0 / mvlc::util::Megabytes(1);
    auto totalMessages = counters.messagesReceived + counters.messagesLost;
    auto efficiency = counters.messagesReceived * 1.0 / totalMessages;

    spdlog::info("{}: stats: msgsReceived={}, msgsLost={} (efficiency={:.2f}), msgsSent={}, bytesReceived={:.2f} MiB, bytesSent={:.2f} MiB",
        info, counters.messagesReceived, counters.messagesLost, efficiency, counters.messagesSent, mibReceived, mibSent);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - counters.tpStart);

    auto msgReceiveRate = counters.messagesReceived * 1.0 / elapsed.count() * 1000.0;
    auto msgSendRate =  counters.messagesSent * 1.0 / elapsed.count() * 1000.0;
    auto bytesReceiveRate = mibReceived / elapsed.count() * 1000.0;
    auto bytesSendRate = mibSent / elapsed.count() * 1000.0;

    spdlog::info("{}: rates: elapsed={:.2f} s, msgReceiveRate={:.2f} msgs/s, msgSendRate={:.2f} msgs/s, "
        " bytesReceivedRate={:.2f} MiB/s, bytesSentRate={:.2f} MiB/s",
        info, elapsed.count() / 1000.0, msgReceiveRate, msgSendRate, bytesReceiveRate, bytesSendRate);
}

void mvlc_eth_readout_loop(MvlcEthReadoutLoopContext &context)
{
    set_thread_name("mvlc_eth_readout_loop");

    spdlog::info("entering mvlc_eth_readout_loop, crateId={}", context.crateId);

    // Listfile handle to pass to ReadoutLoopPlugins.
    multi_crate::NngMsgWriteHandle lfh;
    u32 messageNumber = 1u;
    std::vector<u8> previousData;
    s32 lastPacketNumber = -1;

    auto new_output_message = [&] () -> nng_msg *
    {
        nng_msg *msg = {};

        if (auto res = nng::allocate_reserve_message(&msg, DefaultOutputMessageReserve))
        {
            spdlog::error("mvlc_eth_readout_loop: could not allocate nng output message: {}", nng_strerror(res));
            return nullptr;
        }

        lfh.setMessage(msg);

        multi_crate::ReadoutDataMessageHeader header{};
        header.messageType = multi_crate::MessageType::ReadoutData;
        header.messageNumber = messageNumber++;
        header.bufferType = static_cast<u32>(mvlc::ConnectionType::ETH);
        header.crateId = context.crateId;

        {
            auto messageNumber = header.messageNumber; // fix for gcc + spdlog/fmt + packed struct members
            spdlog::debug("mvlc_eth_readout_loop: preparing new output message: crateId={}, messageNumber={}",
                header.crateId, messageNumber);
        }

        nng_msg_append(msg, &header, sizeof(header));
        assert(nng_msg_len(msg) == sizeof(header));

        nng_msg_append(msg, previousData.data(), previousData.size());
        previousData.clear();

        return msg;
    };

    auto flush_output_message = [&] (nng_msg *msg, MvlcEthReadoutLoopContext &ctx) -> int
    {
        multi_crate::fixup_listfile_buffer_message(mvlc::ConnectionType::ETH, msg, previousData);
        nng_msg *msgClone = nullptr;
        const size_t msgLen = nng_msg_len(msg);

        if (nng_socket_id(context.snoopOutputSocket) >= 0)
        {
            if (auto res = nng_msg_dup(&msgClone, msg))
            {
                spdlog::error("mvlc_eth_readout_loop: could not allocate nng output message");
                return res;
            }
        }

        // Retry until the external quit flag is set.
        auto retryPredicate = [&]
        {
            return !context.quit;
        };

        // The main (blocking) output socket. listfile_writer or similar reads
        // from the other end.
        if (auto res = nng::send_message_retry(context.dataOutputSocket, msg, retryPredicate))
        {
            nng_msg_free(msg);
            nng_msg_free(msgClone);
            return res;
        }

        {
            auto ca = ctx.dataOutputCounters.access();
            ca->messagesSent += 1;
            ca->bytesSent += msgLen;
        }

        // Also write to the snoop socket if one was setup. Only one send attempt.
        if (msgClone)
        {
            if (auto res = nng::send_message_retry(context.snoopOutputSocket, msgClone, 1))
            {
                spdlog::warn("mvlc_eth_readout_loop: could not write to snoop output: {}", nng_strerror(res));
                nng_msg_free(msgClone);
            }

            auto ca = ctx.snoopOutputCounters.access();
            ca->messagesSent += 1;
            ca->bytesSent += msgLen;
        }

        return 0;
    };

    ReadoutLoopPlugin::Arguments pluginArgs{};
    pluginArgs.crateId = context.crateId;
    pluginArgs.listfileHandle = &lfh;

    std::vector<std::unique_ptr<ReadoutLoopPlugin>> readoutLoopPlugins;
    readoutLoopPlugins.emplace_back(std::make_unique<TimetickPlugin>());

    auto msg = new_output_message();

    for (auto &plugin: readoutLoopPlugins)
        plugin->readoutStart(pluginArgs);

    auto tLastFlush = std::chrono::steady_clock::now();
    context.dataOutputCounters.access()->start();
    context.snoopOutputCounters.access()->start();

    while (!context.quit)
    {
        if (!msg || nng::allocated_free_space(msg) < eth::JumboFrameMaxSize)
        {
            if (flush_output_message(msg, context) != 0)
                return; // FIXME: error log or return

            msg = new_output_message();

            if (!msg) return; // FIXME: error log or return
        }

        if (nng::allocated_free_space(msg) < eth::JumboFrameMaxSize)
        {
            spdlog::error("should not happen!");
            std::abort();
        }

        auto msgUsed = nng_msg_len(msg);

        // Note: this should not alloc as we reserved space when the message was
        // created. This just increases the size of the message.
        nng_msg_realloc(msg, msgUsed + eth::JumboFrameMaxSize);

        size_t bytesTransferred = 0;
        auto ec = eth::receive_one_packet(
            context.mvlcDataSocket,
            reinterpret_cast<u8 *>(nng_msg_body(msg)) + msgUsed,
            eth::JumboFrameMaxSize, bytesTransferred, 100);

        if (ec)
        {
            spdlog::warn("Error reading from mvlc data socket: {}", ec.message());
            continue;
        }

        assert(bytesTransferred <= eth::JumboFrameMaxSize);

        if (bytesTransferred > 0)
        {
            eth::PacketReadResult prr{};
            prr.buffer = reinterpret_cast<u8 *>(nng_msg_body(msg)) + msgUsed;
            prr.bytesTransferred = bytesTransferred;

            if (prr.hasHeaders())
            {
                //spdlog::debug("mvlc_eth_readout_loop (crate{}): incoming packet: packetNumber={}, crateId={}, size={} bytes",
                //    context.crateId, prr.packetNumber(), prr.controllerId(), prr.bytesTransferred);
            }

            if (!prr.hasHeaders())
            {
                spdlog::warn("crate{}: no valid headers in received packet of size {}. Dropping the packet!",
                    context.crateId, prr.bytesTransferred);
            }
            else
            {
                if (prr.controllerId() != context.crateId)
                {
                    //spdlog::warn("crate{}: incoming data packet has crateId={} set, excepted {}. Dropping the packet!",
                    //    context.crateId, prr.controllerId(), context.crateId);
                }

                if (lastPacketNumber >= 0)
                {
                    if (auto loss = eth::calc_packet_loss(
                        lastPacketNumber, prr.packetNumber()))
                    {
                        spdlog::warn("crate{}: lost {} incoming data packets!",
                            context.crateId, loss);
                    }
                }

                // Update the message size. This should not alloc as we can only shrink
                // the message here.
                nng_msg_realloc(msg, msgUsed + bytesTransferred);

                // Cross check size here.
                assert(nng_msg_len(msg) == msgUsed + bytesTransferred);
            }
        }

        // Run plugins (currently only timetick generation here).
        for (const auto &plugin: readoutLoopPlugins)
        {
            plugin->operator()(pluginArgs);
        }

        // Check if either the flush timeout elapsed or there is no more space
        // for packets in the output message.
        if (auto elapsed = std::chrono::steady_clock::now() - tLastFlush;
            elapsed >= FlushBufferTimeout || nng::allocated_free_space(msg) < eth::JumboFrameMaxSize)
        {
            if (nng::allocated_free_space(msg) < eth::JumboFrameMaxSize)
                spdlog::trace("crate{}: flushing full output message #{}", context.crateId, messageNumber - 1);
            else
                spdlog::trace("crate{}: flushing output message #{} due to timeout", context.crateId, messageNumber - 1);


            if (flush_output_message(msg, context) != 0)
                return;

            msg = new_output_message();

            if (!msg)
                return;

            tLastFlush = std::chrono::steady_clock::now();
        }
    }

    for (auto &plugin: readoutLoopPlugins)
        plugin->readoutStop(pluginArgs);

    spdlog::info("leaving mvlc_eth_readout_loop, crateId={}", context.crateId);

    // Important: no sentinel has been sent at this point!
}

void listfile_writer_loop(ListfileWriterContext &context)
{
    set_thread_name("listfile_writer_loop");

    spdlog::info("entering listfile_writer_loop");

    // Last received message number per crate.
    std::array<u32, MaxVMECrates> lastMessageNumbers;

    lastMessageNumbers.fill(0);

    {
        auto ca = context.dataInputCounters.access();
        for (auto &counters: ca.ref())
            counters.start();
    }

    while (!context.quit)
    {
        StopWatch stopWatch;
        stopWatch.start();
        nng_msg *msg = nullptr;

        if (auto res = nng::receive_message(context.dataInputSocket, &msg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::warn("listfile_writer_loop: Error reading from data input socket: {}", nng_strerror(res));
                break;;
            }
            continue;
        }

        auto tReceive = stopWatch.interval();

        assert(msg != nullptr);

        const auto msgLen = nng_msg_len(msg);

        // Check for shutdown message.
        if (msgLen >= sizeof(multi_crate::BaseMessageHeader))
        {
            auto header = *reinterpret_cast<multi_crate::BaseMessageHeader *>(nng_msg_body(msg));
            if (header.messageType == multi_crate::MessageType::GracefulShutdown)
            {
                spdlog::warn("listfile_writer_loop: Received shutdown message, leaving loop");
                break;
            }
        }

        if (msgLen < sizeof(multi_crate::ReadoutDataMessageHeader))
        {
            spdlog::warn("listfile_writer_loop: Incoming message is too short!");
            // TODO: count this error (should not happen)
            nng_msg_free(msg);
            continue;
        }

        auto header = *reinterpret_cast<multi_crate::ReadoutDataMessageHeader *>(nng_msg_body(msg));

        if (header.crateId > frame_headers::CtrlIdMask)
        {
            spdlog::warn("listfile_writer_loop: Invalid crateId={} in incoming data packet!", header.crateId);
            nng_msg_free(msg);
            continue;
        }

        auto messageNumber = header.messageNumber;
        auto messageLoss = readout_parser::calc_buffer_loss(header.messageNumber, lastMessageNumbers[header.crateId]);
        lastMessageNumbers[header.crateId] = header.messageNumber;

        if (messageLoss > 0)
        {
            spdlog::warn("listfile_writer_loop: Lost {} messages from crate{}! (header.messageNumber={}, lastMessageNumber={}",
                messageLoss, header.crateId, messageNumber, lastMessageNumbers[header.crateId]);
        }


        lastMessageNumbers[header.crateId] = header.messageNumber;

        // Trim off the header from the front of the message. The rest of the
        // message is pure readout data and added system event frames.
        nng_msg_trim(msg, sizeof(multi_crate::ReadoutDataMessageHeader));

        auto dataPtr = reinterpret_cast<const u32 *>(nng_msg_body(msg));
        size_t dataSize = nng_msg_len(msg) / sizeof(u32);
        std::basic_string_view<u32> dataView(dataPtr, dataSize);

        if (context.lfh)
        {
            try
            {
                context.lfh->write(reinterpret_cast<const u8 *>(nng_msg_body(msg)), nng_msg_len(msg));
            }
            catch(const std::exception& e)
            {
                spdlog::warn("listfile_writer_loop: Error writing to output listfile: {}", e.what());
            }
        }

        nng_msg_free(msg);

        auto tProcess = stopWatch.interval();
        auto tTotal = stopWatch.end();

        {
            auto ca = context.dataInputCounters.access();
            auto &counters = ca.ref()[header.crateId];
            counters.bytesReceived += msgLen;
            counters.messagesLost += messageLoss;
            counters.messagesReceived += 1;
            counters.tReceive += tReceive;
            counters.tProcess += tProcess;
            counters.tTotal += tTotal;
        }
    }

    spdlog::info("leaving listfile_writer_loop");
}

// Serialize the given module data list into the current output message.
bool ParsedEventsMessageWriter::consumeReadoutEventData(int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    size_t requiredBytes = sizeof(multi_crate::ParsedDataEventHeader);

    for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        auto &moduleData = moduleDataList[moduleIndex];
        requiredBytes += sizeof(multi_crate::ParsedModuleHeader);
        requiredBytes += moduleData.data.size * sizeof(u32);
    }

    auto msg = getOutputMessage();
    size_t bytesFree = msg ? nng::allocated_free_space(msg) : 0u;

    if (bytesFree < requiredBytes)
    {
        if (!flushOutputMessage())
            return false;

        msg = getOutputMessage();
    }

    if (!msg || nng::allocated_free_space(msg) < requiredBytes)
        return false;

    multi_crate::ParsedDataEventHeader eventHeader{};
    eventHeader.magicByte = multi_crate::ParsedDataEventMagic;
    eventHeader.crateIndex = crateIndex;
    eventHeader.eventIndex = eventIndex;
    eventHeader.moduleCount = moduleCount;

    nng_msg_append(msg, &eventHeader, sizeof(eventHeader));

    for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        auto &moduleData = moduleDataList[moduleIndex];

        multi_crate::ParsedModuleHeader moduleHeader = {};
        moduleHeader.prefixSize = moduleData.prefixSize;
        moduleHeader.dynamicSize = moduleData.dynamicSize;
        moduleHeader.suffixSize = moduleData.suffixSize;
        moduleHeader.hasDynamic = hasDynamic(crateIndex, eventIndex, moduleIndex);

        nng_msg_append(msg, &moduleHeader, sizeof(moduleHeader));
        nng_msg_append(msg, moduleData.data.data, moduleData.data.size * sizeof(u32));
    }

    return true;
}

// Serialize the given system event data into the current output message.
bool ParsedEventsMessageWriter::consumeSystemEventData(int crateIndex, const u32 *header, u32 size)
{
    size_t requiredBytes = sizeof(multi_crate::ParsedSystemEventHeader) + size * sizeof(u32);

    auto msg = getOutputMessage();
    size_t bytesFree = msg ? nng::allocated_free_space(msg) : 0u;

    if (bytesFree < requiredBytes)
    {
        if (!flushOutputMessage())
            return false;

        msg = getOutputMessage();
    }

    if (!msg || nng::allocated_free_space(msg) < requiredBytes)
        return false;

    multi_crate::ParsedSystemEventHeader eventHeader{};
    eventHeader.magicByte = multi_crate::ParsedSystemEventMagic;
    eventHeader.crateIndex = crateIndex;
    eventHeader.eventSize = size;

    nng_msg_append(msg, &eventHeader, sizeof(eventHeader));
    nng_msg_append(msg, header, size * sizeof(u32));

    return true;
}

// Input must be a ParsedEventsMessageHeader formatted message. Extracts the
// next event from message, trims message and returns the filled event
// container. This should be fine as trimming does not free or overwrite the
// message data.
// Returns EventContainer::Type::None once all data has been extracted from msg.
mvlc::EventContainer next_event(ParsedEventMessageIterator &iter)
{
    mvlc::EventContainer result{};
    auto &msg = iter.msg;

    if (nng_msg_len(msg) >= 1)
    {
        const u8 eventMagic = *reinterpret_cast<u8 *>(nng_msg_body(msg));

        if (eventMagic == multi_crate::ParsedDataEventMagic)
        {
            if (auto eventHeader = nng::msg_trim_read<multi_crate::ParsedDataEventHeader>(msg); eventHeader)
            {
                if (eventHeader->moduleCount >= iter.moduleDataBuffer.size())
                {
                    spdlog::error("too many modules {} in ParsedEventsMessage", eventHeader->moduleCount);
                    return {};
                }

                iter.moduleDataBuffer.fill({});

                for (size_t moduleIndex=0u; moduleIndex<eventHeader->moduleCount; ++moduleIndex)
                {
                    auto moduleHeader = nng::msg_trim_read<multi_crate::ParsedModuleHeader>(msg);

                    if (!moduleHeader)
                        return {};

                    if (moduleHeader->totalBytes() > nng_msg_len(msg))
                    {
                        spdlog::error("ParsedModuleHeader::totalBytes() exceeds remaining message size");
                        return {};
                    }

                    if (moduleHeader->totalBytes())
                    {
                        const u32 *moduleDataPtr = reinterpret_cast<const u32 *>(nng_msg_body(msg));

                        readout_parser::ModuleData moduleData{};
                        moduleData.data.data = moduleDataPtr;
                        moduleData.data.size = moduleHeader->totalSize();
                        moduleData.prefixSize = moduleHeader->prefixSize;
                        moduleData.dynamicSize = moduleHeader->dynamicSize;
                        moduleData.suffixSize = moduleHeader->suffixSize;
                        moduleData.hasDynamic = moduleHeader->hasDynamic;

                        assert(moduleIndex < iter.moduleDataBuffer.size());

                        iter.moduleDataBuffer[moduleIndex] = moduleData;

                        //mvlc::util::log_buffer(std::cout, moduleData.data.data, moduleHeader->totalSize(),
                        //    fmt::format("crate={}, event={}, module={}, size={}",
                        //    eventHeader->crateIndex, eventHeader->eventIndex, moduleIndex, moduleHeader->totalSize()));

                        nng_msg_trim(msg, moduleHeader->totalBytes());
                    }
                }

                result.type = EventContainer::Type::Readout;
                result.crateId = eventHeader->crateIndex;
                result.readout.eventIndex = eventHeader->eventIndex;
                result.readout.moduleDataList = iter.moduleDataBuffer.data();
                result.readout.moduleCount = eventHeader->moduleCount;
            }
        }
        else if (eventMagic == multi_crate::ParsedSystemEventMagic)
        {
            if (auto eventHeader = nng::msg_trim_read<multi_crate::ParsedSystemEventHeader>(msg);
                eventHeader && nng_msg_len(msg) >= eventHeader->totalBytes())
            {
                result.type = EventContainer::Type::System;
                result.crateId = eventHeader->crateIndex;
                result.system.header = reinterpret_cast<const u32 *>(nng_msg_body(msg));
                result.system.size = eventHeader->totalSize();
                nng_msg_trim(msg, eventHeader->totalBytes());
            }
            else
            {
                spdlog::warn("next_event: system event message too short (len={})", nng_msg_len(msg));
            }
        }
        else
        {
            spdlog::warn("next_event: unknown event magic byte 0x{:02x}", eventMagic);
        }
    }

    return result;
}

std::unique_ptr<ReadoutParserNngContext> make_readout_parser_nng_context(const mvlc::CrateConfig &crateConfig)
{
    auto res = std::make_unique<ReadoutParserNngContext>();

    res->crateId = crateConfig.crateId;
    res->inputFormat = crateConfig.connectionType;
    auto stacks = mvme_mvlc::sanitize_readout_stacks(crateConfig.stacks);
    res->parserState = mvlc::readout_parser::make_readout_parser(stacks, crateConfig.crateId, res.get());
    res->parserState.crateIndex = crateConfig.crateId;

    return res;
}

// Allocates and prepares a new ParsedEventsMessageHeader message if there isn't
// one in the context object.
inline bool parser_maybe_alloc_output(ReadoutParserNngContext &ctx)
{
    auto &msg = ctx.outputMessage;

    if (msg)
        return false;

    if (nng::allocate_reserve_message(&msg, DefaultOutputMessageReserve))
        return false;

    multi_crate::ParsedEventsMessageHeader header{};
    header.messageType = multi_crate::MessageType::ParsedEvents;
    header.messageNumber = ++ctx.outputMessageNumber;

    nng_msg_append(msg, &header, sizeof(header));
    assert(nng_msg_len(msg) == sizeof(header));

    return true;
}

inline bool flush_output_message(ReadoutParserNngContext &ctx)
{
    const auto msgSize = nng_msg_len(ctx.outputMessage);

    // Retries forever or until told to quit.
    //auto retryPredicate = [&]
    //{
    //    return !ctx.quit;
    //};

    StopWatch stopWatch;
    stopWatch.start();
    auto debugInfo = fmt::format("readout_parser (crate{})", ctx.crateId);

    if (auto res = nng::send_message_retry(ctx.outputSocket, ctx.outputMessage, 3, debugInfo.c_str()))
    {
        nng_msg_free(ctx.outputMessage);
        ctx.outputMessage = nullptr;
        //spdlog::error("{}: readout parser: flush_output_message(): {}:", debugInfo, nng_strerror(res));
        return false;
    }

    ctx.outputMessage = nullptr;

    {
        auto ta = ctx.counters.access();
        ta->tSend += stopWatch.interval();
        ta->tTotal += stopWatch.end();
        ta->messagesSent++;
        ta->bytesSent += msgSize;
        ctx.tLastFlush = std::chrono::steady_clock::now();
    }

    spdlog::debug("{}: sent message {} of size {}",
        debugInfo, ctx.outputMessageNumber, msgSize);

    return true;
}

struct ReadoutParserNngMessageWriter: public ParsedEventsMessageWriter
{
    ReadoutParserNngMessageWriter(ReadoutParserNngContext &ctx_)
        : ctx(ctx_)
    {
    }

    nng_msg *getOutputMessage() override
    {
        parser_maybe_alloc_output(ctx);
        return ctx.outputMessage;
    }

    bool flushOutputMessage() override
    {
        return flush_output_message(ctx);
    }

    bool hasDynamic(int crateIndex, int eventIndex, int moduleIndex) override
    {
        (void) crateIndex;
        if (0 <= eventIndex && static_cast<size_t>(eventIndex) < ctx.parserState.readoutStructure.size())
        {
            if (0 <= moduleIndex && static_cast<size_t>(moduleIndex) < ctx.parserState.readoutStructure[eventIndex].size())
                return ctx.parserState.readoutStructure[eventIndex][moduleIndex].hasDynamic;
        }

        return false;
    }

    ReadoutParserNngContext &ctx;
};

// The event data callback for the readout parser. Serializes the parsed module
// data list into the current output message.
inline void parser_nng_eventdata(void *ctx_, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());
    assert(eventIndex >= 0 && eventIndex <= std::numeric_limits<u8>::max());
    assert(moduleCount < std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<ReadoutParserNngContext *>(ctx_);
    ++ctx.totalReadoutEvents;

    ReadoutParserNngMessageWriter writer(ctx);
    writer.consumeReadoutEventData(crateIndex, eventIndex, moduleDataList, moduleCount);
}

inline void parser_nng_systemevent(void *ctx_, int crateIndex, const u32 *header, u32 size)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<ReadoutParserNngContext *>(ctx_);
    ++ctx.totalSystemEvents;

    ReadoutParserNngMessageWriter writer(ctx);
    writer.consumeSystemEventData(crateIndex, header, size);
}

void readout_parser_loop(ReadoutParserNngContext &context)
{
    set_thread_name("readout_parser_loop");

    auto crateId = context.crateId;

    spdlog::info("entering readout_parser_loop, crateId={}", crateId);

    size_t totalInputBytes = 0u;
    u32 lastInputMessageNumber = 0u;
    size_t inputBuffersLost = 0;

    // Local, non-protected readout parser counters. The protected version in
    // the context struct will be updated after parsing a full input buffer.
    mvlc::readout_parser::ReadoutParserCounters parserCounters = {};

    mvlc::readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        parser_nng_eventdata,
        parser_nng_systemevent,
    };

    //auto tLastReport = std::chrono::steady_clock::now();
    context.tLastFlush = std::chrono::steady_clock::now();
    context.counters.access()->start();

    while (!context.quit)
    {
        StopWatch stopWatch;
        stopWatch.start();

        nng_msg *inputMsg = nullptr;

        if (auto res = nng::receive_message(context.inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::error("readout_parser_loop (crateId={}) - receive_message: {}",
                    crateId, nng_strerror(res));
                break;
            }
            spdlog::trace("readout_parser_loop (crateId={}) - receive_message: timeout", crateId);
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg);

        if (msgLen >= sizeof(multi_crate::BaseMessageHeader))
        {
            auto header = *reinterpret_cast<multi_crate::BaseMessageHeader *>(nng_msg_body(inputMsg));
            if (header.messageType == multi_crate::MessageType::GracefulShutdown)
            {
                spdlog::warn("reaodut_parser_loop (crateId={}): Received shutdown message, leaving loop", crateId);
                nng_msg_free(inputMsg);
                break;
            }
        }

        if (msgLen < sizeof(multi_crate::ReadoutDataMessageHeader))
        {
            spdlog::warn("reaodut_parser_loop (crateId={}): Incoming message is too short (len={})!",
                crateId, msgLen);
            nng_msg_free(inputMsg);
            continue;
        }

        //spdlog::warn("incoming message size={}", nng_msg_len(inputMsg));
        {
            auto ta = context.counters.access();
            ta->messagesReceived++;
            ta->bytesReceived += msgLen;
            ta->tReceive += stopWatch.interval();
        }

        totalInputBytes += nng_msg_len(inputMsg);
        auto inputHeader = *reinterpret_cast<const multi_crate::ReadoutDataMessageHeader *>(
            nng_msg_body(inputMsg));
        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastInputMessageNumber);
        inputBuffersLost += bufferLoss >= 0 ? bufferLoss : 0u;
        lastInputMessageNumber = inputHeader.messageNumber;
        spdlog::debug("readout_parser_loop (crateId={}): received message {} of size {}",
            crateId, lastInputMessageNumber, nng_msg_len(inputMsg));

        nng_msg_trim(inputMsg, sizeof(multi_crate::ReadoutDataMessageHeader));
        auto inputData = reinterpret_cast<const u32 *>(nng_msg_body(inputMsg));
        size_t inputLen = nng_msg_len(inputMsg) / sizeof(u32);

        // Invoke the parser. The output message is flushed from within the callbacks when needed.
        readout_parser::parse_readout_buffer(
            context.inputFormat,
            context.parserState,
            parserCallbacks,
            parserCounters,
            inputHeader.messageNumber,
            inputData,
            inputLen);

        auto tCycle = stopWatch.interval();
        {
            auto ta = context.counters.access();
            ta->tProcess += tCycle;
            ta->tTotal += stopWatch.end();
            ta->messagesLost = inputBuffersLost;
        }
        context.parserCounters.access().ref() = parserCounters;

        nng_msg_free(inputMsg);

        #if 0
        {
            auto now = std::chrono::steady_clock::now();
            auto tReportElapsed = now - tLastReport;
            static const auto ReportInterval = std::chrono::seconds(1);

            if (tReportElapsed >= ReportInterval)
            {
                log_socket_work_counters(context.counters.access().ref(),
                    fmt::format("readout_parser_loop (crateId={})", context.crateId));
                tLastReport = now;
            }
        }
        #endif
    }

    if (context.outputMessage)
        flush_output_message(context);

    assert(!context.outputMessage);

    log_socket_work_counters(context.counters.access().ref(),
        fmt::format("readout_parser_loop (crateId={})", context.crateId));

    {
        std::ostringstream ss;
        readout_parser::print_counters(ss, parserCounters);
        spdlog::info("readout_parser_loop (crateId={}): parser counters:\n{}", crateId, ss.str());
    }

    spdlog::info("leaving readout_parser_loop, crateId={}", crateId);
}

void event_builder_record_loop(EventBuilderContext &context)
{
    set_thread_name("event_builder_recorder");

    spdlog::info("entering event_builder_record_loop");

    context.counters.access()->start();

    while (!context.quit)
    {
        StopWatch stopWatch;
        stopWatch.start();

        nng_msg *inputMsg = nullptr;

        if (auto res = nng::receive_message(context.inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::error("event_builder_record_loop - receive_message: {}", nng_strerror(res));
                break;
            }
            spdlog::trace("event_builder_record_loop - receive_message: timeout");
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg);

        if (msgLen >= sizeof(multi_crate::BaseMessageHeader))
        {
            auto header = *reinterpret_cast<multi_crate::BaseMessageHeader *>(nng_msg_body(inputMsg));
            if (header.messageType == multi_crate::MessageType::GracefulShutdown)
            {
                spdlog::warn("event_builder_record_loop: Received shutdown message, leaving loop");
                nng_msg_free(inputMsg);
                break;
            }
        }

        if (msgLen < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            spdlog::warn("event_builder_record_loop - incoming message too short (len={})", msgLen);
            nng_msg_free(inputMsg);
            continue;
        }

        auto inputHeader = nng::msg_trim_read<multi_crate::ParsedEventsMessageHeader>(inputMsg).value();

        if (inputHeader.messageType != multi_crate::MessageType::ParsedEvents)
        {
            spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ParsedEvents));
            nng_msg_free(inputMsg);
            break;
        }

        {
            auto ta = context.counters.access();
            ta->messagesReceived++;
            ta->bytesReceived += msgLen;
            ta->tReceive += stopWatch.interval();
        }

        ParsedEventMessageIterator messageIter(inputMsg);

        for (auto eventData = next_event(messageIter);
                eventData.type != EventContainer::Type::None;
                eventData = next_event(messageIter))
        {
            if (eventData.type == EventContainer::Type::Readout)
            {
                auto inputCrateId = context.inputCrateMappings[eventData.crateId];
                context.eventBuilder->recordEventData(inputCrateId, eventData.readout.eventIndex,
                    eventData.readout.moduleDataList, eventData.readout.moduleCount);
            }
            else if (eventData.type == EventContainer::Type::System && eventData.system.size)
            {
                auto inputCrateId = context.inputCrateMappings[eventData.crateId];
                context.eventBuilder->recordSystemEvent(inputCrateId, eventData.system.header, eventData.system.size);
            }
            else if (nng_msg_len(inputMsg))
            {
                spdlog::warn("analysis_loop: incoming message contains unknown subsection '{}'",
                    *reinterpret_cast<const u8 *>(nng_msg_body(inputMsg)));
                break;
            }
        }

        {
            auto ta = context.counters.access();
            ta->tProcess += stopWatch.interval();
            ta->tTotal += stopWatch.end();
        }

        nng_msg_free(inputMsg);
    }

    spdlog::info("leaving event_builder_record_loop");
}

struct EventBuilderNngMessageWriter: public ParsedEventsMessageWriter
{
    EventBuilderNngMessageWriter(EventBuilderContext &ctx_)
        : ctx(ctx_)
    {
    }

    nng_msg *getOutputMessage() override
    {
        auto &msg = ctx.outputMessage;

        if (msg)
            return msg;

        if (nng::allocate_reserve_message(&msg, DefaultOutputMessageReserve))
            return nullptr;

        multi_crate::ParsedEventsMessageHeader header{};
        header.messageType = multi_crate::MessageType::ParsedEvents;
        header.messageNumber = ++ctx.outputMessageNumber;

        nng_msg_append(msg, &header, sizeof(header));
        assert(nng_msg_len(msg) == sizeof(header));

        return msg;
    }

    bool flushOutputMessage() override
    {
        const auto msgSize = nng_msg_len(ctx.outputMessage);

        // Retries forever or until told to quit.
        //auto retryPredicate = [&]
        //{
        //    return !ctx.quit;
        //};

        auto debugInfo = fmt::format("EventBuilderNngMessageWriter (crateId={})", ctx.crateId);

        StopWatch stopWatch;
        stopWatch.start();

        if (auto res = nng::send_message_retry(ctx.outputSocket, ctx.outputMessage, 3, debugInfo.c_str()))
        {
            //spdlog::warn("EventBuilderNngMessageWriter: send_message_retry: {}", nng_strerror(res));
            nng_msg_free(ctx.outputMessage);
            ctx.outputMessage = nullptr;
            return false;
        }

        ctx.outputMessage = nullptr;

        {
            auto ta = ctx.counters.access();
            ta->tSend += stopWatch.interval();
            ta->tTotal += stopWatch.end();
            ta->messagesSent++;
            ta->bytesSent += msgSize;
            // FIXME ctx.tLastFlush = std::chrono::steady_clock::now();
        }

        spdlog::debug("{}: sent message {} of size {}",
            debugInfo, ctx.outputMessageNumber, msgSize);

        return true;
    }

    bool hasDynamic(int crateIndex, int eventIndex, int moduleIndex) override
    {
        (void) crateIndex; (void) eventIndex; (void) moduleIndex;
        return true;
    }

    EventBuilderContext &ctx;
};

inline void event_builder_eventdata(void *ctx_, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());
    assert(eventIndex >= 0 && eventIndex <= std::numeric_limits<u8>::max());
    assert(moduleCount < std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<EventBuilderContext *>(ctx_);

    auto outputCrateId = ctx.outputCrateMappings[crateIndex];

    EventBuilderNngMessageWriter writer(ctx);
    writer.consumeReadoutEventData(outputCrateId, eventIndex, moduleDataList, moduleCount);
}

inline void event_builder_systemevent(void *ctx_, int crateIndex, const u32 *header, u32 size)
{
    assert(ctx_);
    assert(crateIndex >= 0 && crateIndex <= std::numeric_limits<u8>::max());

    auto &ctx = *reinterpret_cast<EventBuilderContext *>(ctx_);

    auto outputCrateId = ctx.outputCrateMappings[crateIndex];

    EventBuilderNngMessageWriter writer(ctx);
    writer.consumeSystemEventData(outputCrateId, header, size);
}

void event_builder_build_loop(EventBuilderContext &context)
{
    set_thread_name("event_builder_builder");

    readout_parser::ReadoutParserCallbacks callbacks;
    callbacks.eventData = event_builder_eventdata;
    callbacks.systemEvent = event_builder_systemevent;


    while (!context.quit)
    {
        if (context.eventBuilder->waitForData(std::chrono::milliseconds(100)))
        {
            auto nEvents = context.eventBuilder->buildEvents(callbacks);
            if (nEvents)
                spdlog::trace("event_builder_builder{}: built {} events", context.crateId, nEvents);
        }
    }

    // flush
    context.eventBuilder->buildEvents(callbacks, true);
}

// records input data and immediately calls buildEvents() // TODO: try this out,
// might be faster than having two loops per event builder
void event_builder_combined_loop(EventBuilderContext &context)
{
    set_thread_name("event_builder_combined_loop");

    spdlog::info("entering event_builder_combined_loop");

    readout_parser::ReadoutParserCallbacks callbacks;
    callbacks.eventData = event_builder_eventdata;
    callbacks.systemEvent = event_builder_systemevent;

    context.counters.access()->start();

    while (!context.quit)
    {
        StopWatch stopWatch;
        stopWatch.start();

        nng_msg *inputMsg = nullptr;

        if (auto res = nng::receive_message(context.inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::error("event_builder_combined_loop - receive_message: {}", nng_strerror(res));
                break;
            }
            spdlog::trace("event_builder_combined_loop - receive_message: timeout");
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg);

        if (msgLen >= sizeof(multi_crate::BaseMessageHeader))
        {
            auto header = *reinterpret_cast<multi_crate::BaseMessageHeader *>(nng_msg_body(inputMsg));
            if (header.messageType == multi_crate::MessageType::GracefulShutdown)
            {
                spdlog::warn("event_builder_combined_loop: Received shutdown message, leaving loop");
                nng_msg_free(inputMsg);
                break;
            }
        }

        if (msgLen < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            spdlog::warn("event_builder_combined_loop - incoming message too short (len={})", msgLen);
            nng_msg_free(inputMsg);
            continue;
        }

        auto inputHeader = nng::msg_trim_read<multi_crate::ParsedEventsMessageHeader>(inputMsg).value();

        if (inputHeader.messageType != multi_crate::MessageType::ParsedEvents)
        {
            spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ParsedEvents));
            nng_msg_free(inputMsg);
            break;
        }

        {
            auto ta = context.counters.access();
            ta->messagesReceived++;
            ta->bytesReceived += msgLen;
            ta->tReceive += stopWatch.interval();
        }

        ParsedEventMessageIterator messageIter(inputMsg);

        for (auto eventData = next_event(messageIter);
                eventData.type != EventContainer::Type::None;
                eventData = next_event(messageIter))
        {
            if (eventData.type == EventContainer::Type::Readout)
            {
                auto inputCrateId = context.inputCrateMappings[eventData.crateId];
                context.eventBuilder->recordEventData(inputCrateId, eventData.readout.eventIndex,
                    eventData.readout.moduleDataList, eventData.readout.moduleCount);
            }
            else if (eventData.type == EventContainer::Type::System && eventData.system.size)
            {
                auto inputCrateId = context.inputCrateMappings[eventData.crateId];
                context.eventBuilder->recordSystemEvent(inputCrateId, eventData.system.header, eventData.system.size);
            }
            else if (nng_msg_len(inputMsg))
            {
                spdlog::warn("analysis_loop: incoming message contains unknown subsection '{}'",
                    *reinterpret_cast<const u8 *>(nng_msg_body(inputMsg)));
                break;
            }
        }

        auto nEvents = context.eventBuilder->buildEvents(callbacks);
        if (nEvents)
            spdlog::trace("event_builder_combined_loop{}: built {} events", context.crateId, nEvents);

        {
            auto ta = context.counters.access();
            ta->tProcess += stopWatch.interval();
            ta->tTotal += stopWatch.end();
        }

        nng_msg_free(inputMsg);
    }

    spdlog::info("leaving event_builder_combined_loop");
}

void analysis_loop(AnalysisProcessingContext &context)
{
    set_thread_name("analysis_loop");

    nng_msg *inputMsg = nullptr;
    size_t totalInputBytes = 0u;
    // FIXME: this thing receives data from multiple event builders so a single message loss calculation is not enough.
    u32 lastInputMessageNumber = 0u;
    size_t inputBuffersLost = 0;
    bool error = false;
    vme_analysis_common::TimetickGenerator timetickGen;
    context.inputSocketCounters.access()->start();

    spdlog::info("entering analysis_loop");

    while (!error && !context.quit)
    {
        if (!context.isReplay)
        {
            int elapsedSeconds = timetickGen.generateElapsedSeconds();

            while (elapsedSeconds >= 1)
            {
                context.analysis->processTimetick();
                elapsedSeconds--;
            }
        }

        if (auto res = nng::receive_message(context.inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::error("analysis_loop - receive_message: {}", nng_strerror(res));
                break;
            }
            spdlog::trace("analysis_loop - receive_message: timeout");
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg);

        if (msgLen >= sizeof(multi_crate::BaseMessageHeader))
        {
            auto header = *reinterpret_cast<multi_crate::BaseMessageHeader *>(nng_msg_body(inputMsg));
            if (header.messageType == multi_crate::MessageType::GracefulShutdown)
            {
                spdlog::warn("analysis_loop: Received shutdown message, leaving loop");
                nng_msg_free(inputMsg);
                break;
            }
        }

        if (msgLen < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            spdlog::warn("analysis_loop - incoming message too short (len={})", msgLen);
            nng_msg_free(inputMsg);
            continue;
        }

        totalInputBytes += nng_msg_len(inputMsg);
        auto inputHeader = nng::msg_trim_read<multi_crate::ParsedEventsMessageHeader>(inputMsg).value();
        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastInputMessageNumber);
        inputBuffersLost += bufferLoss >= 0 ? bufferLoss : 0u;;
        lastInputMessageNumber = inputHeader.messageNumber;
        if (inputHeader.messageType != multi_crate::MessageType::ParsedEvents)
        {
            spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ParsedEvents));
            nng_msg_free(inputMsg);
            break;
        }
        spdlog::debug("analysis_nng: received message {} of size {}", lastInputMessageNumber, nng_msg_len(inputMsg));

        if (context.asp)
        {
            auto a = context.asp->daqStats_.access();
            a->droppedBuffers += inputBuffersLost;
            a->totalBuffersRead += 1;
        }

        {
            auto a = context.inputSocketCounters.access();
            a->bytesReceived = totalInputBytes;
            a->messagesReceived += 1;
            a->messagesLost = inputBuffersLost;
        }

        ParsedEventMessageIterator messageIter(inputMsg);

        for (auto eventData = next_event(messageIter);
                eventData.type != EventContainer::Type::None;
                eventData = next_event(messageIter))
        {
            if (eventData.type == EventContainer::Type::Readout)
            {
                context.analysis->beginEvent(eventData.readout.eventIndex);

                context.analysis->processModuleData(eventData.crateId, eventData.readout.eventIndex,
                    eventData.readout.moduleDataList, eventData.readout.moduleCount);

                context.analysis->endEvent(eventData.readout.eventIndex);
            }
            else if (eventData.type == EventContainer::Type::System && eventData.system.size)
            {
                auto frameInfo = mvlc::extract_frame_info(eventData.system.header[0]);
                assert(frameInfo.type == mvlc::frame_headers::SystemEvent);

                if (frameInfo.sysEventSubType == mvlc::system_event::subtype::UnixTimetick)
                {
                    if (context.analysis && context.isReplay)
                        context.analysis->processTimetick();
                }
            }
            else if (nng_msg_len(inputMsg))
            {
                spdlog::warn("analysis_loop: incoming message contains unknown subsection '{}'",
                    *reinterpret_cast<const u8 *>(nng_msg_body(inputMsg)));
                break;
            }
        }

        assert(nng_msg_len(inputMsg) == 0);

        nng_msg_free(inputMsg);
        inputMsg = nullptr;
    }

    //spdlog::info("analysis_nng: lastInputMessageNumber={}, inputBuffersLost={}, totalInput={:.2f} MiB",
    //    lastInputMessageNumber, inputBuffersLost, 1.0 * totalInputBytes / mvlc::util::Megabytes(1));
}

void parsed_data_test_consumer_loop(ParsedDataConsumerContext &context)
{
    {
        auto name = fmt::format("parsed_data_test_consumer_loop({})", context.info);
        set_thread_name(name.c_str());
    }

    size_t totalInputBytes = 0u;
    size_t inputBuffersLost = 0;
    // FIXME: this thing receives data from multiple event builders so a single message loss calculation is not enough.
    u32 lastInputMessageNumber = 0u;
    context.counters.access()->start();
    context.readoutEventCounts.access()->fill(0);
    context.systemEventCounts.access()->fill(0);

    while (!context.quit)
    {
        nng_msg *inputMsg = nullptr;

        if (auto res = nng::receive_message(context.inputSocket, &inputMsg))
        {
            if (res != NNG_ETIMEDOUT)
            {
                spdlog::error("parsed_data_test_consumer_loop - receive_message: {}", nng_strerror(res));
                break;
            }
            spdlog::trace("parsed_data_test_consumer_loop - receive_message: timeout");
            continue;
        }

        assert(inputMsg != nullptr);

        const auto msgLen = nng_msg_len(inputMsg);

        if (msgLen >= sizeof(multi_crate::BaseMessageHeader))
        {
            auto header = *reinterpret_cast<multi_crate::BaseMessageHeader *>(nng_msg_body(inputMsg));
            if (header.messageType == multi_crate::MessageType::GracefulShutdown)
            {
                spdlog::warn("parsed_data_test_consumer_loop: Received shutdown message, leaving loop");
                nng_msg_free(inputMsg);
                break;
            }
        }

        if (msgLen < sizeof(multi_crate::ParsedEventsMessageHeader))
        {
            spdlog::warn("parsed_data_test_consumer_loop - incoming message too short (len={})", msgLen);
            nng_msg_free(inputMsg);
            continue;
        }

        totalInputBytes += nng_msg_len(inputMsg);
        auto inputHeader = nng::msg_trim_read<multi_crate::ParsedEventsMessageHeader>(inputMsg).value();
        auto bufferLoss = readout_parser::calc_buffer_loss(inputHeader.messageNumber, lastInputMessageNumber);
        inputBuffersLost += bufferLoss >= 0 ? bufferLoss : 0u;;
        lastInputMessageNumber = inputHeader.messageNumber;
        if (inputHeader.messageType != multi_crate::MessageType::ParsedEvents)
        {
            spdlog::error("Received input message with unhandled type 0x{:02x}, expected type 0x{:02x}",
                static_cast<u8>(inputHeader.messageType), static_cast<u8>(multi_crate::MessageType::ParsedEvents));
                break;
        }

        {
            auto a = context.counters.access();
            a->bytesReceived = totalInputBytes;
            a->messagesReceived += 1;
            a->messagesLost = inputBuffersLost;
        }

        ParsedEventMessageIterator messageIter(inputMsg);

        for (auto eventData = next_event(messageIter);
                eventData.type != EventContainer::Type::None;
                eventData = next_event(messageIter))
        {
            if (eventData.type == EventContainer::Type::Readout)
            {
                //spdlog::info("parsed_data_test_consumer_loop: Readout event: crate={}, event={}",
                //    eventData.crateId, eventData.readout.eventIndex);
                context.readoutEventCounts.access()->at(eventData.crateId)++;
            }
            else if (eventData.type == EventContainer::Type::System && eventData.system.size)
            {
                //spdlog::info("parsed_data_test_consumer_loop: System event: crate={}, size={}",
                //    eventData.crateId, eventData.system.size);
                context.systemEventCounts.access()->at(eventData.crateId)++;
            }
        }

        nng_msg_free(inputMsg);
    }
}

} // namespace mesytec::mvme::multi_crate
