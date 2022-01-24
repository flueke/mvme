#include "multi_crate.h"

#include <cassert>
#include <stdexcept>
#include <fmt/format.h>
#include <mesytec-mvlc/mesytec-mvlc.h>

namespace multi_crate
{

template<typename T>
std::unique_ptr<T> copy_config_object(const T *obj)
{
    assert(obj);

    QJsonObject json;
    obj->write(json);

    auto ret = std::make_unique<T>();
    ret->read(json);
    ret->generateNewId();

    return ret;
}

std::pair<std::unique_ptr<VMEConfig>, MultiCrateModuleMappings> make_merged_vme_config(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents
    )
{
    MultiCrateModuleMappings mappings;
    size_t mergedEventCount = crossCrateEvents.size();

    std::vector<std::unique_ptr<EventConfig>> mergedEvents;

    for (auto outEi=0u; outEi<mergedEventCount; ++outEi)
    {
        auto outEv = std::make_unique<EventConfig>();
        outEv->setObjectName(QSL("event%1").arg(outEi));

        for (auto crateConf: crateConfigs)
        {
            auto crateEvents = crateConf->getEventConfigs();

            for (int ei=0; ei<crateEvents.size(); ++ei)
            {
                if (crossCrateEvents.count(ei))
                {
                    auto moduleConfigs = crateEvents[ei]->getModuleConfigs();

                    for (auto moduleConf: moduleConfigs)
                    {
                        auto moduleCopy = copy_config_object(moduleConf);
                        mappings.insertMapping(moduleConf, moduleCopy.get());
                        outEv->addModuleConfig(moduleCopy.release());
                    }
                }
            }
        }

        mergedEvents.emplace_back(std::move(outEv));
    }

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
                auto outEv = std::make_unique<EventConfig>();
                outEv->setObjectName(QSL("crate%1_%2")
                                     .arg(ci)
                                     .arg(eventConf->objectName())
                                     );

                auto moduleConfigs = crateEvents[ei]->getModuleConfigs();

                for (auto moduleConf: moduleConfigs)
                {
                    auto moduleCopy = copy_config_object(moduleConf);
                    mappings.insertMapping(moduleConf, moduleCopy.get());
                    outEv->addModuleConfig(moduleCopy.release());
                }

                singleCrateEvents.emplace_back(std::move(outEv));
            }
        }
    }

    auto merged = std::make_unique<VMEConfig>();

    for (auto &eventConf: mergedEvents)
        merged->addEventConfig(eventConf.release());

    for (auto &eventConf: singleCrateEvents)
        merged->addEventConfig(eventConf.release());

    return std::make_pair(std::move(merged), mappings);
}

QJsonObject to_json_object(const MultiCrateConfig &mcfg)
{
    QJsonObject j;
    j["mainConfig"] = mcfg.mainConfig;
    j["secondaryConfigs"] = QJsonArray::fromStringList(mcfg.secondaryConfigs);

    QJsonArray ids;

    std::transform(
        std::begin(mcfg.crossCrateEventIds), std::end(mcfg.crossCrateEventIds),
        std::back_inserter(ids), [] (const QUuid &id)
        {
            return id.toString();
        });

    j["crossCrateEventIds"] = ids;

    return j;
}

QJsonDocument to_json_document(const MultiCrateConfig &mcfg)
{
    QJsonObject outer;
    outer["MvmeMultiCrateConfig"] = to_json_object(mcfg);
    return QJsonDocument(outer);
}

MultiCrateConfig load_multi_crate_config(const QJsonObject &json)
{
    MultiCrateConfig mcfg = {};
    mcfg.mainConfig = json["mainConfig"].toString();

    for (const auto &jval: json["secondaryConfigs"].toArray())
        mcfg.secondaryConfigs.push_back(jval.toString());

    for (const auto &jval: json["crossCrateEventIds"].toArray())
        mcfg.crossCrateEventIds.insert(QUuid::fromString(jval.toString()));

    return mcfg;
}

