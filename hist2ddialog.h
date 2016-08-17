#ifndef HIST2DDIALOG_H
#define HIST2DDIALOG_H

#include "hist2d.h"
#include <QDialog>

class MVMEContext;

namespace Ui {
class Hist2DDialog;
}

class Hist2DDialog : public QDialog
{
    Q_OBJECT

public:
    explicit Hist2DDialog(MVMEContext *context, QWidget *parent = 0);
    ~Hist2DDialog();

    Hist2D *getHist2D();

private:
    void onEventXChanged(int index);
    void onModuleXChanged(int index);
    void onChannelXChanged(int index);

    void onEventYChanged(int index);
    void onModuleYChanged(int index);
    void onChannelYChanged(int index);

    Ui::Hist2DDialog *ui;
    MVMEContext *m_context;
};

#endif // HIST2DDIALOG_H
