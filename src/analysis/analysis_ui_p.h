#ifndef __ANALYSIS_UI_P_H__
#define __ANALYSIS_UI_P_H__

#include "analysis.h"

#include <QWidget>

class MVMEContext;

namespace analysis
{

class EventWidgetPrivate;

class EventWidget: public QWidget
{
    Q_OBJECT
    public:
        EventWidget(MVMEContext *ctx, const QUuid &eventId, QWidget *parent = 0);
        ~EventWidget();

        void selectInputFor(Slot *slot, s32 userLevel);

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
};

}

#endif /* __ANALYSIS_UI_P_H__ */
