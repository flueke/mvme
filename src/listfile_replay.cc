/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "listfile_replay.h"

#include <array>
#include <cstring>
#include <QJsonDocument>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "analysis/analysis_util.h"
#include "mvlc_listfile_worker.h"
#include "mvlc_stream_worker.h"
#include "mvme_listfile_utils.h"
#include "mvme_listfile_worker.h"
#include "mvme_mvlc_listfile.h"
#include "mvme_stream_worker.h"
#include "qt_util.h"
#include "util_zip.h"
#include "vme_config_json_schema_updates.h"

using namespace mesytec;
using mesytec::mvlc::WaitableProtected;

namespace
{

ListfileBufferFormat detect_listfile_format(QIODevice *listfile)
{
    seek_in_file(listfile, 0);

    std::array<char, 8> buffer;

    // Note: older MVMELST files did not have a preamble at all, newer versions
    // do contain 'MVME' as a preamble so we default to MVMELST format.
    ListfileBufferFormat result = ListfileBufferFormat::MVMELST;

    ssize_t bytesRead = listfile->read(buffer.data(), buffer.size());

    if (std::strncmp(buffer.data(), "MVLC_ETH", bytesRead) == 0)
        result = ListfileBufferFormat::MVLC_ETH;
    else if (std::strncmp(buffer.data(), "MVLC_USB", bytesRead) == 0)
        result = ListfileBufferFormat::MVLC_USB;

    seek_in_file(listfile, 0);

    return result;
}

}

ListfileReplayHandle open_listfile(const QString &filename)
{
    ListfileReplayHandle result;
    result.inputFilename = filename;

    // ZIP archive
    if (filename.toLower().endsWith(QSL(".zip")))
    {
        // Try reading the analysis config and the messages.log file from the
        // archive and store the contents in the result variables.
        // QuaZipFile creates an internal QuaZip instance and uses that to
        // unzip the file contents.
        {
            QuaZipFile f(filename, QSL("analysis.analysis"));
            if (f.open(QIODevice::ReadOnly))
                result.analysisBlob = f.readAll();
        }

        {
            QuaZipFile f(filename, QSL("messages.log"));
            if (f.open(QIODevice::ReadOnly))
                result.messages = f.readAll();
        }

        {
            QuaZipFile f(filename, QSL("mvme_run_notes.txt"));
            if (f.open(QIODevice::ReadOnly))
                result.runNotes = QString::fromLocal8Bit(f.readAll());
        }

        // Now open the archive manually and search for a listfile
        result.archive = std::make_unique<QuaZip>(filename);

        if (!result.archive->open(QuaZip::mdUnzip))
        {
            throw make_zip_error_string(
                "Could not open archive", result.archive.get());
        }

        QStringList fileNames = result.archive->getFileNameList();

        auto it = std::find_if(fileNames.begin(), fileNames.end(), [](const QString &str) {
            return (str.endsWith(QSL(".mvmelst"))
                    || str.endsWith(QSL(".mvlclst"))
                    || str.endsWith(QSL(".mvlclst.lz4"))
                    || str.endsWith(QSL(".mvlc_eth"))
                    || str.endsWith(QSL(".mvlc_usb"))
                    );
        });

        if (it == fileNames.end())
            throw QString("No listfile found inside %1").arg(filename);

        result.listfileFilename = *it;
        auto lfName  = *it;

        // Check whether we're dealing with an MVLC listfile or with a classic
        // mvmelst listfile.
        if (lfName.endsWith(QSL(".mvlclst"))
            || lfName.endsWith(QSL(".mvlclst.lz4"))
            || lfName.endsWith(QSL(".mvlc_eth"))
            || lfName.endsWith(QSL(".mvlc_usb")))
        {
            try
            {
                mvlc::listfile::ZipReader zipReader;
                zipReader.openArchive(filename.toStdString());
                auto lfh = zipReader.openEntry(lfName.toStdString());
                auto preamble = mvlc::listfile::read_preamble(*lfh);
                if (preamble.magic == mvlc::listfile::get_filemagic_eth())
                    result.format = ListfileBufferFormat::MVLC_ETH;
                else if (preamble.magic == mvlc::listfile::get_filemagic_usb())
                    result.format = ListfileBufferFormat::MVLC_USB;
                else
                    throw QString("Unknown listfile format (file=%1, listfile=%2")
                        .arg(filename).arg(lfName);
            }
            catch (const std::runtime_error &e)
            {
                throw QString("Error detecting listfile format (file=%1, error=%2")
                    .arg(filename).arg(e.what());
            }
        }
        else // mvmelst file
        {
            result.archive->setCurrentFile(result.listfileFilename);

            {
                auto zipFile = std::make_unique<QuaZipFile>(result.archive.get());
                zipFile->setFileName(result.listfileFilename);
                result.listfile = std::move(zipFile);
            }

            if (!result.listfile->open(QIODevice::ReadOnly))
            {
                throw make_zip_error_string(
                    "Could not open listfile",
                    reinterpret_cast<QuaZipFile *>(result.listfile.get()));
            }

            assert(result.listfile);
            result.format = detect_listfile_format(result.listfile.get());
        }
    }
    else // flat file
    {
        result.listfile = std::make_unique<QFile>(filename);

        if (!result.listfile->open(QIODevice::ReadOnly))
        {
            throw QString("Error opening %1 for reading: %2")
                .arg(filename)
                .arg(result.listfile->errorString());
        }

        assert(result.listfile);
        result.format = detect_listfile_format(result.listfile.get());
    }

    return result;
}

