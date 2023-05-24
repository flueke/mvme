#ifndef _MVME_SRC_REPLAY_UI_H_
#define _MVME_SRC_REPLAY_UI_H_

#include <memory>
#include <QWidget>

namespace mesytec::mvme
{

class ReplayWidget: public QWidget
{
    Q_OBJECT
    public:
        ReplayWidget(QWidget *parent = nullptr);
        ~ReplayWidget() override;

    public slots:
        void browsePath(const QString &path);
        QString getBrowsePath() const;
        std::vector<QUrl> getQueueContents() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // _MVME_MVME_SRC_REPLAY_UI_H_