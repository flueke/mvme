#include "listfile_filtering.h"

#include <mesytec-mvlc/mvlc_listfile_gen.h>
#include <mesytec-mvlc/mvlc_listfile_util.h>
#include <mesytec-mvlc/mvlc_listfile_zip.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/protected.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>

#include "analysis/a2_adapter.h"
#include "analysis/analysis.h"
#include "analysis_service_provider.h"
#include "globals.h"
#include "mvlc_daq.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_mvlc_listfile.h"
#include "mvme_qthelp.h"
#include "mvme_workspace.h"
#include "run_info.h"
#include "vme_daq.h"

using namespace mesytec::mvlc;

namespace
{
    // https://stackoverflow.com/a/1759114/17562886
    template <class NonMap>
    struct Print
    {
        static void print(const QString &tabs, const NonMap &value)
        {
            qDebug() << tabs << value;
        }
    };

    template <class Key, class ValueType>
    struct Print<class QMap<Key, ValueType>>
    {
        static void print(const QString &tabs, const QMap<Key, ValueType> &map)
        {
            const QString extraTab = tabs + "\t";
            QMapIterator<Key, ValueType> iterator(map);
            while (iterator.hasNext())
            {
                iterator.next();
                qDebug() << tabs << iterator.key();
                Print<ValueType>::print(extraTab, iterator.value());
            }
        }
    };

    template <class Type>
    void printMe(const Type &type)
    {
        Print<Type>::print("", type);
    };
}

QVariant listfile_filter_config_to_variant(const ListfileFilterConfig &cfg)
{
    QVariantMap entryMap;

    for (auto it = cfg.eventEntries.begin(); it != cfg.eventEntries.end(); ++it)
        entryMap.insert(it.key().toString(), it.value().toString());

    QVariantMap result;
    result["EventConditionFilters"] = entryMap;
    result["ListfileOutputInfo"] = listfile_output_info_to_variant(cfg.outputInfo);
    result["FilteringEnabled"] = cfg.enabled;
    return result;
}

ListfileFilterConfig listfile_filter_config_from_variant(const QVariant &var)
{
    auto map = var.toMap();
    ListfileFilterConfig result;
    auto entryMap = map.value("EventConditionFilters").toMap();

    for (auto it = entryMap.begin(); it != entryMap.end(); ++it)
        result.eventEntries.insert(QUuid::fromString(it.key()), it.value().toUuid());

    result.outputInfo = listfile_output_info_from_variant(map.value("ListfileOutputInfo"));
    result.enabled = map.value("FilteringEnabled").toBool();
    return result;
}

struct FilterCounters
{
    std::array<u32, MaxVMEEvents> eventsWritten;
    std::array<u32, MaxVMEEvents> eventsSkipped;

    void reset()
    {
        std::fill(std::begin(eventsWritten), std::end(eventsWritten), 0u);
        std::fill(std::begin(eventsSkipped), std::end(eventsSkipped), 0u);
    }
};

struct ListfileFilterStreamConsumer::Private
{
    static const size_t OutputBufferInitialCapacity = mesytec::mvlc::util::Megabytes(1);
    // Leave some space to avoid reallocations of the buffer.
    static const size_t OutputBufferFlushSize = OutputBufferInitialCapacity-256;

    ListfileFilterConfig config_;
    // Indexes into the a2 condition bitset. In increasing event order, e.g [0]
    // is the bit index for event 0. Filled in beginRun().
    std::vector<int> eventConditionBitIndexes_;

    std::shared_ptr<spdlog::logger> logger_;
    Logger qtLogger_;

    RunInfo runInfo_;
    // TODO: make this a shared_ptr or something at some point. It's passed in
    // beginRun() and must stay valid during the run.
    analysis::Analysis *analysis_ = nullptr;
    std::unique_ptr<listfile::SplitZipCreator> mvlcZipCreator_;
    std::shared_ptr<listfile::WriteHandle> listfileWriteHandle_;
    ReadoutBuffer outputBuffer_;
    FilterCounters counters_;
    mesytec::mvlc::Protected<QString> runNotes_;