std::pair<std::unique_ptr<VMEConfig>, std::error_code>
    read_vme_config_from_listfile(
        ListfileReplayHandle &handle,
        std::function<void (const QString &msg)> logger)
{
    switch (handle.format)
    {
        case ListfileBufferFormat::MVMELST:
            {
                ListFile lf(handle.listfile.get());
                lf.open();
                return read_config_from_listfile(&lf, logger);
            }

        case ListfileBufferFormat::MVLC_ETH:
        case ListfileBufferFormat::MVLC_USB:
            {
                mvlc::listfile::ZipReader zipReader;
                zipReader.openArchive(handle.inputFilename.toStdString());
                auto lfh = zipReader.openEntry(handle.listfileFilename.toStdString());
                auto preamble = mvlc::listfile::read_preamble(*lfh);

                #if 0
                for (const auto &sysEvent: preamble.systemEvents)
                {
                    qDebug() << __PRETTY_FUNCTION__ << "found preamble sysEvent type"
                        << mvlc::system_event_type_to_string(sysEvent.type).c_str();
                }
                #endif

                auto it = std::find_if(
                    std::begin(preamble.systemEvents),
                    std::end(preamble.systemEvents),
                    [] (const mvlc::listfile::SystemEvent &sysEvent)
                    {
                        return sysEvent.type == mvlc::system_event::subtype::MVMEConfig;
                    });

                // TODO: implement a fallback using the MVLCCrateConfig data
                // (or make it an explicit action in the GUI and put the code
                // elsewhere)
                if (it == std::end(preamble.systemEvents))
                    throw std::runtime_error("No MVMEConfig found in listfile");

                //qDebug() << __PRETTY_FUNCTION__ << "found MVMEConfig in listfile preamble, size =" << it->contents.size();

                QByteArray qbytes(
                    reinterpret_cast<const char *>(it->contents.data()),
                    it->contents.size());

                auto doc = QJsonDocument::fromJson(qbytes);
                auto json = doc.object();
                json = json.value("VMEConfig").toObject();

                mvme::vme_config::json_schema::SchemaUpdateOptions updateOptions;
                updateOptions.skip_v4_VMEScriptVariableUpdate = true;

                json = mvme::vme_config::json_schema::convert_vmeconfig_to_current_version(json, logger, updateOptions);
                auto vmeConfig = std::make_unique<VMEConfig>();
                auto ec = vmeConfig->read(json);
                return std::pair<std::unique_ptr<VMEConfig>, std::error_code>(
                    std::move(vmeConfig), ec);
            }
            break;
    }

    return {};
}

