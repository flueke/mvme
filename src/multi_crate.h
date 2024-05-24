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
#include <mesytec-mvlc/util/protected.h>

#include "analysis_service_provider.h"
#include "libmvme_export.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvme_mvlc_listfile.h"
#include "util/mesy_nng.h"
#include "util/mesy_nng_pipeline2.h"
#include "vme_config.h"

// TODO:
// - flush timeouts for everything! Some loops are still missing this.

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

// Simpler version of make_merged_vme_config() which keeps existing object ids
// of ModuleConfigs. EventConfigs are still generated with new ids. These
// currently do not matter for analysis processing.
std::unique_ptr<VMEConfig> LIBMVME_EXPORT make_merged_vme_config_keep_ids(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents);

inline std::unique_ptr<VMEConfig> LIBMVME_EXPORT make_merged_vme_config_keep_ids(
    const std::vector<std::unique_ptr<VMEConfig>> &crateConfigs,
    const std::set<int> &crossCrateEvents)
{
    std::vector<VMEConfig *> rawConfigs;
    for (auto &ptr: crateConfigs)
        rawConfigs.emplace_back(ptr.get());

    return make_merged_vme_config_keep_ids(rawConfigs, crossCrateEvents);
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
    std::unique_ptr<EventConfig> mainStartEvent;
    std::unique_ptr<EventConfig> secondaryStartEvent;
    std::unique_ptr<EventConfig> stopEvent;
    std::unique_ptr<EventConfig> dataEvent;
    QString setMasterModeScript;
    QString setSlaveModeScript;
    QString triggerIoScript;
};

MulticrateTemplates LIBMVME_EXPORT read_multicrate_templates();
std::unique_ptr<MulticrateVMEConfig> LIBMVME_EXPORT make_multicrate_config(size_t numCrates = 2);

// A WriteHandle implementation writing to a nng_msg structure.
struct LIBMVME_EXPORT NngMsgWriteHandle: public mvlc::listfile::WriteHandle
{
    NngMsgWriteHandle()
        : msg_(nullptr)
        { }

    explicit NngMsgWriteHandle(nng_msg *msg)
        : msg_(msg)
        { }

    void setMessage(nng_msg *msg)
    {
        msg_= msg;
    }

    size_t write(const u8 *data, size_t size) override
    {
        assert(msg_);
        if (auto res = nng_msg_append(msg_, data, size))
            throw std::runtime_error(fmt::format("NngMsgWriteHandle: {}", nng_strerror(res)));
        return size;
    }

    nng_msg *msg_;
};

// Readout context for a single crate. Output buffers are written to the output socket.
struct LIBMVME_EXPORT ReadoutProducerContext
{
    unsigned crateId = 0;
    mvlc::MVLC mvlc;
    nng_socket outputSocket = NNG_SOCKET_INITIALIZER;
    nng_msg *outputMessage = nullptr;
    NngMsgWriteHandle msgWriteHandle;

    // TODO: add readout counters here (mvlc_readout_worker)
};

struct LIBMVME_EXPORT ReplayProducerContext
{
    nng_socket outputSocket;
};

struct LIBMVME_EXPORT ReadoutConsumerContext
{
    nng_socket inputSocket;
    nng_socket snoopOutputSocket;
    mvlc::listfile::WriteHandle *listfileWriteHandle;
    // TODO: add consumer counters here
};

void LIBMVME_EXPORT mvlc_readout_loop(ReadoutProducerContext &context, std::atomic<bool> &quit); // throws on error
void LIBMVME_EXPORT mvlc_readout_consumer(ReadoutConsumerContext &context, std::atomic<bool> &quit);

enum class MessageType: u8
{
    GracefulShutdown,   // -> BaseMessageHeader
    ReadoutData,        // -> ReadoutDataMessageHeader
    ParsedEvents,       // -> ParsedEventsMessageHeader
};

#define PACK_AND_ALIGN4 __attribute__((packed, aligned(4)))

struct LIBMVME_EXPORT PACK_AND_ALIGN4 BaseMessageHeader
{
    MessageType messageType;
    u32 messageNumber; // initially starts from 1 to simplify packet loss calculations. wraps to 0.
};

