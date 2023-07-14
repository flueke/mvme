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
#ifndef __MVME_LISTFILE_REPLAY_H__
#define __MVME_LISTFILE_REPLAY_H__

#include "libmvme_export.h"

#include <memory>
#include <quazip.h>
#include <QDebug>
#include <mesytec-mvlc/readout_buffer_queues.h>
#include <mesytec-mvlc/util/protected.h>

#include "databuffer.h"
#include "globals.h"
#include "vme_config.h"

class ListfileReplayWorker;
class StreamWorkerBase;

struct LIBMVME_EXPORT ListfileReplayHandle
{
    // The actual listfile. This is a file inside the archive if replaying from
    // ZIP. As long as this file is open no other file member of the archive can
    // be opened through the same QuaZip instance. This is a restriction of the
    // ZIP library.  If replaying from flat file this is a plain QFile instance.
    // Note: Not used for MVLC listfiles stored inside ZIP archives. Still used
    // for flat MVLC listfiles.
    std::unique_ptr<QIODevice> listfile;

    // The ZIP archive containing the listfile or nullptr if playing directly
    // from a listfile.
    std::unique_ptr<QuaZip> archive;

    // Format of the data stored in the listfile. Detected by looking at the
    // first 8 bytes of the file. Defaults to the old MVMELST format if none of
    // the newer MVLC types match.
    ListfileBufferFormat format;

    QString inputFilename;      // For ZIP archives this is the name of the ZIP file.
                                // For raw listfiles it's the filename that was
                                // passed to open_listfile().

    QString listfileFilename;   // For ZIP archives it's the name of the
                                // listfile inside the archive. Otherwise the
                                // same as inputFilename.

    QByteArray messages;        // Contents of messages.log if found.
    QByteArray analysisBlob;    // Analysis config contents if present in the archive.
    QString runNotes;           // Contents of the mvme_run_notes.txt file stored in the archive.

    ListfileReplayHandle() = default;

    ~ListfileReplayHandle()
    {
        // This needs to be done manually because the default destruction order
        // will destroy the QuaZIP archive member first which will close any
        // open file inside the archive. Thus the QuaZipFile data stored in the
        // listfile member will become invalid.
        if (listfile)
            listfile->close();
    }

    ListfileReplayHandle(ListfileReplayHandle &&) = default;
    ListfileReplayHandle &operator=(ListfileReplayHandle &&) = default;

    ListfileReplayHandle(const ListfileReplayHandle &) = delete;
    ListfileReplayHandle &operator=(const ListfileReplayHandle &) = delete;
};

// IMPORTANT/FIXME: throws QString on error :-(
ListfileReplayHandle LIBMVME_EXPORT open_listfile(const QString &filename);

std::pair<std::unique_ptr<VMEConfig>, std::error_code>
LIBMVME_EXPORT read_vme_config_from_listfile(
        ListfileReplayHandle &handle,
        std::function<void (const QString &msg)> logger = {});

namespace mesytec::mvme::replay
{

// Messy error mess
struct LIBMVME_EXPORT ErrorInfo
{
    QString errorString;
    std::error_code errorCode;
    std::exception_ptr exceptionPtr;

    bool hasError() const
    {
        return !errorString.isEmpty() || errorCode || exceptionPtr;
    }
};

struct LIBMVME_EXPORT ListfileInfo
{
    QUrl fileUrl;
    ListfileReplayHandle handle;
    std::unique_ptr<VMEConfig> vmeConfig;
    std::chrono::steady_clock::time_point updateTime;
    ErrorInfo err; // error mess number 1

    bool hasError() const
    {
        return err.hasError();
    }
};

ListfileInfo LIBMVME_EXPORT gather_fileinfo(const QUrl &url);
QVector<std::shared_ptr<ListfileInfo>> LIBMVME_EXPORT gather_fileinfos(const QVector<QUrl> &urls);

class LIBMVME_EXPORT FileInfoCache: public QObject
{
    Q_OBJECT
    signals:
        void cacheUpdated();

    public:
        FileInfoCache(QObject *parent = nullptr);
        ~FileInfoCache() override;

        bool contains(const QUrl &url);
        std::shared_ptr<ListfileInfo> operator[](const QUrl &url) const;
        std::shared_ptr<ListfileInfo> value(const QUrl &url) const;
        QMap<QUrl, std::shared_ptr<ListfileInfo>> cache() const;

    public slots:
        void requestInfo(const QUrl &url);
        void requestInfos(const QVector<QUrl> &urls);
        void clear();

