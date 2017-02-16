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

    private:
        EventWidgetPrivate *m_d;
};

class AddOperatorWidget: public QWidget
{
    Q_OBJECT

    public:
        AddOperatorWidget(OperatorPtr op, QWidget *parent = 0, Qt::WindowFlags f = Qt::WindowFlags());

        OperatorPtr m_op;
};

}

#endif /* __ANALYSIS_UI_P_H__ */
