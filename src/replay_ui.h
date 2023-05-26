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

enum ReplayCommandType
{
    Replay,
    Merge,
    Split,
    Filter,
};

struct ReplayCommandBase
{
    QVector<QUrl> queue;
};

struct ReplayCommand: public ReplayCommandBase
{
    const ReplayCommandType type = ReplayCommandType::Replay;
    QByteArray analysisBlob;
    QString analysisFilename; // For info purposes only. Data is kept in the blob.
};

struct MergeCommand: public ReplayCommandBase
{
    const ReplayCommandType type = ReplayCommandType::Merge;
    QString outputFilename;
};

struct SplitCommand: public ReplayCommandBase
{
    enum SplitCondition
    {
        Duration,
        CompressedSize,
        UncompressedSize
    };

    const ReplayCommandType type = ReplayCommandType::Split;
    SplitCondition condition;
    std::chrono::seconds splitInterval;
    size_t splitSize;
    QString outputBasename;
};

struct FilterCommand: public ReplayCommandBase
{
    const ReplayCommandType type = ReplayCommandType::Filter;
    QByteArray analysisBlob;
    QString analysisFilename; // For info purposes only. Data is kept in the blob.
    QUuid filterCondition; // Id of the analysis condition to use for filtering.
    QString outputFilename;
};

using CommandHolder = std::variant<ReplayCommand, MergeCommand, SplitCommand, FilterCommand>;

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

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // _MVME_MVME_SRC_REPLAY_UI_H_
