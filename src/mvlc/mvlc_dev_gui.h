/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __MVLC_GUI_H__
#define __MVLC_GUI_H__

#include <boost/circular_buffer.hpp>
#include <functional>
#include <memory>
#include <QHash>
#include <QLineEdit>
#include <QMainWindow>
#include <QMutex>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>

#include "libmvme_mvlc_export.h"
#include "mvlc/mvlc_qt_object.h"
#include "vme_script.h"

struct LIBMVME_EXPORT FixedSizeBuffer
{
    std::unique_ptr<u8[]> data;
    size_t capacity;
    size_t used;
    u8 *payloadBegin;

    FixedSizeBuffer() = default;
    explicit FixedSizeBuffer(size_t capacity_);
    FixedSizeBuffer(const FixedSizeBuffer &rhs);
    FixedSizeBuffer &operator=(const FixedSizeBuffer &rhs);
};

enum class FrameCheckResult: u8
{
    Ok,
    NeedMoreData,       // frame crosses buffer boundary
    HeaderMatchFailed,  // hit something else than F3
};

QString LIBMVME_EXPORT to_string(const FrameCheckResult &fcr);

struct LIBMVME_EXPORT FrameCheckData
{
    // Initial offset is 0, on error the last offset read by frame_check is
    // retained. If UDP is used nextHeaderOffset can be set by using the
    // nextHeaderPointer value found in the UDP header1 data word.
    size_t nextHeaderOffset;
    size_t buffersChecked;

    // stats. These should be somewhere else...
    size_t framesChecked;
    size_t framesWithContinueFlag;
    std::array<size_t, mesytec::mvlc::stacks::StackCount> stackHits = {};
};

FrameCheckResult LIBMVME_EXPORT frame_check(const FixedSizeBuffer &buffer, FrameCheckData &data);

struct LIBMVME_EXPORT ReaderStats
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

struct LIBMVME_EXPORT OwningPacketReadResult
{
    std::vector<u8> buffer;
    mesytec::mvlc::eth::PacketReadResult prr;

    OwningPacketReadResult(): buffer{}, prr{} {}
    explicit OwningPacketReadResult(const mesytec::mvlc::eth::PacketReadResult &prr);
    explicit OwningPacketReadResult(const OwningPacketReadResult &other);

    OwningPacketReadResult &operator=(const OwningPacketReadResult &other);
};

using EthDebugBuffer = boost::circular_buffer<OwningPacketReadResult>;
static const size_t EthDebugPacketCapacity = 5;

LIBMVME_EXPORT const char *reader_stat_name(ReaderStats::CounterEnum counter);

class LIBMVME_EXPORT LIBMVME_EXPORT MVLCDataReader: public QObject
{
    Q_OBJECT
    public:
        using MVLCObject = mesytec::mvme_mvlc::MVLCObject;
        //static const int USB3PacketSizeMax = 1024;
        static const int ReadBufferSize = Megabytes(1);
        //static const int ReadTimeout_ms = 50;

    signals:
        void started();
        void stopped();
        void bufferReady(const QVector<u8> &buffer);
        void message(const QString &msg);
        void frameCheckFailed(const FrameCheckData &fcd, const FixedSizeBuffer &buffer);
        void ethDebugSignal(const EthDebugBuffer &data, const QString &reason);

    public:
        MVLCDataReader(QObject *parent = nullptr);
        virtual ~MVLCDataReader();

        // thread safe
        ReaderStats getStats() const;
        ReaderStats getAndResetStats();
        void resetStats();
        bool isStackFrameCheckEnabled() const;
        void setLogAllBuffers(bool b) { m_logAllBuffers = b; }
        void setReadBufferSize(size_t size) { m_requestedReadBufferSize = size; }
        void enableEthHeaderDebug();
        void disableEthHeaderDebug();
        bool isEthHeaderDebugEnabled() const;

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
                          m_stackFrameCheckEnabled,
                          m_logAllBuffers,
                          m_ethDebugEnabled;
        std::atomic<size_t> m_requestedReadBufferSize;
        FixedSizeBuffer m_readBuffer;
        mutable QMutex m_statsMutex,
                       m_ethDebugMutex;
        ReaderStats m_stats = {};
        std::unique_ptr<QIODevice> m_outDevice;
        FrameCheckData m_frameCheckData = {};

        enum EthDebugState
        {
            EthNoError,
            EthErrorSeen
        };

        EthDebugState m_ethDebugState = EthNoError;
        EthDebugBuffer m_ethDebugBuffer;
        QString m_ethDebugReason;
        u32 m_ethPacketsToCollect = 0;

};

namespace Ui
{
    class MVLCDevGUI;
}

class LIBMVME_EXPORT MVLCDevGUI: public QMainWindow
{
    Q_OBJECT
    signals:
        // private signal used to enter the readout loop in the readout thread
        void sigLogMessage(const QString &msg);

    public:
        using MVLCObject = mesytec::mvme_mvlc::MVLCObject;

        MVLCDevGUI(MVLCObject *mvlc, QWidget *parent = nullptr);
        ~MVLCDevGUI();

    public slots:
        void logMessage(const QString &msg);
        void logBuffer(const std::vector<u32> &buffer, const QString &info);

    private slots:
        void handleEthDebugSignal(const EthDebugBuffer &debugBuffer, const QString &reason);

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
        void stackErrorNotification(const std::vector<u32> &notification);

    public:
        MVLCRegisterWidget(mesytec::mvme_mvlc::MVLCObject *mvlc, QWidget *parent = nullptr);
        ~MVLCRegisterWidget();

    private:
        mesytec::mvme_mvlc::MVLCObject *m_mvlc;

        void writeRegister(u16 address, u32 value);
        u32 readRegister(u16 address);
        void readStackInfo(u8 stackId);
};

class LIBMVME_EXPORT LogWidget: public QWidget
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
