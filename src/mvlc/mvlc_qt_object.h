#ifndef __MVLC_QT_OBJECT_H__
#define __MVLC_QT_OBJECT_H__

#include <QObject>
#include "mvlc/mvlc_usb.h"

namespace mesytec
{
namespace mvlc
{

class MVLCObject: public QObject
{
    Q_OBJECT
    public:
        enum State
        {
            Disconnected,
            Connecting,
            Connected,
        };

        using USB_Impl = mesytec::mvlc::usb::USB_Impl;
        using MVLCError = mesytec::mvlc::usb::MVLCError;

    signals:
        void stateChanged(const State &oldState, const State &newState);
        void errorSignal(const QString &msg, const MVLCError &error);

    public:
        MVLCObject(QObject *parent = nullptr);
        MVLCObject(const USB_Impl &impl, QObject *parent = nullptr);
        virtual ~MVLCObject();

        USB_Impl &getImpl() { return m_impl; }
        bool isConnected() const { return m_state == Connected; }

    public slots:
        void connect();
        void disconnect();

    private:
        void setState(const State &newState);

        mesytec::mvlc::usb::USB_Impl m_impl;
        State m_state = Disconnected;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_QT_OBJECT_H__ */
