#ifndef __MVME_MULTI_CRATE_H__
#define __MVME_MULTI_CRATE_H__

#include <memory>
#include <set>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cassert>
#include <stdexcept>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "libmvme_export.h"
#include "vme_config.h"
#include "mvlc/mvlc_vme_controller.h"

namespace mesytec::mvme::multi_crate
{

// Holds bi-directional mappings between ConfigObjects in crate and merged vme
// configs.
struct LIBMVME_EXPORT MultiCrateObjectMappings
{
    QMap<QUuid, QUuid> cratesToMerged;
    QMap<QUuid, QUuid> mergedToCrates;

    void insertMapping(const ConfigObject *crateObject, const ConfigObject *mergedObject)
    {
        insertMapping(crateObject->getId(), mergedObject->getId());
    }

    void insertMapping(const QUuid &crateId, const QUuid &mergedId)
    {
        cratesToMerged.insert(crateId, mergedId);
        mergedToCrates.insert(mergedId, crateId);
    }

    bool operator==(const MultiCrateObjectMappings &o) const
    {
        return cratesToMerged == o.cratesToMerged
            && mergedToCrates == o.mergedToCrates;
    }

    bool operator!=(const MultiCrateObjectMappings &o) const
    {
        return !(*this == o);
    }
};

QJsonObject LIBMVME_EXPORT to_json(const MultiCrateObjectMappings &mappings);
MultiCrateObjectMappings LIBMVME_EXPORT object_mappings_from_json(const QJsonObject &json);


// A ConfigObject holding the individual crate configs, meta information and
// the merged vme config for a multicrate readout.
// Note: this class does not handle the logic of updating the merged vme config
// when meta info changes. This has to be handled externally by some controller
// object/code.
class LIBMVME_EXPORT MulticrateVMEConfig: public ConfigObject
{
    Q_OBJECT
    signals:
        void crateConfigAdded(VMEConfig *cfg);
        void crateConfigAboutToBeRemoved(VMEConfig *cfg);

    public:
        Q_INVOKABLE explicit MulticrateVMEConfig(QObject *parent = nullptr);
        ~MulticrateVMEConfig() override;

        void addCrateConfig(VMEConfig *cfg);
        void removeCrateConfig(VMEConfig *cfg);
        bool containsCrateConfig(const VMEConfig *cfg) const;
        VMEConfig *getCrateConfig(int crateIndex) const;
        const std::vector<VMEConfig *> &getCrateConfigs() const { return m_crateConfigs; }

        std::set<int> getCrossCrateEventIndexes() const { return m_crossCrateEventIndexes; }
        void setIsCrossCrateEvent(int eventIndex, bool isCrossCrate);
        bool isCrossCrateEvent(int eventIndex) const;

        void setCrossCrateEventMainModuleId(int eventIndex, const QUuid &moduleId);
        QUuid getCrossCrateEventMainModuleId(int eventIndex) const;

        VMEConfig *getMergedConfig() const { return m_mergedConfig; }
        void setMergedConfig(VMEConfig *merged) { m_mergedConfig = merged; }

        const MultiCrateObjectMappings &getMergedObjectMappings() { return m_objectMappings; }
        void setMergedObjectMappings(const MultiCrateObjectMappings &mappings) { m_objectMappings = mappings; }

        void setObjectSettings(const QUuid &objectId, const QVariantMap &settings);
        QVariantMap getObjectSettings(const QUuid &objectId) const;
        QMap<QUuid, QVariantMap> getObjectSettings() const { return m_objectSettings; }
        void clearObjectSettings(const QUuid &objectId);

    protected:
        std::error_code read_impl(const QJsonObject &json) override;
        std::error_code write_impl(QJsonObject &json) const override;

    private:
        // Crate VMEConfigs in crate index order. The first is the
        // primary/master crate.
        std::vector<VMEConfig *> m_crateConfigs;

        // Zero based indexes of cross-crate events.
        std::set<int> m_crossCrateEventIndexes;

        // Holds the id of the main/reference module for each cross-crate event
        // (required for the event builder).
        std::map<int, QUuid> m_crossCrateEventMainModules;

        // VME Config containing all cross-crate events and their modules.
        // Not updated by this class. Needs to be updated externally.
        VMEConfig *m_mergedConfig = nullptr;

