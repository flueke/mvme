#ifndef _MVME_SRC_REPLAY_UI_H_
#define _MVME_SRC_REPLAY_UI_H_

#include <QVector>
#include <QUuid>
#include <QWidget>

#include <memory>
#include <variant>

#include "libmvme_export.h"

namespace mesytec::mvme
{

// Logic part to be moved into separate files.
namespace replay
{

// Commands:
// replay   analysis
// merge    output filename + optional analysis to append
// split    split rules, e.g. timetick or event count based; output filename; produces MVLC_USB format
// filter   analysis and condition(s) to use for filtering. output filename; produces MVLC_USB format

struct ListfileCommandBase
{
    QVector<QUrl> queue;
};

struct ReplayCommand: public ListfileCommandBase
{
    QByteArray analysisBlob;
    QString analysisFilename; // For info purposes only. Data is kept in the blob.
};

struct MergeCommand: public ListfileCommandBase
{
    QString outputFilename;
};

struct SplitCommand: public ListfileCommandBase
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

struct FilterCommand: public ListfileCommandBase
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

class ListfileCommandExecutor: public QObject
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
        // Returns false if not idle.
        bool setCommand(const CommandHolder &cmd);

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

class LIBMVME_EXPORT ReplayWidget: public QWidget
{
    Q_OBJECT
    signals:
        void start();
        void stop();
        void pause();
        void resume();
        void skip();

    public:
        ReplayWidget(QWidget *parent = nullptr);
        ~ReplayWidget() override;

        QString getBrowsePath() const;
        QVector<QUrl> getQueueContents() const;
        replay::CommandHolder getCommand() const;

    public slots:
        void browsePath(const QString &path);
        void clearFileInfoCache(); // TODO: get rid of this. was added for debugging.
        void setCurrentFilename(const QString &filename);

        // Communicate system state to the widget.
        void setRunning();
        void setIdle();
        void setPaused();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // _MVME_MVME_SRC_REPLAY_UI_H_