namespace mesytec::mvme::replay
{

ListfileInfo gather_fileinfo(const QUrl &url)
{
    ListfileInfo result = {};

    try
    {
        result.fileUrl = url;
        result.handle = open_listfile(url.path());
        auto [vmeConfig, ec] = read_vme_config_from_listfile(result.handle);
        result.vmeConfig = std::move(vmeConfig);
        result.err.errorCode = ec;
        result.updateTime = std::chrono::steady_clock::now();
    }
    catch(const QString &e)
    {
        result.err.errorString = e;
    }
    catch(const std::exception &)
    {
        result.err.exceptionPtr = std::current_exception();
    }

    return result;
}

static std::shared_ptr<ListfileInfo> gather_fileinfo_p(const QUrl &url)
{
    return std::make_shared<ListfileInfo>(gather_fileinfo(url));
}

QVector<std::shared_ptr<ListfileInfo>> gather_fileinfos(const QVector<QUrl> &urls)
{
    return QtConcurrent::blockingMapped(urls, gather_fileinfo_p);
}

struct FileInfoCache::Private
{
    FileInfoCache *q = nullptr;
    QMap<QUrl, std::shared_ptr<ListfileInfo>> cache_;
    QFutureWatcher<QVector<std::shared_ptr<ListfileInfo>>> gatherWatcher_;
    QSet<QUrl> gatherQueue_;

    void onGatherFinished()
    {
        auto results = gatherWatcher_.future().result();

        for (const auto &fileInfo: results)
            cache_.insert(fileInfo->fileUrl, fileInfo);

        emit q->cacheUpdated();

        startGatherMissing();
    }

    void startGatherMissing()
    {
        auto known = QSet<QUrl>::fromList(cache_.keys());
        gatherQueue_.subtract(known);

        if (!gatherQueue_.isEmpty())
            startGatherFileInfos(QVector<QUrl>::fromList(gatherQueue_.toList()));

    }

