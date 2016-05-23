#ifndef TWODIMWIDGET_H
#define TWODIMWIDGET_H

#include <QWidget>

class TwoDimDisp;
class ScrollZoomer;

namespace Ui {
class TwoDimWidget;
}

class TwoDimWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit TwoDimWidget(QWidget *parent = 0);
    ~TwoDimWidget();
    Ui::TwoDimWidget *ui;
    void setZoombase();
    ScrollZoomer* m_myZoomer;
    TwoDimDisp* m_pMyDisp;

public slots:
    void displaychanged(void);
    void clearHist(void);


};

#endif // TWODIMWIDGET_H
