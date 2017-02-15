#ifndef __DAQCONTROL_WIDGET_H__
#define __DAQCONTROL_WIDGET_H__

#include <QWidget>

namespace Ui
{
    class DAQControlWidget;
}

class MVMEContext;

class DAQControlWidget: public QWidget
{
    Q_OBJECT
    public:
        DAQControlWidget(MVMEContext *context, QWidget *parent = 0);
        ~DAQControlWidget();

    private:
        void updateWidget();

        Ui::DAQControlWidget *ui;
        MVMEContext *m_context;
};

#endif /* __DAQCONTROL_WIDGET_H__ */
