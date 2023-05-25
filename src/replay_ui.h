#ifndef _MVME_SRC_REPLAY_UI_H_
#define _MVME_SRC_REPLAY_UI_H_

#include <QVector>
#include <QUuid>
#include <QWidget>

#include <memory>
#include <variant>

namespace mesytec::mvme
{

namespace replay
{

// Commands:
// replay   analysis
// merge    output filename + optional analysis to append
// split    split rules, e.g. timetick or event count based; output filename; produces MVLC_USB format
// filter   analysis and condition(s) to use for filtering. output filename; produces MVLC_USB format

struct ReplayCommandBase
{
    QVector<QUrl> queue;
};

struct ReplayCommand: public ReplayCommandBase
{
    // XXX: better store the analysisblob or directly pass a loaded analysis instance here?
    // FIXME: how to specify "load from listfile"? don't! instead pass the blob loaded from the listfile by default.
    QByteArray analysisBlob;
    QString analysisFilename;
};

struct MergeCommand: public ReplayCommandBase
{
    QString outputFilename;
};

struct SplitCommand: public ReplayCommandBase
{
    struct SplitRules
    {
        // time
        // size
    };

    QString analysisFilename;
    QString outputFilename;
    SplitRules rules;
};

struct FilterCommand: public ReplayCommandBase
{
    QString analysisFilename;
    QUuid filterCondition;
    QString outputFilename;
};

using CommandHolder = std::variant<ReplayCommand, MergeCommand, SplitCommand, FilterCommand>;

}

class ReplayWidget: public QWidget
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