    void maybeFlushOutputBuffer(size_t flushSize = OutputBufferFlushSize);
};

ListfileFilterStreamConsumer::ListfileFilterStreamConsumer()
    : d(std::make_unique<Private>())
{
    d->logger_ = get_logger("ListfileFilterStreamConsumer");
    d->logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [tid%t] %v");
    d->logger_->set_level(spdlog::level::debug);

    d->outputBuffer_ = ReadoutBuffer(Private::OutputBufferInitialCapacity);
    d->config_.outputInfo.enabled = false;
}

ListfileFilterStreamConsumer::~ListfileFilterStreamConsumer()
{
}

void ListfileFilterStreamConsumer::setLogger(Logger logger)
{
    d->qtLogger_ = logger;
}

StreamConsumerBase::Logger &ListfileFilterStreamConsumer::getLogger()
{
    return d->qtLogger_;
}

void ListfileFilterStreamConsumer::beginRun(
    const RunInfo &runInfo, const VMEConfig *vmeConfig, analysis::Analysis *analysis)
{
    d->config_ = listfile_filter_config_from_variant(analysis->property("ListfileFilterConfig"));

    if (!runInfo.isReplay)
        d->config_.enabled = false;

    if (!d->config_.enabled)
        return;

    if (!is_mvlc_controller(vmeConfig->getControllerType()))
    {
        getLogger()("Listfile Filter Error: listfile filtering is only implemented for the MVLC controller");
        d->config_.enabled = false;
        return;
    }

    if (auto format = d->config_.outputInfo.format;
        format != ListFileFormat::ZIP && format != ListFileFormat::LZ4)
    {
        getLogger()("Listfile Filter Error: listfile filter can only output ZIP or LZ4 archives");
        d->config_.enabled = false;
        return;
    }

    d->runInfo_ = runInfo;
    d->analysis_ = analysis;
    //printMe(runInfo.infoDict);

    // Build the event -> condition bit index lookup table.
    {
        d->eventConditionBitIndexes_.clear();
        auto eventConfigs = vmeConfig->getEventConfigs();
        auto conditionBitIndexes = d->analysis_->getA2AdapterState()->conditionBitIndexes;

        for (int ei=0; ei<eventConfigs.size(); ++ei)
        {
            auto eventConfig = eventConfigs[ei];
            auto condId = d->config_.eventEntries.value(eventConfig->getId());

            if (!condId.isNull())
            {
                if (auto a1_cond = d->analysis_->getObject<analysis::ConditionInterface>(condId))
                {
                    if (auto bitIndex = conditionBitIndexes.value(a1_cond.get(), -1);
                        bitIndex >= 0)
                    {
                        d->eventConditionBitIndexes_.push_back(bitIndex);
                        continue;
                    }
                }

                getLogger()(QSL("Listfile Filter Error: Condition for event %1 not found. Check config!").arg(ei));
                d->config_.enabled = false;
                return;
            }

            // No condition selected for this event -> add negative condition index as an indicator.
            d->eventConditionBitIndexes_.push_back(-1);
        }
    }

    // Preamble to be written into every part of the output listfile.  Note: the
    // format magic is set to USB format. This may differ from the VME
    // controller type specified in the mvme VMEConfig and the MVLC CrateConfig
    // objects! Consumers of the filtered output listfile must take this into account!
    auto make_listfile_preamble = [&vmeConfig]() -> std::vector<u8>
    {
        listfile::BufferedWriteHandle bwh;
        listfile::listfile_write_magic(bwh, ConnectionType::USB);
        auto crateConfig = mesytec::mvme::vmeconfig_to_crateconfig(vmeConfig);
        listfile::listfile_write_endian_marker(bwh, crateConfig.crateId);
        listfile::listfile_write_crate_config(bwh, crateConfig);
        static const u8 crateId = 0; // FIXME: single crate only!
        mvme_mvlc_listfile::listfile_write_mvme_config(bwh, crateId, *vmeConfig);
        return bwh.getBuffer();
    };

    auto &outInfo = d->config_.outputInfo;
    auto lfSetup = mesytec::mvme_mvlc::make_listfile_setup(outInfo, make_listfile_preamble());

    lfSetup.closeArchiveCallback = [this] (listfile::SplitZipCreator *zipCreator)
    {
        assert(zipCreator->isOpen());
        assert(!zipCreator->hasOpenEntry());

        auto do_write = [zipCreator] (const std::string &filename, const QByteArray &data)
        {
            listfile::add_file_to_archive(zipCreator, filename, data);
        };

        do_write("messages.log",  d->runInfo_.infoDict["listfileLogBuffer"].toByteArray());
        if (d->analysis_)
            do_write("analysis.analysis", analysis::serialize_analysis_to_json_document(*d->analysis_).toJson());
        do_write("mvme_run_notes.txt", d->runNotes_.access().ref().toLocal8Bit());
    };

    try
    {
        d->mvlcZipCreator_ = std::make_unique<listfile::SplitZipCreator>();
        d->mvlcZipCreator_->createArchive(lfSetup);
        d->listfileWriteHandle_ = std::shared_ptr<listfile::WriteHandle>(
            d->mvlcZipCreator_->createListfileEntry());
        d->outputBuffer_.clear();
        d->counters_.reset();

        getLogger()(QSL("Listfile Filter: output file is %1").arg(d->mvlcZipCreator_->archiveName().c_str()));
    }
    catch (const std::exception &e)
    {
        getLogger()(QSL("Listfile Filter: error creating output file: %1").arg(e.what()));
        d->config_.enabled = false;
        return;
    }
}

