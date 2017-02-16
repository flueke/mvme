#ifndef __ANALYSIS_UI_P_H__
#define __ANALYSIS_UI_P_H__

#include "analysis.h"

#include <functional>
#include <QWidget>

class QCloseEvent;
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

    private:
        EventWidgetPrivate *m_d;
};

class AddOperatorWidget: public QWidget
{
    Q_OBJECT

    public:
        AddOperatorWidget(OperatorPtr op, s32 userLevel, EventWidget *eventWidget);

        OperatorPtr m_op;
        s32 m_userLevel;
        EventWidget *m_eventWidget;

    protected:
        virtual void closeEvent(QCloseEvent *event) override;

    private:
        void inputSelected(s32 slotIndex);
};

}

#endif /* __ANALYSIS_UI_P_H__ */
