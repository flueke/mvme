#ifndef VME_DEBUG_WIDGET_H
#define VME_DEBUG_WIDGET_H

#include <QWidget>
#include "util.h"

namespace Ui {
class VMEDebugWidget;
}

class MVMEContext;

class VMEDebugWidget : public QWidget
{
    Q_OBJECT

public:
    VMEDebugWidget(MVMEContext *context, QWidget *parent = 0);
    ~VMEDebugWidget();

private slots:

    void on_writeLoop1_toggled(bool checked);
    void on_writeLoop2_toggled(bool checked);
    void on_writeLoop3_toggled(bool checked);

    void on_writeWrite1_clicked();
    void on_writeWrite2_clicked();
    void on_writeWrite3_clicked();

    void on_readLoop1_toggled(bool checked);
    void on_readLoop2_toggled(bool checked);
    void on_readLoop3_toggled(bool checked);

    void on_readRead1_clicked();
    void on_readRead2_clicked();
    void on_readRead3_clicked();

    void on_runScript_clicked();
    void on_saveScript_clicked();
    void on_loadScript_clicked();

private:
    void doWrite(u32 address, u32 value);
    u16 doRead(u32 address);


    Ui::VMEDebugWidget *ui;
    MVMEContext *m_context;
};

#endif // VME_DEBUG_WIDGET_H