void ListfileFilterStreamConsumer::endRun(const DAQStats &stats, const std::exception *e)
{
    (void) stats;
    (void) e;

    if (!d->config_.enabled)
        return;

    // Flush remaining data.
    d->maybeFlushOutputBuffer(0);

    const auto outputFilename = d->mvlcZipCreator_->archiveName();

    // Disable filtering after each run and write it back to the analysis
    // config. The user has to manually re-enable filtering again.
    // FIXME: only works when using an analysis instance that's shared between
    // gui and this code. Not thread-safe right now. Bad! Think up a design that
    // would also work without a shared analysis instance.
    d->config_.enabled = false;
    d->analysis_->setProperty("ListfileFilterConfig", listfile_filter_config_to_variant(d->config_));
    d->listfileWriteHandle_.reset();
    d->mvlcZipCreator_ = {};

    auto &logger = getLogger();
    logger(QSL("Listfile Filter: closed output file %1").arg(outputFilename.c_str()));

    const auto &counters = d->counters_;
    std::vector<std::string> msgs;

    for (size_t ei=0; ei<counters.eventsWritten.size(); ++ei)
    {
        if (!(counters.eventsWritten[ei] || counters.eventsSkipped[ei]))
            continue;

        msgs.emplace_back(fmt::format("eventIndex={}, events_written={:}, events_skipped={:}\n",
            ei, counters.eventsWritten[ei], counters.eventsSkipped[ei]));
    }

    for (const auto &msg: msgs)
    {
        logger(QSL("Listfile Filter Counters: %1").arg(msg.c_str()));
    }
}

void ListfileFilterStreamConsumer::Private::maybeFlushOutputBuffer(size_t flushSize)
{
    if (const auto used = outputBuffer_.used();
        used > flushSize)
    {
        logger_->debug("@{}: flushing output buffer, used={}, capacity={}", fmt::ptr(this), used, outputBuffer_.capacity());
        listfileWriteHandle_->write(outputBuffer_.data(), used);
        outputBuffer_.clear();
    }
}

void ListfileFilterStreamConsumer::beginEvent(s32 eventIndex)
{
    (void) eventIndex;
}

void ListfileFilterStreamConsumer::endEvent(s32 eventIndex)
{
    (void) eventIndex;
}