// These messages contain raw controller data possibly mixed with system event
// frames.
struct LIBMVME_EXPORT PACK_AND_ALIGN4 ReadoutDataMessageHeader: public BaseMessageHeader
{
    ReadoutDataMessageHeader()
    {
        messageType = MessageType::ReadoutData;
    }

    u32 bufferType = 0; // mvlc eth or mvlc usb
    u8 crateId = 0;
};

static_assert(sizeof(ReadoutDataMessageHeader) % sizeof(u32) == 0);

// Message header for parsed data and system events. Can carry data from
// different crates.
struct LIBMVME_EXPORT PACK_AND_ALIGN4 ParsedEventsMessageHeader: public BaseMessageHeader
{
    ParsedEventsMessageHeader()
    {
        messageType = MessageType::ParsedEvents;
    }
};

static_assert(sizeof(ParsedEventsMessageHeader) % sizeof(u32) == 0);

// Magic byte to identify a parsed readout data section.
static const u8 ParsedDataEventMagic = 0xF3u;

// Magic byte to identify a parsed system event section.
static const u8 ParsedSystemEventMagic = 0xFAu;

struct LIBMVME_EXPORT PACK_AND_ALIGN4 ParsedEventHeader
{
    u8 magicByte;
    u8 crateIndex;
};

struct LIBMVME_EXPORT PACK_AND_ALIGN4 ParsedDataEventHeader: public ParsedEventHeader
{
    u8 eventIndex;
    u8 moduleCount;
};

struct LIBMVME_EXPORT PACK_AND_ALIGN4 ParsedModuleHeader
{
    u16 prefixSize;
    u16 suffixSize;
    u32 dynamicSize;
    bool hasDynamic;

    size_t totalSize() const { return prefixSize + suffixSize + dynamicSize; }
    size_t totalBytes() const { return totalSize() * sizeof(u32); }
};

struct LIBMVME_EXPORT PACK_AND_ALIGN4 ParsedSystemEventHeader: public ParsedEventHeader
{
    u32 eventSize;

    size_t totalSize() const { return eventSize; }
    size_t totalBytes() const { return totalSize() * sizeof(u32); }
};

#undef PACK_AND_ALIGN4

template<typename Header>
nng::unique_msg allocate_prepare_message(const Header &header, size_t allocSize = mvlc::util::Megabytes(1))
{
    auto msg = nng::allocate_reserve_message(sizeof(Header) + allocSize);

    if (!msg)
        return msg;

    if (auto res = nng_msg_append(msg.get(), &header, sizeof(header)))
    {
        nng::mesy_nng_error("nng_msg_append", res);
        return nng::unique_msg(nullptr, nng_msg_free);
    }

    return msg;
}

int LIBMVME_EXPORT send_shutdown_message(nng_socket socket);
void LIBMVME_EXPORT send_shutdown_messages(std::initializer_list<nng_socket> sockets);
bool LIBMVME_EXPORT is_shutdown_message(nng_msg *msg);
int LIBMVME_EXPORT send_shutdown_message(nng::OutputWriter &outputWriter);

// Move trailing bytes from msg to tmpBuf. Returns the number of bytes moved.
size_t LIBMVME_EXPORT fixup_listfile_buffer_message(
    const mvlc::ConnectionType &bufferType, nng_msg *msg, std::vector<u8> &tmpBuf);

class LIBMVME_EXPORT MinimalAnalysisServiceProvider: public AnalysisServiceProvider
{
    Q_OBJECT
    public:
        QString getWorkspaceDirectory() override;

        QString getWorkspacePath(
            const QString &settingsKey,
            const QString &defaultValue = QString(),
            bool setIfDefaulted = true) const override;

        std::shared_ptr<QSettings> makeWorkspaceSettings() const override;

        // VMEConfig
        VMEConfig *getVMEConfig() override;
        QString getVMEConfigFilename() override;
        void setVMEConfigFilename(const QString &filename) override;
        void vmeConfigWasSaved() override;

