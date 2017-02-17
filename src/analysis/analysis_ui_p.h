#ifndef __ANALYSIS_UI_P_H__
#define __ANALYSIS_UI_P_H__

#include "analysis.h"

#include <functional>

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QWidget>

class MVMEContext;

namespace analysis
{

class EventWidgetPrivate;

class EventWidget: public QWidget
{
    Q_OBJECT
    public:

        using SelectInputCallback = std::function<void ()>;

        EventWidget(MVMEContext *ctx, const QUuid &eventId, QWidget *parent = 0);
        ~EventWidget();

        void selectInputFor(Slot *slot, s32 userLevel, SelectInputCallback callback);
        void endSelectInput();
        void addOperator(OperatorPtr op, s32 userLevel);

    private:
        // Note: the EventWidgetPrivate part is not neccessary anymore as this
        // now already is inside a private header...
        EventWidgetPrivate *m_d;
};

class AddOperatorWidget: public QWidget
{
    Q_OBJECT

    public:
        AddOperatorWidget(OperatorPtr op, s32 userLevel, EventWidget *eventWidget);

        virtual void closeEvent(QCloseEvent *event) override;
        void inputSelected(s32 slotIndex);
        void accept();

        OperatorPtr m_op;
        s32 m_userLevel;
        EventWidget *m_eventWidget;
        QVector<QPushButton *> m_selectButtons;
        QDialogButtonBox *m_buttonBox = nullptr;
        bool m_inputSelectActive = false;
};

}

#endif /* __ANALYSIS_UI_P_H__ */