void ListfileFilterStreamConsumer::processModuleData(
    s32 crateIndex, s32 eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
{
    if (!d->config_.enabled)
        return;

    const auto &conditionBits = d->analysis_->getA2AdapterState()->a2->conditionBits;

    if (eventIndex < static_cast<signed>(d->eventConditionBitIndexes_.size()))
    {
        if (auto bitIndex = d->eventConditionBitIndexes_[eventIndex];
            0 <= bitIndex && static_cast<unsigned>(bitIndex) < conditionBits.size())
        {
            if (!conditionBits.test(bitIndex))
            {
                d->counters_.eventsSkipped[eventIndex]++;
                return;
            }
        }
    }

    listfile::write_event_data(d->outputBuffer_, crateIndex, eventIndex, moduleDataList, moduleCount);
    d->counters_.eventsWritten[eventIndex]++;
    d->maybeFlushOutputBuffer();
}

void ListfileFilterStreamConsumer::processSystemEvent(s32 crateIndex, const u32 *header, u32 size)
{
    if (!d->config_.enabled)
        return;

    if (!header || !size)
        return;

    using namespace mesytec::mvlc;

    auto info = extract_frame_info(*header);

    if (info.type == frame_headers::SystemEvent)
    {
        switch (info.sysEventSubType)
        {
            // These are written as part of the listfile preamble, prepared in beginRun().
            case system_event::subtype::EndianMarker:
            case system_event::subtype::MVMEConfig:
            case system_event::subtype::MVLCCrateConfig:
                return;

            default:
                break;
        }
    }

    // Note: just passing the event through works for MVLC inputs only, as
    // otherwise the system event header won't be compatible with the mvlc
    // listfile format.
    listfile::write_system_event(d->outputBuffer_, crateIndex, header, size);
    d->maybeFlushOutputBuffer();
}

void ListfileFilterStreamConsumer::processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size)
{
    (void) eventIndex;
    (void) moduleIndex;
    (void) data;
    (void) size;
    assert(!"don't call me please!");
    throw std::runtime_error(fmt::format("{}: don't call me please!", __PRETTY_FUNCTION__));
}

void ListfileFilterStreamConsumer::setRunNotes(const QString &runNotes)
{
    d->runNotes_.access().ref() = runNotes;
}

//
// ListfileFilterDialog
//

struct ListfileFilterDialog::Private
{
    AnalysisServiceProvider *asp_;
    ListfileFilterConfig config_;

    struct Row
    {
        QUuid eventId;
        QString eventName;
        QComboBox *combo_conditions;
    };

    QVector<Row> rows_;

    QComboBox *combo_outputFormat;
    QLineEdit *le_outputFilename;
    QCheckBox *cb_enableFiltering;
};

