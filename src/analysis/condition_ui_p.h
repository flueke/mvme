#ifndef __CONDITION_UI_P_H__
#define __CONDITION_UI_P_H__

#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>

namespace analysis
{
namespace ui
{

class NodeModificationButtons: public QWidget
{
    Q_OBJECT
    signals:
        void accept();
        void reject();

    public:
        NodeModificationButtons(QWidget *parent = nullptr);

        QPushButton *getAcceptButton() const { return pb_accept; }
        QPushButton *getRejectButton() const { return pb_reject; }

        void setButtonsVisible(bool visible)
        {
            pb_accept->setVisible(visible);
            pb_reject->setVisible(visible);
        }

    private:
        QPushButton *pb_accept;
        QPushButton *pb_reject;
};

}
}

#endif /* __CONDITION_UI_P_H__ */
