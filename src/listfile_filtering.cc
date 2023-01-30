#include "listfile_filtering.h"

#include <globals.h>
#include <mesytec-mvlc/mvlc_listfile_gen.h>
#include <mesytec-mvlc/mvlc_listfile_util.h>
#include <mesytec-mvlc/mvlc_listfile_zip.h>
#include <mesytec-mvlc/util/logging.h>
#include <QDebug>

#include "mvme_mvlc_listfile.h"
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

struct ListfileFilterStreamConsumer::Private
{
    Logger logger_;
    std::unique_ptr<listfile::SplitZipCreator> mvlcZipCreator;
    std::shared_ptr<listfile::WriteHandle> listfileWriteHandle;
    ReadoutBuffer outputBuffer;
};

ListfileFilterStreamConsumer::ListfileFilterStreamConsumer()
    : d(std::make_unique<Private>())
{
    spdlog::info("{} => {}", __PRETTY_FUNCTION__, fmt::ptr(this));

    d->outputBuffer = ReadoutBuffer(util::Megabytes(1));
}

ListfileFilterStreamConsumer::~ListfileFilterStreamConsumer()
{
    spdlog::info("{} destroying {}", __PRETTY_FUNCTION__, fmt::ptr(this));
}

void ListfileFilterStreamConsumer::setLogger(Logger logger)
{
    d->logger_ = logger;
}

StreamConsumerBase::Logger &ListfileFilterStreamConsumer::getLogger()
{
    return d->logger_;
}

#if 0
[2023-01-30 16:43:07.256] [info] virtual void ListfileFilterStreamConsumer::beginRun(const RunInfo&, const VMEConfig*, const analysis::Analysis*) @ 0x555556868390
"" "ExperimentName"
"\t" QVariant(QString, "Experiment")
"" "ExperimentTitle"
"\t" QVariant(QString, "")
"" "MVMEWorkspace"
"\t" QVariant(QString, "/home/florian/Documents/mvme-workspaces-on-hdd/2301_listfile-filtering")
"" "replaySourceFile"
"\t" QVariant(QString, "00-mdpp_trig.zip")

runId is "00-mdpp_trig"
#endif

void ListfileFilterStreamConsumer::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig, const analysis::Analysis *analysis)
{
    auto make_listfile_preamble = [&vmeConfig]() -> std::vector<u8>
    {
        listfile::BufferedWriteHandle bwh;
        listfile::listfile_write_magic(bwh, ConnectionType::USB);
        listfile::listfile_write_endian_marker(bwh);
        mvme_mvlc_listfile::listfile_write_mvme_config(bwh, *vmeConfig);
        return bwh.getBuffer();
    };

    spdlog::info("{} @ {}", __PRETTY_FUNCTION__, fmt::ptr(this));
    qDebug() << runInfo.runId;
    printMe(runInfo.infoDict);

    auto workspaceSettings = make_workspace_settings();

    // for make_new_listfile_name()
    ListFileOutputInfo lfOutInfo{};
    lfOutInfo.format = ListFileFormat::ZIP;
    // Not actually computing the absolute path here. Should still work as we are inside the workspace directory.
    lfOutInfo.fullDirectory = workspaceSettings.value("ListFileDirectory").toString();
    lfOutInfo.prefix = QFileInfo(runInfo.infoDict["replaySourceFile"].toString()).completeBaseName() + "_filtered";

    QFileInfo lfFileInfo(make_new_listfile_name(&lfOutInfo));
    auto lfDir = lfFileInfo.path();
    auto lfBase = lfFileInfo.completeBaseName();
    auto lfPrefix = lfDir + "/" + lfBase;

    listfile::SplitListfileSetup lfSetup;
    lfSetup.entryType = listfile::ZipEntryInfo::ZIP;
    lfSetup.compressLevel = 1;
    lfSetup.filenamePrefix = lfPrefix.toStdString();
    lfSetup.preamble = make_listfile_preamble();
    // TODO: set lfSetup.closeArchiveCallback to add additional files to the output archive

    spdlog::info("{} @ {}: output filename is {}", __PRETTY_FUNCTION__, fmt::ptr(this), lfSetup.filenamePrefix);

    d->mvlcZipCreator = std::make_unique<listfile::SplitZipCreator>();
    d->mvlcZipCreator->createArchive(lfSetup); // FIXME: does it throw? yes, it probably does
    d->listfileWriteHandle = std::shared_ptr<listfile::WriteHandle>(
        d->mvlcZipCreator->createListfileEntry());
    d->outputBuffer.clear();
}

void ListfileFilterStreamConsumer::endRun(const DAQStats &stats, const std::exception *e)
{
}

void ListfileFilterStreamConsumer::beginEvent(s32 eventIndex)
{
}

void ListfileFilterStreamConsumer::processModuleData(
    s32 crateIndex, s32 eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
{
}

void ListfileFilterStreamConsumer::processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size)
{
    assert(!"don't call me please!");
}

void ListfileFilterStreamConsumer::endEvent(s32 eventIndex)
{
}

void ListfileFilterStreamConsumer::processSystemEvent(s32 crateIndex, const u32 *header, u32 size)
{
}
