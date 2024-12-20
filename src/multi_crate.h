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
#include "util/mesy_nng_pipeline.h"
#include "vme_config.h"

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
std::unique_ptr<VMEConfig> make_merged_vme_config_keep_ids(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents);

inline std::unique_ptr<VMEConfig> make_merged_vme_config_keep_ids(
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
        if (!msg_)
            throw std::runtime_error(fmt::format("NngMsgWriteHandle: msg is null!"));
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
    u8 crateId = 0;
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
};

static_assert(sizeof(ReadoutDataMessageHeader) % sizeof(u32) == 0);

// Message header for parsed data and system events. Can carry data from
// different crates but still has a crateId field in the header. Even if data
// from multiple crates is mixed within these messages the producer code should
// be able to output an increasing message number for per-crate buffer loss
// calculations.
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
    // ParsedDataEventMagic or ParsedSystemEventMagic.
    u8 magicByte;

    // The events crate index. May be different than the value of the outer
    // ParsedEventsMessageHeader.crateId field.
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
        std::weak_ptr<analysis::Analysis> analysis_;
        QString analysisConfigFilename_;
        std::shared_ptr<WidgetRegistry> widgetRegistry_;
        // artifacts from mvme. Don't think it's needed in the future.
        ListfileReplayHandle listfileReplayHandle_;
        mutable mvlc::Protected<DAQStats> daqStats_;
        RunInfo runInfo_;
};


struct LIBMVME_EXPORT SocketWorkPerformanceCounters
{
    std::chrono::steady_clock::time_point tpStart = {};
    std::chrono::steady_clock::time_point tpStop = {};
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
        tReceive = tProcess = tSend = tTotal = {};
        messagesReceived = messagesLost = messagesSent = bytesReceived = bytesSent = 0;
        tpStart = std::chrono::steady_clock::now();
        tpStop = {};
    }

    void stop()
    {
        tpStop = std::chrono::steady_clock::now();
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

    // Readout data goes here. Format: MessageType::ReadoutData
    std::unique_ptr<nng::OutputWriter> outputWriter;

    mvlc::Protected<SocketWorkPerformanceCounters> dataOutputCounters;
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

    // Readout data goes here. Format: MessageType::ReadoutData
    std::unique_ptr<nng::OutputWriter> outputWriter;

    mvlc::Protected<SocketWorkPerformanceCounters> dataOutputCounters;
};

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

// Helper class for reading ParsedEventsMessages.
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

} // namespace mesytec::mvme::multi_crate

Q_DECLARE_METATYPE(mesytec::mvme::multi_crate::MulticrateVMEConfig *);

#endif /* __MVME_MULTI_CRATE_H__ */
