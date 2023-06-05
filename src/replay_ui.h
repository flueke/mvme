#ifndef _MVME_SRC_REPLAY_UI_H_
#define _MVME_SRC_REPLAY_UI_H_

#include <QVector>
#include <QUuid>
#include <QWidget>

#include <memory>
#include <variant>

#include "libmvme_export.h"
#include "listfile_replay.h"

namespace mesytec::mvme
{

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
        void clearFileInfoCache(); // TODO: get rid of this. was added for debugging.
        void browsePath(const QString &path);
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
