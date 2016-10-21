#ifndef __DAQSTATS_WIDGET_H__
#define __DAQSTATS_WIDGET_H__

#include <QWidget>

class MVMEContext;
class DAQStatsWidgetPrivate;

class DAQStatsWidget: public QWidget
{
    Q_OBJECT
    public:
        DAQStatsWidget(MVMEContext *context, QWidget *parent = 0);
        ~DAQStatsWidget();

    private:
        void updateWidget();

        DAQStatsWidgetPrivate *m_d;
};

#endif /* __DAQSTATS_WIDGET_H__ */
