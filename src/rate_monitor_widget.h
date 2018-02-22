#ifndef __RATE_MONITOR_WIDGET_H__
#define __RATE_MONITOR_WIDGET_H__

#include <QWidget>
#include "mvme_context.h"

struct RateMonitorWidgetPrivate;

class RateMonitorWidget: public QWidget
{
    Q_OBJECT
    public:
        RateMonitorWidget(MVMEContext *context, QWidget *parent = nullptr);
        ~RateMonitorWidget();

    public slots:
        /** Replots and updates the rate table. */
        void update();

    private slots:
        void sample();

    private:
        std::unique_ptr<RateMonitorWidgetPrivate> m_d;
};
#endif /* __RATE_MONITOR_WIDGET_H__ */
