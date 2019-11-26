#ifndef __CONDITION_UI_P_H__
#define __CONDITION_UI_P_H__

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>

#include "analysis.h"

class MVMEContext;

namespace analysis
{
namespace ui
{

class ConditionTreeWidget: public QTreeWidget
{
    Q_OBJECT
    signals:
        void applyConditionAccept();
        void applyConditionReject();
        void editConditionGraphically(const ConditionLink &cond);
        void editConditionInEditor(const ConditionLink &cond);

    public:
        ConditionTreeWidget(MVMEContext *ctx, const QUuid &eventId, int eventIndex,
                            QWidget *parent = nullptr);
        virtual ~ConditionTreeWidget() override;

        void repopulate();
        void doPeriodicUpdate();

        void highlightConditionLink(const ConditionLink &cl);
        void clearHighlights();
        void setModificationButtonsVisible(const ConditionLink &cl, bool visible);

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

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

class ConditionIntervalEditor: public QDialog
{
    Q_OBJECT
    signals:
        void applied();

    public:
        ConditionIntervalEditor(ConditionInterval *cond, MVMEContext *context,
                                QWidget *parent = nullptr);
        virtual ~ConditionIntervalEditor();

        virtual void accept();
        virtual void reject();

    private:
        ConditionInterval *m_cond;
        MVMEContext *m_context;

        QLineEdit *le_name;
        QTableWidget *m_table = nullptr;
        QDialogButtonBox *m_bb = nullptr;
};

} // end namespace ui
} // end namespace analysis

#endif /* __CONDITION_UI_P_H__ */
