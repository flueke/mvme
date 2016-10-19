#ifndef __DAQCONTROL_WIDGET_H__
#define __DAQCONTROL_WIDGET_H__

#include <QWidget>

class QPushButton;
class QLabel;
class QCheckBox;

class MVMEContext;

class DAQControlWidget: public QWidget
{
    Q_OBJECT
    public:
        DAQControlWidget(MVMEContext *context, QWidget *parent = 0);

    private:
        void updateWidget();

        MVMEContext *m_context;
        QPushButton *pb_start, *pb_stop, *pb_oneCycle, *pb_reconnect;
        QLabel *label_controller, *label_daqState, *label_listFileDir;
        QCheckBox *cb_writeListFile;
};

#endif /* __DAQCONTROL_WIDGET_H__ */
