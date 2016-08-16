#ifndef HIST2DDIALOG_H
#define HIST2DDIALOG_H

#include "hist2d.h"
#include <QDialog>

namespace Ui {
class Hist2DDialog;
}

class Hist2DDialog : public QDialog
{
    Q_OBJECT

public:
    explicit Hist2DDialog(QWidget *parent = 0);
    ~Hist2DDialog();

    Hist2D *getHist2D();

private:
    Ui::Hist2DDialog *ui;
};

#endif // HIST2DDIALOG_H