        // Analysis
        analysis::Analysis *getAnalysis() override;
        QString getAnalysisConfigFilename() override;
        void setAnalysisConfigFilename(const QString &filename) override;
        void analysisWasSaved() override;
        void analysisWasCleared() override;
        void stopAnalysis() override;
        void resumeAnalysis(analysis::Analysis::BeginRunOption runOption) override;
        bool loadAnalysisConfig(const QString &filename) override;
        bool loadAnalysisConfig(const QJsonDocument &doc, const QString &inputInfo, AnalysisLoadFlags flags) override;

        // Widget registry
        mesytec::mvme::WidgetRegistry *getWidgetRegistry() override;

        // Worker states
        AnalysisWorkerState getAnalysisWorkerState() override;
        StreamWorkerBase *getMVMEStreamWorker() override;

        void logMessage(const QString &msg) override;


        GlobalMode getGlobalMode() override; // DAQ or Listfile
        const ListfileReplayHandle &getReplayFileHandle() const override;

        DAQStats getDAQStats() const override;
        RunInfo getRunInfo() const override;

        DAQState getDAQState() const override;

    public slots:
        void addAnalysisOperator(QUuid eventId, const std::shared_ptr<analysis::OperatorInterface> &op,
                                 s32 userLevel) override;
        void setAnalysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op) override;

    public:
        VMEConfig *vmeConfig_;
        QString vmeConfigFilename_;
        std::shared_ptr<analysis::Analysis> analysis_;
        QString analysisConfigFilename_;
        std::shared_ptr<WidgetRegistry> widgetRegistry_;
        // artifacts from mvme. Don't think it's needed in the future.
        ListfileReplayHandle listfileReplayHandle_;
        mutable mvlc::Protected<DAQStats> daqStats_;
        RunInfo runInfo_;
};

struct LoopResult
{
    std::error_code ec;
    std::exception_ptr exception;

    bool hasError() const { return ec || exception; }
};

// Runtime state for a loop context. Combines the loops quit flag, the future
// from the async call spawning the loop and optionally a raw pointer to the
// loops outputWriter.
//
// Allows to run a graceful shutdown sequence for processing pipelines.
// - Set quit flag on the first element (the producer) in a pipeline.
// - Wait for the future of the first element to finish.
// - Send shutdown messages through the outputWriter of the first element.
//
// * Repeat for all remaining elements in the pipeline:
//   - Wait for the future of the element to finish (it should receive the
//     shutdown message after having processed all input data).
//   - If a timeout is exceeded set the quit flag to force the loop to exit. Repeat waiting for the future.
//     Give up after a defined time or number of attempts.
//   - Send shutdown messages through the outputWriter of the element.
struct LoopRuntime
{
    std::atomic<bool> &quit;
    std::future<LoopResult> resultFuture;
    std::vector<nng::OutputWriter *> outputWriters;
};

using PipelineRuntime = std::vector<LoopRuntime>;

std::vector<LoopResult> LIBMVME_EXPORT shutdown_pipeline(PipelineRuntime &pipeline,
    std::chrono::milliseconds elementShutdownTimeout = std::chrono::milliseconds(5000));

struct LIBMVME_EXPORT SocketWorkPerformanceCounters
{
    std::chrono::steady_clock::time_point tpStart = {};
    std::chrono::microseconds tReceive = {};
    std::chrono::microseconds tProcess = {};
    std::chrono::microseconds tSend = {};
    std::chrono::microseconds tTotal = {};
    size_t messagesReceived = 0;
    size_t messagesLost = 0;
    size_t messagesSent = 0;
    size_t bytesReceived = 0;
    size_t bytesSent = 0;

    void start()
    {
        tpStart = std::chrono::steady_clock::now();
    }
};

void LIBMVME_EXPORT log_socket_work_counters(const SocketWorkPerformanceCounters &counters, const std::string &info);

// Ethernet-only MVLC data stream readout.
struct LIBMVME_EXPORT MvlcEthReadoutLoopContext
{
    std::atomic<bool> quit;

    // This is put into output ReadoutDataMessageHeader messages and passed
    // to ReadoutLoopPlugins.
    u8 crateId;

    // The MVLC data stream is read from this UDP socket.
    int mvlcDataSocket;

    // Readout data is written to this socket. Expected to use a lossless,
    // blocking protocol, e.g. pair or push, as this data goes to the output
    // listfile.
    nng_socket dataOutputSocket;

