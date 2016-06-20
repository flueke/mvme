#ifndef MVMECONTROL_H
#define MVMECONTROL_H

#include <QWidget>


class mvme;

namespace Ui {
class mvmeControl;
}

class mvmeControl : public QWidget
{
    Q_OBJECT

private:
    mvme * theApp;

public:
    explicit mvmeControl(mvme *theApp, QWidget *parent = 0);
    ~mvmeControl();
    void parseDataText(QString textt);
    void saveData();
    Ui::mvmeControl *ui;

    void dispAll();
    void dispDiag1();
    void dispDiag2();
    void dispResultlist();
    void dispRt();

public slots:
    void refreshDisplay(void);
    virtual void setValues();
    virtual void getValues();
    virtual void changeSource();
    virtual void changeLed(int led);
    virtual void changeBulkTransfer();
    virtual void changeNumber();
    virtual void changeMode();
    virtual void readLoopSlot();
    virtual void writeLoopSlot();
    virtual void readLoopSlot2();
    virtual void writeLoopSlot2();
    virtual void readLoopSlot3();
    virtual void writeLoopSlot3();
    virtual void readVme();
    virtual void writeVme();
    virtual void readVme2();
    virtual void writeVme2();
    virtual void readVme3();
    virtual void writeVme3();
    virtual void sendData();
    virtual void activateStack();
    virtual void loadData();
    virtual void loadStack();
    virtual void saveStack();
    virtual void readStack();
    virtual void setIrq();
    virtual void readBuffer();
    virtual void startStop(bool val);
    virtual void setScalerValue();
    virtual void toggleDisplay(bool state);
    virtual void dataTaking(bool val);
    virtual void clearData();
    virtual void selectListfile();
    virtual void changeEndianess();
    virtual void listSlot();
    virtual void replayListfile();
    virtual void writeData();
    virtual void checkSlot();
    virtual void calcSlot();
    virtual void clearSlot();
    void dispChan(int c);

private:
    unsigned char  counter;
    bool dontUpdate;
};




#endif // MVMECONTROL_H
