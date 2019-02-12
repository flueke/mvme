#ifndef __MVLC_GUI_H__
#define __MVLC_GUI_H__

#include <functional>
#include <memory>
#include <QMainWindow>
#include <QString>

#include "mvlc/mvlc_usb.h"
#include "vme_script.h"

enum class MVLC_USB_State
{
    Disconnected,
    Connecting,
    Connected,
};

Q_DECLARE_METATYPE(MVLC_USB_State);

class MVLCDevContext: public QObject
{
    Q_OBJECT
    signals:
        void mvlc_usb_state_changed(MVLC_USB_State state);

    public:
        MVLCDevContext();

    private:

};

class MVLCDevGUI: public QMainWindow
{
    Q_OBJECT
    public:
        MVLCDevGUI(QWidget *parent = 0);
        ~MVLCDevGUI();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

#endif /* __MVLC_GUI_H__ */