    // Readout data is also written to this socket. Should be a lossfull,
    // non-blocking protocol, e.g. pub.
    nng_socket snoopOutputSocket;

    mvlc::Protected<SocketWorkPerformanceCounters> dataOutputCounters;
    mvlc::Protected<SocketWorkPerformanceCounters> snoopOutputCounters;
};

void LIBMVME_EXPORT mvlc_eth_readout_loop(MvlcEthReadoutLoopContext &context);

// Data stream readout working on a mvlc::MVLC instance. Works for both ETH and
// USB connections.
struct LIBMVME_EXPORT MvlcInstanceReadoutLoopContext
{
    std::atomic<bool> quit;

    // This is put into output ReadoutDataMessageHeader messages and passed
    // to ReadoutLoopPlugins.
    u8 crateId;

    mvlc::MVLC mvlc;

    nng_socket dataOutputSocket;
    nng_socket snoopOutputSocket;

    mvlc::Protected<SocketWorkPerformanceCounters> dataOutputCounters;
    mvlc::Protected<SocketWorkPerformanceCounters> snoopOutputCounters;
};

// TODO: implement this (similar to ReadoutWorker::Private::readout())
void LIBMVME_EXPORT mvlc_instance_readout_loop(MvlcInstanceReadoutLoopContext &context);

struct LIBMVME_EXPORT ListfileWriterContext
{
    std::atomic<bool> quit;

    // Readout data in the form of ReadoutDataMessageHeader messages is read
    // from this socket.
    nng_socket dataInputSocket;

    // This is where readout data is written to if non-null. If the handle is
    // null readout data is read from the input socket and discarded.
    std::unique_ptr<mvlc::listfile::WriteHandle> lfh;

    // Per crate data input counters.
    mvlc::Protected<std::array<SocketWorkPerformanceCounters, mvlc::MaxVMECrates>> dataInputCounters;

    ListfileWriterContext() {}
};

void LIBMVME_EXPORT listfile_writer_loop(ListfileWriterContext &context);

// Helper class for filling out ParsedEventsMessages.
struct LIBMVME_EXPORT ParsedEventsMessageWriter
{
    // Must return a prepared MessageType::ParsedEvents message with reserved
    // free space.
    virtual nng_msg *getOutputMessage() = 0;

    // Called when the output message is full and needs to be sent. Afterwards
    // getOutputMessage() is used to get a fresh output message.
    virtual bool flushOutputMessage() = 0;

    // Must return true if the module specified by the given indices has dynamic
    // data. Note: this is a configuration setting. Concrete events can have
    // empty dynamic data when e.g. a VME block read immediately terminates.
    virtual bool hasDynamic(int crateIndex, int eventIndex, int moduleIndex) = 0;

    virtual ~ParsedEventsMessageWriter() = default;

    // Serialize the given module data list into the current output message.
    bool consumeReadoutEventData(int crateIndex, int eventIndex,
        const mvlc::readout_parser::ModuleData *moduleDataList, unsigned moduleCount);

    // Serialize the given system event data into the current output message.
    bool consumeSystemEventData(int crateIndex, const u32 *header, u32 size);
};

struct LIBMVME_EXPORT ParsedEventMessageIterator
{
    nng_msg *msg = nullptr;
    std::array<mvlc::readout_parser::ModuleData, MaxVMEModules * 2> moduleDataBuffer;

    explicit ParsedEventMessageIterator(nng_msg *msg_)
        : msg(msg_)
    {
        moduleDataBuffer.fill({});
    }
};

mvlc::EventContainer LIBMVME_EXPORT next_event(ParsedEventMessageIterator &iter);

struct LIBMVME_EXPORT ReadoutParserNngContext
{
    std::atomic<bool> quit = false;

    // Input: MessageType::ReadoutData
    std::unique_ptr<nng::InputReader> inputReader;
    // Output: MessageType::ParsedEvents
    std::unique_ptr<nng::OutputWriter> outputWriter;

    unsigned crateId = 0;
    mvlc::ConnectionType inputFormat;

