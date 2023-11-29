#include "multi_crate.h"

#include <cassert>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <QDir>
#include <stdexcept>

#include "util/mesy_nng.h"
#include "util/qt_fs.h"
#include "vme_config_scripts.h"
#include "vme_config_util.h"

using namespace mesytec::mvlc;

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
std::unique_ptr<T> copy_config_object(const T *obj)
{
    assert(obj);

    QJsonObject json;
    obj->write(json);

    auto ret = std::make_unique<T>();
    ret->read(json);

    generate_new_ids(ret.get());

    return ret;
}

std::unique_ptr<ModuleConfig> copy_module_config(const ModuleConfig *src)
{
    auto moduleCopy = copy_config_object(src);

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

static size_t fixup_listfile_buffer_message(const mvlc::ConnectionType &bufferType, nng_msg *msg, std::vector<u8> &tmpBuf)
{
    size_t bytesMoved = 0u;
    const u8 *msgBufferData = reinterpret_cast<const u8 *>(nng_msg_body(msg)) + sizeof(ListfileBufferMessageHeader);
    const auto msgBufferSize = nng_msg_len(msg) - sizeof(ListfileBufferMessageHeader);

    if (bufferType == mvlc::ConnectionType::USB)
        bytesMoved = mvlc::fixup_buffer_mvlc_usb(msgBufferData, msgBufferSize, tmpBuf);
    else
        bytesMoved = mvlc::fixup_buffer_mvlc_eth(msgBufferData, msgBufferSize, tmpBuf);

    nng_msg_chop(msg, bytesMoved);

    return bytesMoved;
}

// A WriteHandle implementation writing to a nng_msg structure.
struct NngMsgWriteHandle: public listfile::WriteHandle
{
    explicit NngMsgWriteHandle(nng_msg *msg)
        : msg_(msg)
        { }

    size_t write(const u8 *data, size_t size) override
    {
        if (nng_msg_append(msg_, data, size) != 0)
            return 0;
        return size;
    }

    nng_msg *msg_;
};

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

// TODOs / things to think of
// flush timeout
// timetick sections
// crate id for timetick events
// counters

static const std::chrono::milliseconds FlushBufferTimeout(500);

std::error_code readout_usb(
    usb::MVLC_USB_Interface *mvlcUSB,
    nng_msg *dest,
    size_t &totalBytesTransferred
)
{
    return {};
}

std::error_code readout_eth(
    eth::MVLC_ETH_Interface *mvlcETH,
    nng_msg *msg,
    size_t &totalBytesTransferred)
{
#if 0
    auto originalMessageSize = nng_msg_len(msg);
    auto originalFreeSpace = allocated_free_space(msg);
    size_t messageUsed = originalMessageSize;

    nng_msg_realloc(msg, originalMessageSize + originalFreeSpace);

    while (allocated_free_space(msg) >= eth::JumboFrameMaxSize)
    {
        nng_msg_realloc(msg, nng_msg_len(msg) + eth::JumboFrameMaxSize);

        auto result = mvlcETH->read_packet(
            Pipe::Data,
            destBuffer->data() + destBuffer->used(),
            destBuffer->free());

        ec = result.ec;
        destBuffer->use(result.bytesTransferred);
        totalBytesTransferred += result.bytesTransferred;
    }

#endif
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

    ListfileBufferMessageHeader header{};
    header.messageType = MessageType::ListfileBuffer;
    header.messageNumber = 1;
    // TODO: don't actually need to know the buffer type. Can detect when parsing.
    header.bufferType = static_cast<u32>(mvlcEth ? ConnectionType::ETH : ConnectionType::USB);

    nng_msg *msg = nullptr;
    TimetickGenerator timetickGen;

    while (!quit)
    {
        // allocate new output message if needed
        if (!msg)
        {
            if (auto res = allocate_reserve_message(&msg, sizeof(header) +  mvlc::util::Megabytes(1)))
            {
                throw std::runtime_error(fmt::format("mvlc_readout_loop: error allocating output message: {}", nng_strerror(res)));
            }
        }
        assert(msg);

        // move trailing data from last readout cycle to the current output message
        nng_msg_append(msg, tmpBuf.data(), tmpBuf.size());
        tmpBuf.clear();

        NngMsgWriteHandle msgWriteHandle(msg);

        // run the timetick generator
        timetickGen(msgWriteHandle, context.crateId);

        // the actual readout
        {
            auto dataGuard = mvlc.getLocks().lockData();
            std::error_code ec;
            size_t bytesTransferred = 0u;
            if (mvlcEth)
                ec = readout_eth(mvlcEth, msg, bytesTransferred);
            else
                ec = readout_usb(mvlcUsb, msg, bytesTransferred);
        }

    }
}

}
