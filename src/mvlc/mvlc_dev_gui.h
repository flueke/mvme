#ifndef __MVLC_GUI_H__
#define __MVLC_GUI_H__

#include <functional>
#include <memory>
#include <QHash>
#include <QLineEdit>
#include <QMainWindow>
#include <QMutex>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>

#include "mvlc/mvlc_qt_object.h"
#include "vme_script.h"


struct FixedSizeBuffer
{
    std::unique_ptr<u8[]> data;
    size_t capacity;
    size_t used;
};

FixedSizeBuffer make_buffer(size_t capacity);

enum class FrameCheckResult: u8
{
    Ok,
    NeedMoreData,       // frame crosses buffer boundary
    HeaderMatchFailed,  // hit something else than F3
};

struct FrameCheckData
{
    size_t nextHeaderOffset;
    size_t framesChecked;
    size_t framesWithContinueFlag;
    std::array<size_t, mesytec::mvlc::stacks::StackCount> stackHits = {};
};

FrameCheckResult frame_check(const FixedSizeBuffer &buffer, FrameCheckData &data);

struct ReaderStats
{
    enum CounterEnum
    {
        TotalBytesReceived,
        NumberOfAttemptedReads,
        NumberOfTimeouts,
        NumberOfErrors,
        FramesSeen,
        FramesCrossingBuffers,
        FramesWithContinueFlag,

        CountersCount,
    };

    size_t counters[CountersCount];
    // Histogram of incoming read size -> number of reads
    QHash<size_t, size_t> readBufferSizes;
    std::array<size_t, mesytec::mvlc::stacks::StackCount> stackHits = {};
};

const char *reader_stat_name(ReaderStats::CounterEnum counter);

class MVLCDataReader: public QObject
{
    Q_OBJECT
    public:
        using MVLCObject = mesytec::mvlc::MVLCObject;
        static const int ReadBufferSize = Kilobytes(256);
        static const int ReadTimeout_ms = 250;

    signals:
        void started();
        void stopped();
        void bufferReady(const QVector<u8> &buffer);
        void message(const QString &msg);

    public:
        MVLCDataReader(QObject *parent = nullptr);
        virtual ~MVLCDataReader();

        // thread safe
        ReaderStats getStats() const;
        ReaderStats getAndResetStats();
        void resetStats();
        bool isStackFrameCheckEnabled() const;

        // not thread safe - must be done before entering readoutLoop
        void setMVLC(MVLCObject *mvlc);
        void setOutputDevice(std::unique_ptr<QIODevice> dev);
        void setStackFrameCheckEnabled(bool enable);

    public slots:
        // Runs until stop() is invoked from the outside. This is a blocking
        // call.
        void readoutLoop();

        // Thread safe, sets an atomic flag which makes readoutLoop() return.
        void stop();

        // Thread safe, sets atomic flag which makes readoutLoop() copy the
        // next buffer it receives and send it out via the bufferReady()
        // signal.
        void requestNextBuffer();

    private:
        MVLCObject *m_mvlc;
        std::atomic<bool> m_doQuit,
                          m_nextBufferRequested,
                          m_stackFrameCheckEnabled;
        FixedSizeBuffer m_readBuffer;
        mutable QMutex m_statsMutex;
        ReaderStats m_stats = {};
        std::unique_ptr<QIODevice> m_outDevice;
        FrameCheckData m_frameCheckData = {};
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
        void sigLogMessage(const QString &msg);

    public:
        using MVLCObject = mesytec::mvlc::MVLCObject;

        MVLCDevGUI(std::unique_ptr<MVLCObject> mvlc, QWidget *parent = 0);
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
        void sigLogBuffer(const QVector<u32> &buffer, const QString &info);

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

// Input and display widget for MVLC registers containing IPv4 addresses.
class IPv4RegisterWidget: public QWidget
{
    Q_OBJECT
    signals:
        void read(u16 reg);
        void write(u16 reg, u16 value);
        void sigLogMessage(const QString &str);

    public:
        IPv4RegisterWidget(u16 regLo, u16 regHi, const QString &regName = QString(),
                           QWidget *parent = nullptr);
        IPv4RegisterWidget(u16 regLo, const QString &regName = QString(),
                           QWidget *parent = nullptr);

    public slots:
        void setRegisterValue(u16 reg, u16 value);

    private:
        u16 m_regLo,
            m_regHi;
        QLineEdit *le_valLo,
                  *le_valHi,
                  *le_addressInput;
};

QString format_ipv4(u32 address);

#endif /* __MVLC_GUI_H__ */