    size_t totalReadoutEvents = 0u;
    size_t totalSystemEvents = 0u;
    mvlc::Protected<SocketWorkPerformanceCounters> counters;
    nng::unique_msg outputMessage = nng::make_unique_msg();
    u32 outputMessageNumber = 0u;
    mvlc::readout_parser::ReadoutParserState parserState;
    mvlc::Protected<mvlc::readout_parser::ReadoutParserCounters> parserCounters;
    std::chrono::steady_clock::time_point tLastFlush;
};

std::unique_ptr<ReadoutParserNngContext> LIBMVME_EXPORT make_readout_parser_nng_context(const mvlc::CrateConfig &crateConfig);
void LIBMVME_EXPORT readout_parser_loop(ReadoutParserNngContext &context);

// EventBuilder
// Note: this can be split up: N threads can call
// recordEventData()/recordSystemEvent() while one thread repeatedly calls
// buildEvents().
struct LIBMVME_EXPORT EventBuilderContext
{
    std::atomic<bool> quit;

    // Input: ParsedEventsMessageHeader
    //std::unique_ptr<nng::InputReader> inputReader;
    nng_socket inputSocket = NNG_SOCKET_INITIALIZER;

    // Output: ParsedEventsMessageHeader
    //std::unique_ptr<nng::OutputWriter> outputWriter;
    nng_socket outputSocket = NNG_SOCKET_INITIALIZER;

    mvlc::EventBuilderConfig eventBuilderConfig;
    std::unique_ptr<mvlc::EventBuilder> eventBuilder;

    nng_msg *outputMessage = nullptr;
    u32 outputMessageNumber = 0u;
    mvlc::Protected<SocketWorkPerformanceCounters> counters;
    u8 crateId = 0;

    std::array<u8, mvlc::MaxVMECrates> inputCrateMappings;
    std::array<u8, mvlc::MaxVMECrates> outputCrateMappings;

    EventBuilderContext()
    {
        for (size_t i=0; i<inputCrateMappings.size(); ++i)
        {
            inputCrateMappings[i] = i;
            outputCrateMappings[i] = i;
        }
    }
};

// Calls recordEventData and recordSystemEvent with data read from inputSocket.
void LIBMVME_EXPORT event_builder_record_loop(EventBuilderContext &context);
void LIBMVME_EXPORT event_builder_build_loop(EventBuilderContext &context);
// Combines the above two into a single loop. Somehow way faster than having the
// two loops in two threads
void LIBMVME_EXPORT event_builder_combined_loop(EventBuilderContext &context);

struct LIBMVME_EXPORT AnalysisProcessingContext
{
    std::atomic<bool> quit;
    nng_socket inputSocket;
    std::shared_ptr<analysis::Analysis> analysis;
    bool isReplay = false;
    multi_crate::MinimalAnalysisServiceProvider *asp = nullptr;
    mvlc::Protected<SocketWorkPerformanceCounters> inputSocketCounters;
    u8 crateId = 0;
};

// Consumes ParsedEventsMessageHeader type messages.
void LIBMVME_EXPORT analysis_loop(AnalysisProcessingContext &context);

struct LIBMVME_EXPORT ParsedDataConsumerContext
{
    std::atomic<bool> quit;
    nng_socket inputSocket;
    mvlc::Protected<SocketWorkPerformanceCounters> counters;
    std::string info;
    mvlc::Protected<std::array<size_t, mvlc::MaxVMECrates>> readoutEventCounts;
    mvlc::Protected<std::array<size_t, mvlc::MaxVMECrates>> systemEventCounts;
};

// Consumes ParsedEventsMessageHeader type messages.
void LIBMVME_EXPORT parsed_data_test_consumer_loop(ParsedDataConsumerContext &context);

struct MulticrateReplayContext
{
    std::atomic<bool> quit;
    mvlc::listfile::ReadHandle *lfh = nullptr;
    // output writers in crate order
    std::vector<std::unique_ptr<nng::OutputWriter>> writers;
    mvlc::Protected<std::vector<SocketWorkPerformanceCounters>> writerCounters;
};

LoopResult LIBMVME_EXPORT replay_loop(MulticrateReplayContext &context);

} // namespace mesytec::mvme::multi_crate

Q_DECLARE_METATYPE(mesytec::mvme::multi_crate::MulticrateVMEConfig *);

#endif /* __MVME_MULTI_CRATE_H__ */