        // Object mappings result from creating the merged VME config. Stored
        // for reuse when having to recreate the merged config. Needs to be
        // updated externally.
        MultiCrateObjectMappings m_objectMappings;

        // Per object settings. Holds e.g. multi_event_splitter and
        // event_builder settings for events and modules.
        QMap<QUuid, QVariantMap> m_objectSettings;
};

//
// VME config merging
//

// inputs:
// * list of crate vme configs with the first being the main crate
// * list of event indexes which are part of a cross-crate event
// * optional: id mappings returned from a previous merge operation.
//   If specified the objects in the merged config will have the ids contained
//   in the mappings.
//
// outputs:
// * a new merged vme config containing both merged cross-crate events and
//   non-merged single-crate events. The latter events are in linear (crate,
//   event) order.
// * bi-directional object id mappings (crates <-> merged)
std::pair<std::unique_ptr<VMEConfig>, MultiCrateObjectMappings>
LIBMVME_EXPORT make_merged_vme_config(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents,
    const MultiCrateObjectMappings &prevMappings = {}
    );

inline std::pair<std::unique_ptr<VMEConfig>, MultiCrateObjectMappings>
make_merged_vme_config(
    const std::vector<std::unique_ptr<VMEConfig>> &crateConfigs,
    const std::set<int> &crossCrateEvents,
    const MultiCrateObjectMappings &prevMappings = {}
    )
{
    std::vector<VMEConfig *> rawConfigs;
    for (auto &ptr: crateConfigs)
        rawConfigs.emplace_back(ptr.get());

    return make_merged_vme_config(rawConfigs, crossCrateEvents, prevMappings);
}

//
// Playground
//

// listfile::WriteHandle implementation which enqueues the data onto the given
// ReadoutBufferQueues. The queues are used in a blocking fashion which means
// write() will block if the consuming side is too slow.
//
// FIXME (maybe): using the WriteHandle interface leads to having to create
// another copy of the readout buffer (the first copy is done in
// mvlc::ReadoutWorker::Private::flushCurrentOutputBuffer()).
class BlockingBufferQueuesWriteHandle: public mvlc::listfile::WriteHandle
{
    public:
        BlockingBufferQueuesWriteHandle(
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

class DroppingBufferQueuesWriteHandle: public mvlc::listfile::WriteHandle
{
    public:
        DroppingBufferQueuesWriteHandle(
            mvlc::ReadoutBufferQueues &destQueues,
            mvlc::ConnectionType connectionType
            )
            : destQueues_(destQueues)
            , connectionType_(connectionType)
        {
        }

        size_t write(const u8 *data, size_t size) override
        {
            if (auto destBuffer = destQueues_.emptyBufferQueue().dequeue())
            {
                destBuffer->clear();
                destBuffer->setBufferNumber(nextBufferNumber_);
                destBuffer->setType(connectionType_);
                destBuffer->ensureFreeSpace(size);
                assert(destBuffer->used() == 0);
                std::memcpy(destBuffer->data(), data, size);
                destBuffer->use(size);
                destQueues_.filledBufferQueue().enqueue(destBuffer);
            }

            ++nextBufferNumber_;
            return size;
        }

    private:
        mvlc::ReadoutBufferQueues &destQueues_;
        mvlc::ConnectionType connectionType_;
        u32 nextBufferNumber_ = 1u;
};

struct LIBMVME_EXPORT EventBuilderOutputBufferWriter
{
    /* TODO:
     *
     * - find a better name for the struct
     * - if writing a listfile using the output of the EventBuilder
     *   vmeconfig and beginrun/endrun sections are needed.
     * - forced flush due to timeout and at the end of a run
     */


    using ModuleData = mvlc::readout_parser::ModuleData;

    EventBuilderOutputBufferWriter(mvlc::ReadoutBufferQueues &snoopQueues)
        : snoopQueues_(snoopQueues)
    {
    }

    ~EventBuilderOutputBufferWriter()
    {
        flushCurrentOutputBuffer();

        if (outputBuffer_)
        {
            snoopQueues_.emptyBufferQueue().enqueue(outputBuffer_);
            outputBuffer_ = nullptr;
        }
    }

    void eventData(int ci, int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        if (auto dest = getOutputBuffer())
        {
            mvlc::listfile::write_event_data(
                *dest, ci, ei, moduleDataList, moduleCount);
            maybeFlushOutputBuffer();
        }
        // else: data is dropped
    }

    void systemEvent(int ci, const u32 *header, u32 size)
    {
        if (auto dest = getOutputBuffer())
        {
            mvlc::listfile::write_system_event(
                *dest, ci, header, size);
            maybeFlushOutputBuffer();
        }
        // else: data is dropped
    }

    mvlc::ReadoutBuffer *getOutputBuffer()
    {
        if (!outputBuffer_)
        {
            outputBuffer_ = snoopQueues_.emptyBufferQueue().dequeue();

            if (outputBuffer_)
            {
                outputBuffer_->clear();
                outputBuffer_->setBufferNumber(nextOutputBufferNumber_++);
                outputBuffer_->setType(mvlc::ConnectionType::USB);
            }
        }

        return outputBuffer_;
    }

    void flushCurrentOutputBuffer()
    {
        if (outputBuffer_ && outputBuffer_->used() > 0)
        {
            snoopQueues_.filledBufferQueue().enqueue(outputBuffer_);
            outputBuffer_ = nullptr;
            lastFlushTime_ = std::chrono::steady_clock::now();
        }
    }

    void maybeFlushOutputBuffer()
    {
        if (!outputBuffer_ || outputBuffer_->empty())
            return;

        if (std::chrono::steady_clock::now() - lastFlushTime_ >= FlushBufferInterval)
            flushCurrentOutputBuffer();
    }

    mvlc::ReadoutBufferQueues &snoopQueues_;
    mvlc::ReadoutBuffer *outputBuffer_ = nullptr;
    u32 nextOutputBufferNumber_ = 1u;
    std::chrono::steady_clock::time_point lastFlushTime_ = std::chrono::steady_clock::now();;
    const std::chrono::milliseconds FlushBufferInterval = std::chrono::milliseconds(500);
};



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

    std::unique_ptr<mvme_mvlc::MVLC_VMEController> mvlcController;
    mvlc::MVLC mvlc;
    // unused but required for mvlc::ReadoutWorker instances.
    std::unique_ptr<mvlc::ReadoutBufferQueues> readoutSnoopQueues;
    std::unique_ptr<mvlc::ReadoutWorker> readoutWorker;

    // Buffer queues between the readout worker and the readout parser. Filled
    // by an instance of BlockingBufferQueuesWriteHandle, emptied by the
    // readout parser.
    std::unique_ptr<mvlc::ReadoutBufferQueues> readoutBufferQueues;
    std::shared_ptr<BlockingBufferQueuesWriteHandle> readoutWriteHandle;

    mvlc::readout_parser::ReadoutParserState parserState;
    std::unique_ptr<ProtectedParserCounters> parserCounters;
    mvlc::readout_parser::ReadoutParserCallbacks parserCallbacks;
    std::unique_ptr<std::atomic<bool>> parserQuit;
    std::thread parserThread;

    //CrateReadout() {}
    //CrateReadout(CrateReadout &&) = default;
    //CrateReadout(const CrateReadout &) = delete;
};

struct MultiCrateReadout_first
{

    std::vector<std::unique_ptr<CrateReadout>> crateReadouts;

    std::unique_ptr<mvlc::EventBuilder> eventBuilder;
    mvlc::readout_parser::ReadoutParserCallbacks eventBuilderCallbacks;
    std::unique_ptr<std::atomic<bool>> eventBuilderQuit;
    std::thread eventBuilderThread;

    std::unique_ptr<mvlc::ReadoutBufferQueues> eventBuilderSnoopOutputQueues;
    //std::unique_ptr<mvlc::ReadoutBufferQueues> postProcessedListfileQueues;
};

struct MulticrateTemplates
{
    std::unique_ptr<EventConfig> startEvent;
    std::unique_ptr<EventConfig> stopEvent;
    std::unique_ptr<EventConfig> dataEvent;
    QString setMasterModeScript;
    QString setSlaveModeScript;
    QString triggerIoScript;
};

MulticrateTemplates read_multicrate_templates();
std::unique_ptr<MulticrateVMEConfig> make_multicrate_config(size_t numCrates = 2);


}

Q_DECLARE_METATYPE(mesytec::mvme::multi_crate::MulticrateVMEConfig *);

#endif /* __MVME_MULTI_CRATE_H__ */
