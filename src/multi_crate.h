#ifndef __MVME_MULTI_CRATE_H__
#define __MVME_MULTI_CRATE_H__

#include <memory>
#include <set>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cassert>
#include <stdexcept>
#include <fmt/format.h>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "vme_config.h"

namespace multi_crate
{

//
// VME config merging
//

struct MultiCrateModuleMappings
{
    QMap<QUuid, QUuid> cratesToMerged;
    QMap<QUuid, QUuid> mergedToCrates;

    void insertMapping(const ModuleConfig *crateModule, const ModuleConfig *mergedModule)
    {
        insertMapping(crateModule->getId(), mergedModule->getId());
    }

    void insertMapping(const QUuid &crateModuleId, const QUuid &mergedModuleId)
    {
        cratesToMerged.insert(crateModuleId, mergedModuleId);
        mergedToCrates.insert(mergedModuleId, crateModuleId);
    }
};

// inputs:
// * list of crate vme configs with the first being the main crate
// * list of event indexes which are part of a cross-crate event
//
// outputs:
// * a new merged vme config containing both merged cross-crate events and
//   non-merged single-crate events. The latter events are in linear (crate,
//   event) order.
// * bi-directional module mappings
std::pair<std::unique_ptr<VMEConfig>, MultiCrateModuleMappings> make_merged_vme_config(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents
    );

inline std::pair<std::unique_ptr<VMEConfig>, MultiCrateModuleMappings> make_merged_vme_config(
    const std::vector<std::unique_ptr<VMEConfig>> &crateConfigs,
    const std::set<int> &crossCrateEvents
    )
{
    std::vector<VMEConfig *> rawConfigs;
    for (auto &ptr: crateConfigs)
        rawConfigs.emplace_back(ptr.get());

    return make_merged_vme_config(rawConfigs, crossCrateEvents);
}

//
// MultiCrateConfig
//

struct MultiCrateConfig
{
    // Filename of the VMEConfig for the main crate.
    QString mainConfig;

    // Filenames of VMEconfigs of the secondary crates.
    QStringList secondaryConfigs;

    // Event ids from mainConfig which form cross crate events.
    std::set<QUuid> crossCrateEventIds;

    // Ids of the "main" module for each cross crate event. The moduleId may
    // come from any of the individual VMEConfigs. The representation of this
    // module in the merged VMEConfig will be used as the EventBuilders main
    // module.
    std::set<QUuid> mainModuleIds;
};

inline bool operator==(const MultiCrateConfig &a, const MultiCrateConfig &b)
{
    return (a.mainConfig == b.mainConfig
            && a.secondaryConfigs == b.secondaryConfigs
            && a.crossCrateEventIds == b.crossCrateEventIds
           );
}

inline bool operator!=(const MultiCrateConfig &a, const MultiCrateConfig &b)
{
    return !(a == b);
}

MultiCrateConfig load_multi_crate_config(const QJsonDocument &doc);
MultiCrateConfig load_multi_crate_config(const QJsonObject &json);
MultiCrateConfig load_multi_crate_config(const QString &filename);

QJsonObject to_json_object(const MultiCrateConfig &mcfg);
QJsonDocument to_json_document(const MultiCrateConfig &mcfg);

//
// Playground (XXX: moved from the implementation file)
//

using namespace mesytec;

// Chain to build:
// mvlc::ReadoutWorker -> listfile::WriteHandle
//   -> [write  single crate listfile to disk]
//   -> Queue -> ReadoutParser [-> MultiEventSplitter ] -> EventBuilder::recordEventData()
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
    using ProtectedParserCounters = mvlc::Protected<mvlc::readout_parser::ReadoutParserCounters>;

    mvlc::MVLC mvlc;
    //std::unique_ptr<mvlc::ReadoutBufferQueues> readoutSnoopQueues; // unused but required for mvlc::ReadoutWorker
    std::unique_ptr<mvlc::ReadoutWorker> readoutWorker;

    // Buffer queues between the readout worker and the readout parser. Filled
    // by an instance of BufferQueuesWriteHandle, emptied by the readout
    // parser.
    std::unique_ptr<mvlc::ReadoutBufferQueues> listfileBufferQueues;

    mvlc::readout_parser::ReadoutParserState parserState;
    std::unique_ptr<ProtectedParserCounters> parserCounters;
    mvlc::readout_parser::ReadoutParserCallbacks parserCallbacks;
    std::unique_ptr<std::atomic<bool>> parserQuit;
    std::thread parserThread;

    //CrateReadout() {}
    //CrateReadout(CrateReadout &&) = default;
    //CrateReadout(const CrateReadout &) = delete;
};

struct MultiCrateReadout
{
    std::unique_ptr<mvlc::ReadoutBufferQueues> readoutSnoopQueues; // unused but required for mvlc::ReadoutWorker

    std::vector<CrateReadout> crateReadouts;

    mvlc::EventBuilder eventBuilder;
    mvlc::readout_parser::ReadoutParserCallbacks eventBuilderCallbacks;
    std::thread eventBuilderThread;

    std::unique_ptr<mvlc::ReadoutBufferQueues> postProcessedSnoopQueues;
    std::unique_ptr<mvlc::ReadoutBufferQueues> postProcessedListfileQueues;
};

// listfile::WriteHandle implementation which enqueues the data onto the given
// ReadoutBufferQueues. The queues are used in a blocking fashion which means
// write() will block if the consuming side is too slow.
//
// FIXME (maybe): using the WriteHandle interface leads to having to create
// another copy of the readout buffer (the first copy is done in
// mvlc::ReadoutWorker::Private::flushCurrentOutputBuffer()).
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

} // end namespace multi_crate

#endif /* __MVME_MULTI_CRATE_H__ */
