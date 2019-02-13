#ifndef __MVLC_GUI_H__
#define __MVLC_GUI_H__

#include <functional>
#include <memory>
#include <QMainWindow>
#include <QString>
#include <QMutex>

#include "mvlc/mvlc_usb.h"
#include "vme_script.h"

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

class MVLCDataReader: public QObject
{
    Q_OBJECT
    public:
        using USB_Impl = mesytec::mvlc::usb::USB_Impl;
        using MVLCError = mesytec::mvlc::usb::MVLCError;

        struct Stats
        {
            // TODO: enum for counters
            // array of counter variables
            size_t totalBytesReceived;
            size_t numberOfAttemptedReads;
            size_t numberOfTimeouts;
            size_t numberOfErrors;
        };

        static const int ReadBufferSize = Megabytes(1);
        static const int ReadTimeout_ms = 250;

    signals:
        void started();
        void stopped();

    public:
        MVLCDataReader(QObject *parent = nullptr);
        //MVLCDataReader(const USB_Impl &impl, QObject *parent = nullptr);
        virtual ~MVLCDataReader();

        // thread safe
        Stats getStats() const;

        // not thread safe
        void setImpl(const USB_Impl &impl);

    public slots:
        void readoutLoop();
        void stop();

    private:
        USB_Impl m_impl;
        std::atomic<bool> m_doQuit;
        QVector<u8> m_readBuffer;
        mutable QMutex m_statsMutex;
        Stats m_stats = {};
};

namespace Ui
{
    class MVLCDevGUI;
}

class MVLCDevGUI: public QMainWindow
{
    Q_OBJECT
    public:
        MVLCDevGUI(QWidget *parent = 0);
        ~MVLCDevGUI();

    public slots:
        void logMessage(const QString &msg);
        void logBuffer(const QVector<u32> &buffer, const QString &info);
        void clearLog();

    private:
        struct Private;
        friend Private;
        std::unique_ptr<Private> m_d;
        Ui::MVLCDevGUI *ui;
};

#endif /* __MVLC_GUI_H__ */
