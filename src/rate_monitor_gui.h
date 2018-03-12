#ifndef __RATE_MONITOR_GUI_H__
#define __RATE_MONITOR_GUI_H__

#include <QWidget>
#include "mvme_context.h"

struct RateMonitorGuiPrivate;

class RateMonitorGui: public QWidget
{
    Q_OBJECT
    public:
        RateMonitorGui(MVMEContext *context, QWidget *parent = nullptr);
        ~RateMonitorGui();

    public slots:
        /** Replots and updates the rate table. */
        void update();

    private slots:
        void sample();

    private:
        std::unique_ptr<RateMonitorGuiPrivate> m_d;
};

#endif /* __RATE_MONITOR_GUI_H__ */