    private:
       struct Private;
       std::unique_ptr<Private> d;
};

struct LIBMVME_EXPORT ReplayQueues
{
    mesytec::mvlc::ReadoutBufferQueues mvlcQueues; // the MVLC replays
    mesytec::mvlc::ReadoutBufferQueues_<DataBuffer> mvmelstQueues; // for the old mvmelst format used by VMUSB and SIS3153 controllers
};

std::unique_ptr<ListfileReplayWorker> LIBMVME_EXPORT make_replay_worker(const ListfileBufferFormat &fmt, ReplayQueues &queues);
std::unique_ptr<ListfileReplayWorker> LIBMVME_EXPORT make_replay_worker(const ListfileReplayHandle &h, ReplayQueues &queues);

std::unique_ptr<StreamWorkerBase> LIBMVME_EXPORT make_analysis_worker(const ListfileReplayHandle &fmt, ReplayQueues &queues);
std::unique_ptr<StreamWorkerBase> LIBMVME_EXPORT make_analysis_worker(const ListfileReplayHandle &h, ReplayQueues &queues);

// Commands:
// replay   analysis
// merge    output filename + optional analysis to append
// split    split rules, e.g. timetick or event count based; output filename; produces MVLC_USB format
// filter   analysis and condition(s) to use for filtering. output filename; produces MVLC_USB format
struct LIBMVME_EXPORT ListfileCommandBase
{
    QVector<QUrl> queue;
};

struct LIBMVME_EXPORT ReplayCommand: public ListfileCommandBase
{
    QByteArray analysisBlob;
    QString analysisFilename; // For info purposes only. Data is kept in the blob.
};

struct LIBMVME_EXPORT MergeCommand: public ListfileCommandBase
{
    QString outputFilename;
};

struct LIBMVME_EXPORT SplitCommand: public ListfileCommandBase
{
    enum SplitCondition
    {
        Duration,
        CompressedSize,
        UncompressedSize
    };

    SplitCondition condition;
    std::chrono::seconds splitInterval;
    size_t splitSize;
    QString outputBasename;
};

struct LIBMVME_EXPORT FilterCommand: public ListfileCommandBase
{
    QByteArray analysisBlob;
    QString analysisFilename; // For info purposes only. Data is kept in the blob.
    QUuid filterCondition; // Id of the analysis condition to use for filtering.
    QString outputFilename;
};

enum ReplayCommandType
{
    Replay,
    Merge,
    Split,
    Filter,
};

using CommandHolder = std::variant<ReplayCommand, MergeCommand, SplitCommand, FilterCommand>;

struct LIBMVME_EXPORT ListfileCommandState
{
    int currentQueueIndex;
    ErrorInfo err; // error mess number 2

    bool hasError() const
    {
        return err.hasError();
    }
};

struct LIBMVME_EXPORT ReplayCommandState: public ListfileCommandState
{
    std::shared_ptr<ListfileReplayWorker> replayWorker;
    std::shared_ptr<StreamWorkerBase> analysisWorker;

    void pause();
    void resume();
};

struct LIBMVME_EXPORT MergeCommandState: public ListfileCommandState
{
    void pause() {}
    void resume() {}
};

struct LIBMVME_EXPORT SplitCommandState: public ListfileCommandState
{
    void pause() {}
    void resume() {}
};

struct LIBMVME_EXPORT FilterCommandState: public ListfileCommandState
{
    void pause() {}
    void resume() {}
};

using CommandStateHolder = std::variant<ReplayCommandState, MergeCommandState, SplitCommandState, FilterCommandState>;

class LIBMVME_EXPORT ListfileCommandExecutor: public QObject
{
    Q_OBJECT
    signals:
        void globalProgressChanged(int cur, int max);
        void subProgressChanged(int cur, int max);
        void listfileChanged(const QString &filename);

        // This part of the interface is very similar to that of QFutureWatcher
        void started();
        void finished();
        void canceled();
        void paused();
        void resumed();

    public:
        enum State { Idle, Running, Paused };
        ListfileCommandExecutor(QObject *parent = nullptr);
        ~ListfileCommandExecutor() override;

        State state() const;

        bool setCommand(const CommandHolder &cmd); // Returns false if not idle.
        mesytec::mvlc::WaitableProtected<CommandStateHolder> &commandState();
        void setLogger(const std::function<void (const QString &)> &logger);

    public slots:
        void start();
        void cancel();
        void pause();
        void resume();
        void skip(); // skip to the next listfile in the queue

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif /* __MVME_LISTFILE_REPLAY_H__ */