ListfileFilterDialog::ListfileFilterDialog(AnalysisServiceProvider *asp, QWidget *parent)
    : QDialog(parent)
    , d(std::make_unique<Private>())
{
    setWindowTitle("Listfile Filtering Setup");

    d->asp_ = asp;
    auto analysis = d->asp_->getAnalysis();
    auto vmeConfig = d->asp_->getVMEConfig();
    const auto &replayHandle = d->asp_->getReplayFileHandle();

    d->config_ = listfile_filter_config_from_variant(analysis->property("ListfileFilterConfig"));
    auto eventConfigs = vmeConfig->getEventConfigs();

    for (int ei=0; ei<eventConfigs.size(); ++ei)
    {
        auto event = eventConfigs[ei];
        auto conditions = analysis->getConditions(event->getId());
        auto combo = new QComboBox;
        combo->addItem("<none>", QUuid());

        for (auto cond: conditions)
            combo->addItem(cond->objectName(), cond->getId());

        if (auto activeCondId = d->config_.eventEntries.value(event->getId()); !activeCondId.isNull())
            combo->setCurrentIndex(combo->findData(activeCondId));

        d->rows_.push_back({ event->getId(), event->objectName(), combo });
    }

    // Event -> Filter Condition Table
    auto eventCondTable = new QTableWidget(d->rows_.size(), 2);
    eventCondTable->setHorizontalHeaderLabels({ "Event Name", "Filter Condition" });
    eventCondTable->horizontalHeader()->setStretchLastSection(false);
    eventCondTable->verticalHeader()->hide();

    for (int rowIdx = 0; rowIdx < d->rows_.size(); ++rowIdx)
    {
        auto &row = d->rows_[rowIdx];
        auto item = new QTableWidgetItem(row.eventName);
        item->setData(Qt::UserRole, row.eventId);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        eventCondTable->setItem(rowIdx, 0, item);
        eventCondTable->setCellWidget(rowIdx, 1, row.combo_conditions);
    }

    eventCondTable->resizeColumnsToContents();
    eventCondTable->resizeRowsToContents();

    auto gb_condTable = new QGroupBox("Filter Conditions");
    auto l_condTable = new QHBoxLayout(gb_condTable);
    l_condTable->addWidget(eventCondTable);

    // Listfile output options
    d->combo_outputFormat = new QComboBox;
    d->combo_outputFormat->addItem("ZIP", static_cast<int>(ListFileFormat::ZIP));
    d->combo_outputFormat->addItem("LZ4", static_cast<int>(ListFileFormat::LZ4));

    auto newFilename = d->config_.outputInfo.prefix;

    // Default value if no filter config was present in the analysis yet.
    if (newFilename == QSL("mvmelst"))
    {
        QFileInfo fi(replayHandle.inputFilename);
        newFilename = QSL("%1_filtered").arg(fi.baseName());
    }

    d->le_outputFilename = new QLineEdit;
    d->le_outputFilename->setText(newFilename);
    d->cb_enableFiltering = new QCheckBox("Enable Filtering for the next replay");
    d->cb_enableFiltering->setChecked(d->config_.enabled);

    auto gb_listfile = new QGroupBox("Listfile Output");
    auto l_listfile = new QFormLayout(gb_listfile);
    l_listfile->addRow("Output Format", d->combo_outputFormat);
    l_listfile->addRow("Output Filename Prefix", d->le_outputFilename);
    l_listfile->addRow(d->cb_enableFiltering);

    // buttonbox: ok, cancel, help
    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    auto bbLayout = new QHBoxLayout;
    bbLayout->addStretch(1);
    bbLayout->addWidget(bb);

    auto dialogLayout = new QVBoxLayout(this);
    dialogLayout->setContentsMargins(2, 2, 2, 2);
    dialogLayout->addWidget(gb_condTable);
    dialogLayout->addWidget(gb_listfile);
    dialogLayout->addLayout(bbLayout);
    dialogLayout->setStretch(0, 1);

    QObject::connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QObject::connect(bb, &QDialogButtonBox::helpRequested,
                     this, mesytec::mvme::make_help_keyword_handler("Listfile Filtering"));
    resize(600, 300);
}

ListfileFilterDialog::~ListfileFilterDialog()
{
}

void ListfileFilterDialog::accept()
{
    ListfileFilterConfig filterConfig;

    for (auto &row: d->rows_)
        filterConfig.eventEntries.insert(row.eventId, row.combo_conditions->currentData().toUuid());

    filterConfig.outputInfo.format = static_cast<ListFileFormat>(d->combo_outputFormat->currentData().toInt());
    filterConfig.outputInfo.prefix = d->le_outputFilename->text();
    filterConfig.outputInfo.suffix.clear();
    filterConfig.enabled = d->cb_enableFiltering->isChecked();

    auto analysis = d->asp_->getAnalysis();
    auto currentConfig = analysis->property("ListfileFilterConfig");
    auto newConfig = listfile_filter_config_to_variant(filterConfig);

    if (currentConfig != newConfig)
    {
        analysis->setProperty("ListfileFilterConfig", newConfig);
        analysis->setModified();
    }

    done(QDialog::Accepted);
}