    void startGatherFileInfos(const QVector<QUrl> &urls)
    {
        if (gatherWatcher_.isFinished())
            gatherWatcher_.setFuture(QtConcurrent::run(gather_fileinfos, urls));
    }
};

FileInfoCache::FileInfoCache(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;

    connect(&d->gatherWatcher_, &QFutureWatcher<std::shared_ptr<ListfileInfo>>::finished,
            this, [this] { d->onGatherFinished(); });
}

FileInfoCache::~FileInfoCache()
{
    d->gatherWatcher_.waitForFinished();
}

bool FileInfoCache::contains(const QUrl &url)
{
    return d->cache_.contains(url);
}

std::shared_ptr<ListfileInfo> FileInfoCache::operator[](const QUrl &url) const
{
    return value(url);
}

std::shared_ptr<ListfileInfo> FileInfoCache::value(const QUrl &url) const
{
    return d->cache_.value(url);
}

void FileInfoCache::requestInfo(const QUrl &url)
{
    d->gatherQueue_.insert(url);
    d->startGatherMissing();
}

void FileInfoCache::requestInfos(const QVector<QUrl> &urls)
{
    for (const auto &url: urls)
        d->gatherQueue_.insert(url);
    d->startGatherMissing();
}

void FileInfoCache::clear()
{
    d->cache_.clear();
}

QMap<QUrl, std::shared_ptr<ListfileInfo>> FileInfoCache::cache() const
{
    return d->cache_;
}

std::unique_ptr<ListfileReplayWorker> make_replay_worker(
    const ListfileBufferFormat &fmt, ReplayQueues &queues)
{
    switch (fmt)
    {
        case ListfileBufferFormat::MVLC_ETH:
        case ListfileBufferFormat::MVLC_USB:
            {
                auto result = std::make_unique<MVLCListfileWorker>();
                result->setSnoopQueues(&queues.mvlcQueues);
                return result;
            }

        case ListfileBufferFormat::MVMELST:
            {
                auto result = std::make_unique<MVMEListfileWorker>(
                    &queues.mvmelstQueues.emptyBufferQueue(),
                    &queues.mvmelstQueues.filledBufferQueue());
                return result;
            }
    }

    return {};
}

std::unique_ptr<ListfileReplayWorker> make_replay_worker(
    const ListfileReplayHandle &h, ReplayQueues &queues)
{
    return make_replay_worker(h.format, queues);
}

std::unique_ptr<StreamWorkerBase> make_analysis_worker(
    const ListfileBufferFormat &fmt, ReplayQueues &queues)
{
    switch (fmt)
    {
        case ListfileBufferFormat::MVLC_ETH:
        case ListfileBufferFormat::MVLC_USB:
            return std::make_unique<MVLC_StreamWorker>(queues.mvlcQueues);

        case ListfileBufferFormat::MVMELST:
            return std::make_unique<MVMEStreamWorker>(
                &queues.mvmelstQueues.emptyBufferQueue(),
                &queues.mvmelstQueues.filledBufferQueue());
    }

    return {};
}

std::unique_ptr<StreamWorkerBase> make_analysis_worker(
    const ListfileReplayHandle &h, ReplayQueues &queues)
{
    return make_analysis_worker(h.format, queues);
}

void ReplayCommandState::pause()
{
        if (replayWorker) replayWorker->pause();
        if (analysisWorker) analysisWorker->pause();
}

void ReplayCommandState::resume()
{
        if (replayWorker) replayWorker->resume();
        if (analysisWorker) analysisWorker->resume();
}

struct ListfileCommandExecutor::Private
{
    ListfileCommandExecutor *q = nullptr;
    WaitableProtected<State> state_;
    CommandHolder cmd_;
    WaitableProtected<CommandStateHolder> cmdState_;

    std::atomic<bool> canceled_;
    std::thread runThread_;

    std::function<void (const QString &)> logger_;

    explicit Private()
        : state_{State::Idle}
    {}

    void run();

