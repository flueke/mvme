#ifndef __MVLC_GUI_H__
#define __MVLC_GUI_H__

#include <functional>
#include <memory>
#include <QMainWindow>
#include <QString>
#include <QMutex>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>

#include "mvlc/mvlc_qt_object.h"
#include "vme_script.h"


struct FixedSizeBuffer
{
    std::unique_ptr<u8[]> data;
    size_t capacity;
    size_t used;
};

FixedSizeBuffer make_buffer(size_t capacity);

struct ReaderStats
{
    enum CounterEnum
    {
        TotalBytesReceived,
        NumberOfAttemptedReads,
        NumberOfTimeouts,
        NumberOfErrors,

        CountersCount,
    };

    size_t counters[CountersCount];
};

const char *reader_stat_name(ReaderStats::CounterEnum counter);

class MVLCDataReader: public QObject
{
    Q_OBJECT
    public:
        using USB_Impl = mesytec::mvlc::usb::USB_Impl;
        using MVLCError = mesytec::mvlc::usb::MVLCError;


        static const int ReadBufferSize = Megabytes(1);
        static const int ReadTimeout_ms = 250;

    signals:
        void started();
        void stopped();
        void bufferReady(const QVector<u8> &buffer);
        void message(const QString &msg);

    public:
        MVLCDataReader(QObject *parent = nullptr);
        //MVLCDataReader(const USB_Impl &impl, QObject *parent = nullptr);
        virtual ~MVLCDataReader();

        // thread safe
        ReaderStats getStats() const;
        ReaderStats getAndResetStats();
        void resetStats();

        // not thread safe
        void setImpl(const USB_Impl &impl);
        void setOutputDevice(std::unique_ptr<QIODevice> dev);

    public slots:
        // Runs until stop() is invoked from the outside.
        void readoutLoop();
        // Thread safe, sets an atomic flag which makes readoutLoop() return.
        void stop();

        // Thread safe, sets atomic flag which makes readoutLoop() copy the
        // next buffer it receives and send it out via the bufferReady()
        // signal.
        void requestNextBuffer();

    private:
        USB_Impl m_impl;
        std::atomic<bool> m_doQuit,
                          m_nextBufferRequested;
        FixedSizeBuffer m_readBuffer;
        mutable QMutex m_statsMutex;
        ReaderStats m_stats = {};
        std::unique_ptr<QIODevice> m_outDevice;
};

namespace Ui
{
    class MVLCDevGUI;
}

class MVLCDevGUI: public QMainWindow
{
    Q_OBJECT
    signals:
        // private signal used to enter the readout loop in the readout thread
        void enterReadoutLoop();
        void sigLogMessage(const QString &msg);

    public:
        MVLCDevGUI(QWidget *parent = 0);
        ~MVLCDevGUI();

    public slots:
        void logMessage(const QString &msg);
        void logBuffer(const QVector<u32> &buffer, const QString &info);

    private:
        struct Private;
        friend Private;
        std::unique_ptr<Private> m_d;
        Ui::MVLCDevGUI *ui;
};

class MVLCRegisterWidget: public QWidget
{
    Q_OBJECT
    signals:
        void sigLogMessage(const QString &str);

    public:
        MVLCRegisterWidget(mesytec::mvlc::MVLCObject *mvlc, QWidget *parent = nullptr);
        ~MVLCRegisterWidget();

    private:
        mesytec::mvlc::MVLCObject *m_mvlc;

        void writeRegister(u16 address, u32 value);
        u32 readRegister(u16 address);
        void readStackInfo(u8 stackId);
};

class LogWidget: public QWidget
{
    Q_OBJECT
    public:
        LogWidget(QWidget *parent = nullptr);
        virtual ~LogWidget();

    public slots:
        void logMessage(const QString &msg);
        void clearLog();

    private:
        QPlainTextEdit *te_log;
        QPushButton *pb_clearLog;
};

#endif /* __MVLC_GUI_H__ */