MultiCrateConfig load_multi_crate_config(const QJsonDocument &doc)
{
    return load_multi_crate_config(doc.object()["MvmeMultiCrateConfig"].toObject());
}

MultiCrateConfig load_multi_crate_config(const QString &filename)
{
    QFile inFile(filename);

    if (!inFile.open(QIODevice::ReadOnly))
        throw std::runtime_error(inFile.errorString().toStdString());

    auto data = inFile.readAll();

    QJsonParseError parseError;
    QJsonDocument doc(QJsonDocument::fromJson(data, &parseError));

    if (parseError.error != QJsonParseError::NoError)
    {
        throw std::runtime_error(
            fmt::format("JSON parse error in file {}: {} at offset {}",
                        filename.toStdString(),
                        parseError.errorString().toStdString(),
                        std::to_string(parseError.offset)));
    }

    if (doc.isNull())
        return {};

    return load_multi_crate_config(doc);
}

//
// Playground
//

using namespace mesytec;

// Chain to build:
// mvlc::ReadoutWorker -> listfile::WriteHandle
//   -> [write  single crate listfile to disk]
//   -> Queue -> ReadoutParser -> callback -> EventBuilder::recordEventData()
//   -> EventBuilder -> MergedQueue
//   -> MergedQueue -> Analysis
//                  -> MergedListfile
//
// Can leave out the merged queue in the fist iteration and directly call into
// the analysis but this makes EventBuilder and analysis run in the same
// thread.

// Threads, ReadoutWorker, ReadoutParser, EventBuilder, Analysis



struct CrateReadout
{
    mvlc::MVLC mvlc;
    mvlc::ReadoutBufferQueues readoutSnoopQueues; // unused but required for mvlc::ReadoutWorker
    std::unique_ptr<mvlc::ReadoutWorker> readoutWorker;

    // Buffer queues between the readout worker and the readout parser. Filled
    // by an instance of BufferQueuesWriteHandle, emptied by the readout
    // parser.
    mvlc::ReadoutBufferQueues listfileBufferQueues;

    mvlc::readout_parser::ReadoutParserState parserState;
    mvlc::Protected<mvlc::readout_parser::ReadoutParserCounters> parserCounters;
    mvlc::readout_parser::ReadoutParserCallbacks parserCallbacks;
    std::atomic<bool> parserQuit;
    std::thread parserThread;
};

struct MultiCrateReadout
{
    std::vector<CrateReadout> crateReadouts;

    mvlc::EventBuilder eventBuilder;
    mvlc::readout_parser::ReadoutParserCallbacks eventBuilderCallbacks;
    std::thread eventBuilderThread; // XXX: leftoff here
};

// listfile::WriteHandle implementation which enqueues the data onto the given
// ReadoutBufferQueues. The queues are used in a blocking fashion which means
// write() will block if the consuming side is too slow.
class BufferQueuesWriteHandle: public mvlc::listfile::WriteHandle
{
    public:
        BufferQueuesWriteHandle(
            mvlc::ReadoutBufferQueues &destQueues,
            mvlc::ConnectionType connectionType
            )
            : destQueues_(destQueues)
            , connectionType_(connectionType)
        {
        }

        size_t write(const u8 *data, size_t size) override
        {
            auto destBuffer = destQueues_.emptyBufferQueue().dequeue_blocking();
            destBuffer->clear();
            destBuffer->setBufferNumber(nextBufferNumber_++);
            destBuffer->setType(connectionType_);
            destBuffer->ensureFreeSpace(size);
            assert(destBuffer->used() == 0);
            std::memcpy(destBuffer->data(), data, size);
            destBuffer->use(size);
            destQueues_.filledBufferQueue().enqueue(destBuffer);
            return size;
        }

    private:
        mvlc::ReadoutBufferQueues &destQueues_;
        mvlc::ConnectionType connectionType_;
        u32 nextBufferNumber_ = 1u;
};

// run_readout_parser()
// run_event_builder()

void multi_crate_playground()
{
    CrateReadout crateReadout;
}

}