    void operator()(const ReplayCommand &cmd);
    void operator()(const MergeCommand &cmd);
    void operator()(const SplitCommand &cmd);
    void operator()(const FilterCommand &cmd);
};

ListfileCommandExecutor::ListfileCommandExecutor(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
}

ListfileCommandExecutor::~ListfileCommandExecutor()
{
    d->canceled_ = true;
    if (d->runThread_.joinable())
        d->runThread_.join();
}

ListfileCommandExecutor::State ListfileCommandExecutor::state() const
{
    return d->state_.copy();
}

bool ListfileCommandExecutor::setCommand(const CommandHolder &cmd)
{
    if (auto access = d->state_.access(); access.ref() == State::Idle)
    {
        d->cmd_ = cmd;
        return true;
    }

    return false;
}

mesytec::mvlc::WaitableProtected<CommandStateHolder> &ListfileCommandExecutor::commandState()
{
    return d->cmdState_;
}

void ListfileCommandExecutor::setLogger(const std::function<void (const QString &)> &logger)
{
    d->logger_ = logger;
}

void ListfileCommandExecutor::start()
{
    auto stateAccess = d->state_.access();
    auto &state = stateAccess.ref();

    if (state == State::Idle && d->cmd_.index() != std::variant_npos)
    {
        if (d->runThread_.joinable())
            d->runThread_.join();
        d->canceled_ = false;
        d->runThread_ = std::thread(&Private::run, d.get());
        state = State::Running;
        emit started();
    }
    else if (state != State::Idle)
    {
        qDebug() << __PRETTY_FUNCTION__ << "Error: state != Idle, aborting";
    }
    else if (d->cmd_.index() == std::variant_npos)
    {
        qDebug() << __PRETTY_FUNCTION__ << "Error: ReplayCommand not set";
    }
}

void ListfileCommandExecutor::cancel()
{
    d->canceled_ = true;
}

void ListfileCommandExecutor::pause()
{
    auto cmdStateAccess = d->cmdState_.access();
    if (cmdStateAccess.ref().index() != std::variant_npos)
    {
        std::visit([] (auto &cmdState) { cmdState.pause(); }, cmdStateAccess.ref());
        d->state_.access().ref() = State::Paused;
        emit paused();
    }
}

void ListfileCommandExecutor::resume()
{
    auto cmdStateAccess = d->cmdState_.access();
    if (cmdStateAccess.ref().index() != std::variant_npos)
    {
        std::visit([] (auto &cmdState) { cmdState.resume(); }, cmdStateAccess.ref());
        d->state_.access().ref() = State::Running;
        emit resumed();
    }
}

void ListfileCommandExecutor::skip()
{
}

void ListfileCommandExecutor::Private::run()
{
    std::visit(*this, cmd_);
    state_.access().ref() = State::Idle;

    if (canceled_)
        emit q->canceled();
    else
        emit q->finished();
}

void ListfileCommandExecutor::Private::operator()(const ReplayCommand &cmd)
{
    // TODO: things that need to be accessed from outside while the operation is
    // ongoing:
    // - errors
    // - analysis, streamworkerr

    cmdState_.access().ref() = ReplayCommandState{};

    auto [ana, ec] = analysis::read_analysis(QJsonDocument::fromJson(cmd.analysisBlob));

    if (ec)
    {
        std::get<ReplayCommandState>(cmdState_.access().ref()).err.errorCode = ec;
        return;
    }

    RunInfo runInfo;
    runInfo.keepAnalysisState = true;
    runInfo.isReplay = true;

    ReplayQueues bufferQueues;

    const auto queueSize = cmd.queue.size();

    for (int queueIndex = 0; queueIndex < queueSize; ++queueIndex)
    {
        const auto &url = cmd.queue[queueIndex];
        auto info = gather_fileinfo(url);

        if (info.hasError())
        {
            std::get<ReplayCommandState>(cmdState_.access().ref()).err = info.err;
            return;
        }

        auto replayWorker = make_replay_worker(info.handle, bufferQueues);

        if (!replayWorker)
        {
            std::get<ReplayCommandState>(cmdState_.access().ref()).err.errorString =
                QSL("Could not create replay worker for %1").arg(url.toString());
            return;
        }

        qDebug() << "replayWorker=" << replayWorker.get();

        replayWorker->setLogger(logger_);
        replayWorker->setListfile(&info.handle);

        auto analysisWorker = make_analysis_worker(info.handle, bufferQueues);
        analysisWorker->setAnalysis(ana.get());
        analysisWorker->setVMEConfig(info.vmeConfig.get());
        // TODO / FIXME: setWorkspaceDirectory() for the analysis session saving :(
        analysisWorker->setRunInfo(runInfo);
        ana->beginRun(runInfo, info.vmeConfig.get());

        std::get<ReplayCommandState>(cmdState_.access().ref()).err = info.err;

        auto fReplay = QtConcurrent::run([&] { replayWorker->start(); }); // returns when done
        auto fAnalysis = QtConcurrent::run([&] { analysisWorker->start(); }); // does not return unless stopped

        while (!fReplay.isFinished() && !canceled_)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (canceled_)
        {
            replayWorker->stop();
            analysisWorker->stop(false);
        }

        fReplay.waitForFinished();

        if (!canceled_)
            analysisWorker->stop(true);

        fAnalysis.waitForFinished();

        qDebug() << "done" << queueIndex << queueSize << canceled_.load();

        if (canceled_)
            break;
    }
}

void ListfileCommandExecutor::Private::operator()(const MergeCommand &cmd)
{
    (void) cmd;
}

void ListfileCommandExecutor::Private::operator()(const SplitCommand &cmd)
{
    (void) cmd;
}

void ListfileCommandExecutor::Private::operator()(const FilterCommand &cmd)
{
    (void) cmd;
}


}
