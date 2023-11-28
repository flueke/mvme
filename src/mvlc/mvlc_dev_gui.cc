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
#include "mvlc/mvlc_dev_gui.h"

#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QGridLayout>
#include <QHostAddress>
#include <QMessageBox>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>
#include <QTableWidget>

#include <iostream>
#include <cmath>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>

#include "mesytec-mvlc/mvlc_stack_errors.h"
#include "ui_mvlc_dev_ui.h"

#include "mvlc/mvlc_script.h"
#include "mvlc/mvlc_vme_debug_widget.h"
#include "mvlc/mvlc_util.h"
#include "qt_util.h"
#include "util/counters.h"
#include "util/qt_font.h"
#include "util/strings.h"
#include "vme_script_util.h"

using namespace mesytec;
using namespace mesytec::mvme_mvlc;
using namespace mesytec::mvlc::usb;

FixedSizeBuffer::FixedSizeBuffer(size_t capacity_)
    : data(std::make_unique<u8[]>(capacity_))
    , capacity(capacity_)
    , used(0)
    , payloadBegin(data.get())
{
}

FixedSizeBuffer::FixedSizeBuffer(const FixedSizeBuffer &rhs)
{
    data = std::make_unique<u8[]>(rhs.capacity);
    capacity = rhs.capacity;
    std::copy(rhs.data.get(), rhs.data.get() + rhs.used, data.get());
    used = rhs.used;
    auto offset = rhs.payloadBegin - rhs.data.get();
    payloadBegin = data.get() + offset;
}

FixedSizeBuffer &FixedSizeBuffer::operator=(const FixedSizeBuffer &rhs)
{
    data = std::make_unique<u8[]>(rhs.capacity);
    capacity = rhs.capacity;
    std::copy(rhs.data.get(), rhs.data.get() + rhs.used, data.get());
    used = rhs.used;
    auto offset = rhs.payloadBegin - rhs.data.get();
    payloadBegin = data.get() + offset;

    return *this;
}

QVector<u8> copy_data_to_qvector(const FixedSizeBuffer &buffer)
{
    QVector<u8> result;
    result.reserve(buffer.used);
    std::copy(buffer.data.get(),
              buffer.data.get() + buffer.used,
              std::back_inserter(result));
    return result;
}

const char *reader_stat_name(ReaderStats::CounterEnum counter)
{
    switch (counter)
    {
        case ReaderStats::TotalBytesReceived:
            return "TotalBytesReceived";
        case ReaderStats::NumberOfAttemptedReads:
            return "NumberOfAttemptedReads";
        case ReaderStats::NumberOfTimeouts:
            return "NumberOfTimeouts";
        case ReaderStats::NumberOfErrors:
            return "NumberOfErrors";
        case ReaderStats::FramesSeen:
            return "FramesSeen";
        case ReaderStats::FramesCrossingBuffers:
            return "FramesCrossingBuffers";
        case ReaderStats::FramesWithContinueFlag:
            return "FramesWithContinueFlag";

        case ReaderStats::CountersCount:
            return "INVALID COUNTER";
    }

    return "UNKNOWN COUNTER";
}

static const QString Key_LastMVLCScriptDirectory = "Files/LastMVLCScriptDirectory";
static const QString Key_LastMVLCDataOutputDirectory = "Files/LastMVLCDataOutputDirectory";
static const QString DefaultOutputFilename = "mvlc_dev_data.bin";

OwningPacketReadResult::OwningPacketReadResult(const mesytec::mvlc::eth::PacketReadResult &input)
{
    buffer.reserve(input.bytesTransferred);
    std::copy(input.buffer, input.buffer + input.bytesTransferred,
              std::back_inserter(buffer));
    prr = input;
    prr.buffer = buffer.data(); // pointer adjustment
}

OwningPacketReadResult::OwningPacketReadResult(const OwningPacketReadResult &other)
{
    buffer = other.buffer;
    prr = other.prr;
    prr.buffer = buffer.data(); // pointer adjustment
}

OwningPacketReadResult &OwningPacketReadResult::operator=(const OwningPacketReadResult &other)
{
    buffer = other.buffer;
    prr = other.prr;
    prr.buffer = buffer.data(); // pointer adjustment
    return *this;
}

//
// MVLCDataReader
//
MVLCDataReader::MVLCDataReader(QObject *parent)
    : QObject(parent)
    , m_doQuit(false)
    , m_nextBufferRequested(false)
    , m_stackFrameCheckEnabled(true)
    , m_logAllBuffers(false)
    , m_ethDebugEnabled(true)
    , m_requestedReadBufferSize(ReadBufferSize)
    , m_readBuffer{}
    , m_ethDebugBuffer(EthDebugPacketCapacity)
{
}

MVLCDataReader::~MVLCDataReader()
{
}

ReaderStats MVLCDataReader::getStats() const
{
    ReaderStats result;
    {
        QMutexLocker guard(&m_statsMutex);
        result = m_stats;
    }
    return result;
}

ReaderStats MVLCDataReader::getAndResetStats()
{
    ReaderStats result;
    {
        QMutexLocker guard(&m_statsMutex);
        result = m_stats;
        m_stats = {};
    }
    return result;
}

void MVLCDataReader::resetStats()
{
    QMutexLocker guard(&m_statsMutex);
    m_stats = {};
}

bool MVLCDataReader::isStackFrameCheckEnabled() const
{
    return m_stackFrameCheckEnabled;
}

void MVLCDataReader::enableEthHeaderDebug()
{
    QMutexLocker guard(&m_ethDebugMutex);
    m_ethDebugEnabled = true;
    m_ethDebugBuffer.clear();
    assert(m_ethDebugBuffer.capacity() == EthDebugPacketCapacity);
    assert(m_ethDebugBuffer.size() == 0);
}

void MVLCDataReader::disableEthHeaderDebug()
{
    QMutexLocker guard(&m_ethDebugMutex);
    m_ethDebugEnabled = false;
    m_ethDebugBuffer.clear();
    assert(m_ethDebugBuffer.capacity() == EthDebugPacketCapacity);
    assert(m_ethDebugBuffer.size() == 0);
}

bool MVLCDataReader::isEthHeaderDebugEnabled() const
{
    QMutexLocker guard(&m_ethDebugMutex);
    return m_ethDebugEnabled;
}

void MVLCDataReader::setMVLC(MVLCObject *mvlc)
{
    m_mvlc = mvlc;
}

void MVLCDataReader::setOutputDevice(std::unique_ptr<QIODevice> dev)
{
    m_outDevice = std::move(dev);
}

QString to_string(const FrameCheckResult &fcr)
{
    switch (fcr)
    {
        case FrameCheckResult::Ok:
            return "OK";
        case  FrameCheckResult::NeedMoreData:
            return "NeedMoreData";
        case FrameCheckResult::HeaderMatchFailed:
            return "HeaderMatchFailed";
    }

    InvalidCodePath;
    return "invalid FrameCheckResult";
}

FrameCheckResult frame_check(const FixedSizeBuffer &buffer, FrameCheckData &data)
{
    const u32 *buffp = reinterpret_cast<const u32 *>(buffer.payloadBegin);
    const u32 *endp  = reinterpret_cast<const u32 *>(buffer.data.get() + buffer.used);
    size_t loopIteration = 0u;

    while (true)
    {
        const u32 *nextp = buffp + data.nextHeaderOffset;

        // Wrap to the next buffer
        if (nextp >= endp)
        {
            data.nextHeaderOffset = nextp - endp;

            //qDebug("%s -> NeedMoreData", __FUNCTION__);

            return FrameCheckResult::NeedMoreData;
        }

        const u32 header = *nextp;

        if (!(mvlc::is_stack_buffer(header) || mvlc::is_stack_buffer_continuation(header)))
        {
            qDebug("%s: loopIteration=%lu, header=0x%08x, is_stack_buffer=%d,"
                   " is_stack_buffer_continuation=%d => HeaderMatchFailed",
                   __FUNCTION__, loopIteration,
                   header, mvlc::is_stack_buffer(header),
                   mvlc::is_stack_buffer_continuation(header));

            qDebug("%s: buffp=%p, nextHeaderOffset=%lu",
                   __FUNCTION__,
                   buffp, data.nextHeaderOffset);

            // leave nextHeaderOffset unmodified for inspection
            return FrameCheckResult::HeaderMatchFailed;
        }

        const auto hdrInfo = mvlc::extract_frame_info(header);

        if (hdrInfo.stack < mvlc::stacks::StackCount)
            ++data.stackHits[hdrInfo.stack];

        if (hdrInfo.flags & mvlc::frame_flags::Continue)
            ++data.framesWithContinueFlag;

        ++data.framesChecked;
        data.nextHeaderOffset += 1 + hdrInfo.len;
        ++loopIteration;
    }

    return {};
}

void MVLCDataReader::readoutLoop()
{
    m_doQuit = false;
    resetStats();
    m_frameCheckData = {};
    m_stackFrameCheckEnabled = true;

    emit started();

    //m_mvlc->setReadTimeout(Pipe::Data, ReadTimeout_ms);

    qDebug() << __PRETTY_FUNCTION__ << "entering readout loop";
    qDebug() << __PRETTY_FUNCTION__ << "executing in" << QThread::currentThread();

    mvlc::eth::Impl *mvlc_eth = nullptr;
    mvlc::usb::Impl *mvlc_usb = nullptr;

    switch (m_mvlc->connectionType())
    {
        case mvlc::ConnectionType::ETH:
            {
                mvlc_eth = reinterpret_cast<mvlc::eth::Impl *>(m_mvlc->getImpl());

                emit message(QSL("Connection type is UDP. Sending initial empty request"
                                 " using the data socket."));

                size_t bytesTransferred = 0;

                static const std::array<u32, 2> EmptyRequest =
                {
                    0xF1000000,
                    0xF2000000
                };

                if (auto ec = mvlc_eth->write(
                        mvlc::Pipe::Data,
                        reinterpret_cast<const u8 *>(EmptyRequest.data()),
                        EmptyRequest.size() * sizeof(u32),
                        bytesTransferred))
                {
                    emit message(QSL("Error sending initial empty request using the data socket: %1")
                                 .arg(ec.message().c_str()));
                    emit stopped();
                    return;
                }
            } break;

        case mvlc::ConnectionType::USB:
            {
                mvlc_usb = reinterpret_cast<mvlc::usb::Impl *>(m_mvlc->getImpl());
            } break;
    }

    assert(mvlc_eth || mvlc_usb);

    auto prevFrameCheckResult = FrameCheckResult::Ok;

    while (!m_doQuit)
    {
        size_t requestedReadBufferSize = m_requestedReadBufferSize;

        if (m_readBuffer.capacity != requestedReadBufferSize)
        {
            m_readBuffer = FixedSizeBuffer(requestedReadBufferSize);
            emit message(QSL("New read buffer size: %1 bytes").arg(m_readBuffer.capacity));
        }

        size_t bytesTransferred = 0u;
        std::error_code ec;
        mvlc::eth::PacketReadResult eth_rr = {};

        if (mvlc_eth)
        {
            assert(!mvlc_usb);

            // Manual locking. Might be better to make read_packet() available
            // in a higher layer?
            auto guard = m_mvlc->getLocks().lockData();
            eth_rr = mvlc_eth->read_packet(mvlc::Pipe::Data, m_readBuffer.data.get(), m_readBuffer.capacity);
            ec = eth_rr.ec;
            bytesTransferred = eth_rr.bytesTransferred;
            m_readBuffer.payloadBegin = m_readBuffer.data.get() + mvlc::eth::HeaderBytes;

            // ethernet buffer debugging
            {
                QMutexLocker ethDebugGuard(&m_ethDebugMutex);
                if (m_ethDebugEnabled && eth_rr.hasHeaders())
                {
                    if (m_ethDebugState == EthNoError)
                    {
                        // store all packets in the circular buffer
                        m_ethDebugBuffer.push_back(OwningPacketReadResult(eth_rr));

                        // check header pointer validity, range and type of pointed to data word
                        if (eth_rr.nextHeaderPointer() != mvlc::eth::header1::NoHeaderPointerPresent)
                        {
                            bool isInvalid = false;

                            if (eth_rr.payloadBegin() + eth_rr.nextHeaderPointer() >= eth_rr.payloadEnd())
                            {
                                isInvalid = true;
                                m_ethDebugReason = "nextHeaderPointer out of range";
                            }
                            else
                            {
                                u32 stackFrameHeader = *(eth_rr.payloadBegin() + eth_rr.nextHeaderPointer());
                                if (!(mvlc::is_stack_buffer(stackFrameHeader)
                                      || mvlc::is_stack_buffer_continuation(stackFrameHeader)))
                                {
                                    isInvalid = true;
                                    m_ethDebugReason = "nextHeaderPointer does not point to a stack header (F3 or F9)";
                                }
                            }

                            if (isInvalid)
                            {
                                m_ethDebugState = EthErrorSeen;
                                m_ethPacketsToCollect = m_ethDebugBuffer.capacity() / 2;
                                assert(m_ethPacketsToCollect > 0);
                            }
                        }
                    }
                    else if (m_ethDebugState == EthErrorSeen)
                    {
                        // store additional packets received after the error has happened
                        m_ethDebugBuffer.push_back(OwningPacketReadResult(eth_rr));

                        if (--m_ethPacketsToCollect == 0)
                        {
                            m_ethDebugState = EthNoError;
                            m_ethDebugEnabled = false;
                            emit ethDebugSignal(m_ethDebugBuffer, m_ethDebugReason);
                        }
                    }
                }
            }
        }
        else if (mvlc_usb)
        {
            assert(!mvlc_eth);

            auto guard = m_mvlc->getLocks().lockData();
            ec = mvlc_usb->read_unbuffered(mvlc::Pipe::Data,
                                           m_readBuffer.data.get(),
                                           m_readBuffer.capacity,
                                           bytesTransferred);
        }

        m_readBuffer.used = bytesTransferred;


        if (ec == mvlc::ErrorType::ConnectionError)
        {
            emit message(QSL("Lost connection to MVLC. Leaving readout loop. Reason: %1")
                         .arg(ec.message().c_str()));
            break;
        }
        else if (ec && ec != mvlc::ErrorType::Timeout)
        {
            emit message(QSL("Other error from read: %1, %2, %3")
                         .arg(ec.message().c_str())
                         .arg(ec.category().name())
                         .arg(ec.value()));
            break;
        }

        // stats
        {
            QMutexLocker guard(&m_statsMutex);

            ++m_stats.counters[ReaderStats::NumberOfAttemptedReads];
            m_stats.counters[ReaderStats::TotalBytesReceived] += bytesTransferred;
            if (bytesTransferred > 0)
                ++m_stats.readBufferSizes[bytesTransferred];

            if (ec)
            {
                if (ec == mvlc::ErrorType::Timeout)
                    ++m_stats.counters[ReaderStats::NumberOfTimeouts];
                else
                    ++m_stats.counters[ReaderStats::NumberOfErrors];
            }
        }

        // Stack frame checks (F3, F9)
        if (m_readBuffer.used > 0 && m_stackFrameCheckEnabled)
        {
            auto checkResult = frame_check(m_readBuffer, m_frameCheckData);
            QMutexLocker guard(&m_statsMutex);
            m_stats.counters[ReaderStats::FramesSeen] = m_frameCheckData.framesChecked;
            m_stats.counters[ReaderStats::FramesWithContinueFlag] =
                m_frameCheckData.framesWithContinueFlag;
            m_stats.stackHits = m_frameCheckData.stackHits;

            if (checkResult == FrameCheckResult::HeaderMatchFailed
                && mvlc_eth && eth_rr.hasHeaders())
            {
                // This is the mechanism allowing to correctly resume data
                // processing in case of packet loss without having to rely
                // on searching and magic words. The 2nd UDP header word
                // contains an offset to the next stack frame header inside
                // the packet. The offset is
                // header1::NoHeaderPointerPresent if there is no header
                // present in the packet data.

                if (!eth_rr.lostPackets)
                {
                    emit message(QSL("Warning: frame check failed without prior ETH packet loss!"));
                    emit frameCheckFailed(m_frameCheckData, m_readBuffer);
                }

                emit message(QSL("Adjusting FrameCheckData.nextHeaderOffset using UDP frame info and rechecking."));
                m_frameCheckData.nextHeaderOffset = eth_rr.nextHeaderPointer();
                checkResult = frame_check(m_readBuffer, m_frameCheckData);

                emit message(QSL("Result of recheck: %1 (%2)")
                             .arg(static_cast<int>(checkResult))
                             .arg(to_string(checkResult))
                             );
            }

            ++m_frameCheckData.buffersChecked;

            if (checkResult == FrameCheckResult::HeaderMatchFailed)
            {
                m_stackFrameCheckEnabled = false;

                emit message(QSL("!!! !!! !!!"));
                emit message(QSL("Frame Check header match failed! Disabling frame check."));

                emit message(QSL("  prevFrameCheckResult=%1 (%2)")
                             .arg(static_cast<int>(prevFrameCheckResult))
                             .arg(to_string(prevFrameCheckResult))
                            );

                emit message(QSL("  nextHeaderOffset=%1").arg(m_frameCheckData.nextHeaderOffset));

                u32 nextHeader = *reinterpret_cast<u32 *>(m_readBuffer.payloadBegin)
                    + m_frameCheckData.nextHeaderOffset;

                emit message(QSL("  nextHeader=0x%1")
                             .arg(nextHeader, 8, 16, QLatin1Char('0')));
                emit message(QSL("!!! !!! !!!"));

                emit frameCheckFailed(m_frameCheckData, m_readBuffer);
            }
            else if (checkResult == FrameCheckResult::NeedMoreData)
            {
                ++m_stats.counters[ReaderStats::FramesCrossingBuffers];
            }

            prevFrameCheckResult = checkResult;
        }

        if ((m_nextBufferRequested || m_logAllBuffers) && m_readBuffer.used > 0)
        {
            emit bufferReady(copy_data_to_qvector(m_readBuffer));

            m_nextBufferRequested = false;
        }

        if (m_readBuffer.used > 0 && m_outDevice)
        {
            m_outDevice->write(reinterpret_cast<const char *>(m_readBuffer.data.get()),
                               m_readBuffer.used);
        }
    }

    qDebug() << __PRETTY_FUNCTION__ << "left readout loop";

    m_outDevice = {};

    qDebug() << __PRETTY_FUNCTION__ << "emitting stopped() signal";
    emit stopped();
}

void MVLCDataReader::stop()
{
    m_doQuit = true;
}

void MVLCDataReader::requestNextBuffer()
{
    m_nextBufferRequested = true;
}

//
// MVLCDevGUI
//
struct MVLCDevGUI::Private
{
    MVLCDevGUI *q;

    // Widgets
    QWidget *centralWidget;
    QToolBar *toolbar;
    //QStatusBar *statusbar;
    MVLCRegisterWidget *registerWidget;
    VMEDebugWidget *vmeDebugWidget;

    //QAction *act_showLog,
    //        *act_showVMEDebug,
    //        *act_loadScript
    //        ;

    MVLCObject *mvlc;
    QThread readoutThread;
    MVLCDataReader *dataReader;

    // DataReader stats
    QVector<QLabel *> readerStatLabels;

    QLabel *l_statRunDuration,
           *l_statReadRate,
           *l_statFrameRate;

    QPushButton *pb_printReaderBufferSizes,
                *pb_printStackHits,
                *pb_enableEthDebug;

    QDateTime tReaderStarted,
              tReaderStopped,
              tLastReaderStatUpdate,
              tLastStackNotificationUpdate;

    ReaderStats prevReaderStats = {};

    mvlc::StackErrorCounters curStackErrors,
                             prevStackErrors;
};

MVLCDevGUI::MVLCDevGUI(MVLCObject *mvlc, QWidget *parent)
    : QMainWindow(parent)
    , m_d(std::make_unique<Private>())
    , ui(new Ui::MVLCDevGUI)
{
    assert(m_d->dataReader == nullptr);

    m_d->q = this;
    m_d->mvlc = mvlc;
    m_d->registerWidget = new MVLCRegisterWidget(m_d->mvlc, this);
    m_d->vmeDebugWidget = new VMEDebugWidget(m_d->mvlc, this);

    auto updateTimer = new QTimer(this);
    updateTimer->setInterval(1000);

    setObjectName(QSL("MVLC Dev GUI"));
    setWindowTitle(objectName());

    m_d->toolbar = new QToolBar(this);
    //m_d->statusbar = new QStatusBar(this);
    m_d->centralWidget = new QWidget(this);
    ui->setupUi(m_d->centralWidget);

    setCentralWidget(m_d->centralWidget);
    addToolBar(m_d->toolbar);
    //setStatusBar(m_d->statusbar);

    // MVLC Script Editor
    {
        auto font = make_monospace_font();
        font.setPointSize(8);
        ui->te_scriptInput->setFont(font);
        ui->te_udpScriptInput->setFont(font);
    }

    new vme_script::SyntaxHighlighter(ui->te_scriptInput->document());
    static const int SpacesPerTab = 4;
    set_tabstop_width(ui->te_scriptInput, SpacesPerTab);
    set_tabstop_width(ui->te_udpScriptInput, SpacesPerTab);

    // Reader stats ui setup
    {
        auto l = new QFormLayout(ui->gb_readerStats);
        l->setSizeConstraint(QLayout::SetMinimumSize);

        for (int counterType = 0;
             counterType < ReaderStats::CountersCount;
             counterType++)
        {
            auto name = reader_stat_name(
                static_cast<ReaderStats::CounterEnum>(counterType));
            auto label = new QLabel();
            m_d->readerStatLabels.push_back(label);
            l->addRow(name, label);
        }

        m_d->l_statRunDuration = new QLabel();
        l->addRow("Run Duration", m_d->l_statRunDuration);

        m_d->l_statReadRate = new QLabel();
        l->addRow("Read Rate", m_d->l_statReadRate);

        m_d->l_statFrameRate = new QLabel();
        l->addRow("Frame Rate", m_d->l_statFrameRate);

        m_d->pb_printReaderBufferSizes = new QPushButton("Print Incoming Buffer Sizes");
        m_d->pb_printStackHits = new QPushButton("Print Stack Hits");
        {
            auto bl = make_layout<QHBoxLayout, 0, 0>();
            bl->addWidget(m_d->pb_printReaderBufferSizes);
            bl->addWidget(m_d->pb_printStackHits);
            bl->addStretch();
            l->addRow(bl);
        }
    }

    // UDP receive stats table
    ui->gb_udpStats->hide();
    if (m_d->mvlc->connectionType() == mvlc::ConnectionType::ETH)
    {
        ui->gb_udpStats->show();

        //
        // UDP pipe stats table
        //
        auto tbl = new QTableWidget(this);

        static const QStringList colTitles = {
            "Cmd(0)", "Data(1)"
        };


        tbl->setColumnCount(colTitles.size());
        tbl->setHorizontalHeaderLabels(colTitles);

        auto update_stats_table = [this, tbl]()
        {
            auto guard = m_d->mvlc->getLocks().lockBoth();
            auto udp_impl = reinterpret_cast<mvlc::eth::Impl *>(m_d->mvlc->getImpl());

            static auto lastPipeStats = udp_impl->getPipeStats();

            static QDateTime lastUpdateTime;
            QDateTime now = QDateTime::currentDateTime();

            if (!lastUpdateTime.isValid())
            {
                lastUpdateTime = now;
                return;
            }

            QStringList rowTitles = {
                "rcvdPackets", "packets/s",
                "shortPackets",
                "receivedBytes", "bytesPerSecond",
                "noHeader", "headerOutOfRange"
            };

            double secondsElapsed = lastUpdateTime.msecsTo(now) / 1000.0;
            auto pipeStats = udp_impl->getPipeStats();

            for (unsigned ht = 0; ht < 256; ht++)
            {
                if (pipeStats[0].headerTypes[ht] || pipeStats[1].headerTypes[ht])
                {
                    rowTitles << QString("headerType 0x%1").arg(ht, 2, 16, QLatin1Char('0'));
                }
            }

            tbl->setRowCount(rowTitles.size());
            tbl->setVerticalHeaderLabels(rowTitles);
            int firstHeaderTypeRow = 0u;
            using QTWI = QTableWidgetItem;

            for (unsigned pipe = 0; pipe < pipeStats.size(); pipe++)
            {
                auto &lastStats = lastPipeStats[pipe];
                auto &stats = pipeStats[pipe];
                int row = 0;

                s64 deltaPackets = stats.receivedPackets - lastStats.receivedPackets;
                double packetsPerSecond = deltaPackets / secondsElapsed;
                s64 deltaBytes = stats.receivedBytes - lastStats.receivedBytes;
                double bytesPerSecond = deltaBytes / secondsElapsed;

                tbl->setItem(row++, pipe, new QTWI(QSL("%1").arg(stats.receivedPackets)));
                tbl->setItem(row++, pipe, new QTWI(QSL("%1").arg(packetsPerSecond)));
                tbl->setItem(row++, pipe, new QTWI(QSL("%1").arg(stats.shortPackets)));
                tbl->setItem(row++, pipe, new QTWI(QSL("%1").arg(stats.receivedBytes)));
                tbl->setItem(row++, pipe, new QTWI(QSL("%1").arg(bytesPerSecond)));
                tbl->setItem(row++, pipe, new QTWI(QSL("%1").arg(stats.noHeader)));
                tbl->setItem(row++, pipe, new QTWI(QSL("%1").arg(stats.headerOutOfRange)));

                firstHeaderTypeRow = row;
            }

            for (unsigned ht = 0, row = firstHeaderTypeRow; ht < 256; ht++)
            {
                if (pipeStats[0].headerTypes[ht] || pipeStats[1].headerTypes[ht])
                {
                    for (unsigned pipe = 0; pipe < pipeStats.size(); pipe++)
                    {
                        tbl->setItem(row, pipe, new QTWI(
                                QString::number(pipeStats[pipe].headerTypes[ht])));
                    }
                    row++;
                }
            }

            tbl->resizeColumnsToContents();
            tbl->resizeRowsToContents();

            lastPipeStats = pipeStats;
            lastUpdateTime = now;
        };

        connect(updateTimer, &QTimer::timeout, this, update_stats_table);

        //
        // UDP packet channel loss counters
        //
        QStringList channelNames = { "Command", "Stack", "Data" };
        std::array<QLabel *, mvlc::eth::NumPacketChannels> lossLabels;
        auto l_packetLoss = new QFormLayout();
        l_packetLoss->addRow(new QLabel("Packet loss counters"));
        for (u8 chan = 0; chan < mvlc::eth::NumPacketChannels; chan++)
        {
            lossLabels[chan] = new QLabel(this);
            l_packetLoss->addRow(channelNames[chan], lossLabels[chan]);
        }

        auto update_loss_labels = [this, lossLabels] ()
        {
            auto guard = m_d->mvlc->getLocks().lockBoth();
            auto udp_impl = reinterpret_cast<mvlc::eth::Impl *>(m_d->mvlc->getImpl());
            auto channelStats = udp_impl->getPacketChannelStats();

            for (size_t chan = 0; chan < channelStats.size(); chan++)
            {
                auto label = lossLabels[chan];
                label->setText(QString::number(channelStats[chan].lostPackets));
            }
        };

        connect(updateTimer, &QTimer::timeout, this, update_loss_labels);

#if 0
        auto debug_print_packet_sizes = [this] ()
        {
            auto guard = m_d->mvlc->getLocks().lockBoth();
            auto udp_impl = reinterpret_cast<eth::Impl *>(m_d->mvlc->getImpl());
            auto channelStats = udp_impl->getPacketChannelStats();

            for (size_t chan = 0; chan < channelStats.size(); chan++)
            {
                const auto &pktSizes = channelStats[chan].packetSizes;

                if (pktSizes.empty())
                    continue;

                std::vector<u16> sizeVec;
                sizeVec.reserve(pktSizes.size());

                for (const auto &kv: pktSizes)
                    sizeVec.push_back(kv.first);

                std::sort(sizeVec.begin(), sizeVec.end());

                qDebug("Incoming packet sizes for packet channel %lu:", chan);

                for (u16 pktSize: sizeVec)
                {
                    qDebug("  sz=%4u, packets=%lu", pktSize, pktSizes.at(pktSize));
                }
            }
        };

        connect(updateTimer, &QTimer::timeout, this, debug_print_packet_sizes);
#endif

        m_d->pb_enableEthDebug = new QPushButton("Enable Eth Header Debug");
        m_d->pb_enableEthDebug->setCheckable(true);
        m_d->pb_enableEthDebug->setChecked(true);

        connect(m_d->pb_enableEthDebug, &QPushButton::toggled,
                this, [this] (bool b)
        {
            if (b)
                m_d->dataReader->enableEthHeaderDebug();
            else
                m_d->dataReader->disableEthHeaderDebug();
        });

        connect(updateTimer, &QTimer::timeout, this, [this] ()
        {
            bool b = m_d->dataReader->isEthHeaderDebugEnabled();
            QSignalBlocker blocker(m_d->pb_enableEthDebug);
            m_d->pb_enableEthDebug->setChecked(b);
        });

        auto udpStatsLayout = new QGridLayout(ui->gb_udpStats);
        udpStatsLayout->addWidget(tbl, 0, 0, 2, 1);
        udpStatsLayout->addLayout(l_packetLoss, 0, 1, 1, 1);
        udpStatsLayout->addWidget(m_d->pb_enableEthDebug, 1, 1, 1, 1);
    }

    // Interactions

    // mvlc connection state changes
    auto on_mvlc_state_changed = [this] (const MVLCObject::State &,
                                         const MVLCObject::State &newState)
    {
        switch (newState)
        {
            case MVLCObject::Disconnected:
                ui->le_connectionStatus->setText("Disconnected");
                break;
            case MVLCObject::Connecting:
                ui->le_connectionStatus->setText("Connecting...");
                break;
            case MVLCObject::Connected:
                ui->le_connectionStatus->setText("Connected");
                break;
        }

        if (newState == MVLCObject::Connected)
        {
            QString msg("Connected to MVLC");

            switch (m_d->mvlc->connectionType())
            {
                case mvlc::ConnectionType::USB:
                    {
                        auto mvlc_usb = reinterpret_cast<mvlc::usb::Impl *>(m_d->mvlc->getImpl());
                        auto devInfo = mvlc_usb->getDeviceInfo();

                        const char *speedstr = ((devInfo.flags & mvlc::usb::DeviceInfo::Flags::USB2)
                                                ? "USB2" : "USB3");

                        msg += QString(" (speed=%1, serial=%2)")
                            .arg(speedstr)
                            .arg(devInfo.serial.c_str());

                    } break;
                case mvlc::ConnectionType::ETH:
                    {
                        auto mvlc_eth = reinterpret_cast<mvlc::eth::Impl *>(m_d->mvlc->getImpl());

                        msg += QString (" (address=%1)")
                            .arg(QHostAddress(mvlc_eth->getCmdAddress()).toString());
                    } break;
            }

            logMessage(msg);
        }

        ui->pb_runScript->setEnabled(newState == MVLCObject::Connected);
        ui->pb_runScript->setEnabled(false); // FIXME
        ui->pb_reconnect->setEnabled(newState != MVLCObject::Connecting);
    };

    connect(m_d->mvlc, &MVLCObject::stateChanged,
            this, on_mvlc_state_changed);

    on_mvlc_state_changed({}, m_d->mvlc->getState());

    // count stack error notifications published by the mvlc object
    connect(ui->pb_runScript, &QPushButton::clicked,
            this, [this] ()
    {
        // FIXME
#if 0
        try
        {
            bool logRequest = ui->cb_scriptLogRequest->isChecked();
            bool logMirror  = ui->cb_scriptLogMirror->isChecked();

            auto scriptText = ui->te_scriptInput->toPlainText();
            auto cmdList = mvme_mvlc::script::parse(scriptText);
            auto cmdBuffer = mvme_mvlc::script::to_mvlc_command_buffer(cmdList);

            if (logRequest)
            {
                logBuffer(cmdBuffer, "Outgoing Request Buffer");
            }

            std::vector<u32> responseBuffer;

            if (auto ec = m_d->mvlc->mirrorTransaction(cmdBuffer, responseBuffer))
            {
                logMessage(QString("Error performing MVLC mirror transaction: %1 (%2)")
                           .arg(ec.message().c_str())
                           .arg(ec.value())
                           );

                if (!logRequest)
                {
                    // In case of a mirror check error do log the request
                    // buffer but only if it has not been logged yet.
                    logBuffer(cmdBuffer, "Outgoing Request Buffer");
                }
                logBuffer(responseBuffer, "Incoming erroneous Mirror Buffer");
                return;
            }

            if (logMirror)
            {
                logBuffer(responseBuffer, "Incoming Mirror Buffer");
            }

            // Log a short message after any buffers have been logged.
            logMessage(QString("Sent %1 words, received %2 words, mirror check ok.")
                       .arg(cmdBuffer.size())
                       .arg(responseBuffer.size()));

            if (ui->cb_scriptReadStack->isChecked())
            {
                logMessage("Attempting to read stack response...");

                auto ec = m_d->mvlc->readResponse(mvlc::is_stack_buffer, responseBuffer);

                if (ec && ec != mvlc::ErrorType::Timeout)
                {
                    logMessage(QString("Error reading from MVLC: %1")
                               .arg(ec.message().c_str()));
                    return;
                }
                else if (responseBuffer.empty())
                {
                    logMessage("Did not receive a stack response from MVLC");
                    return;
                }

                if (ec == mvlc::ErrorType::Timeout)
                    logMessage("Received response but ran into a read timeout");

                logBuffer(responseBuffer, "Stack response from MVLC");

                // Same as is done in MVLCDialog::stackTransaction(): if error
                // bits are set, read in the error notification (0xF7) buffer
                // and log it.
                u32 header = responseBuffer[0];
                u8 errorBits = ((header >> mvlc::frame_headers::FrameFlagsShift)
                                & mvlc::frame_headers::FrameFlagsMask);

                if (errorBits)
                {
                    std::vector<u32> tmpBuffer;
                    m_d->mvlc->readKnownBuffer(tmpBuffer);
                    if (!tmpBuffer.empty())
                    {
                        u32 header = tmpBuffer[0];

                        if (mvlc::is_stackerror_notification(header))
                        {
                            logBuffer(tmpBuffer, "Stack error notification from MVLC");
                            handleStackErrorNotification(tmpBuffer);
                        }
                        else
                        {
                            logBuffer(tmpBuffer, "Unexpected buffer contents (wanted a stack error notification (0xF7)");
                        }
                    }
                }
            }

#if 0
            for (const auto &notification: m_d->mvlc->getStackErrorNotifications())
            {
                this->handleStackErrorNotification(notification);
            }
#endif
        }
        catch (const mvme_mvlc::script::ParseError &e)
        {
            logMessage("MVLC Script parse error: " + e.toString());
        }
        catch (const vme_script::ParseError &e)
        {
            logMessage("Embedded VME Script parse error: " + e.toString());
        }
#endif
    });

    connect(ui->pb_loadScript, &QPushButton::clicked,
            this, [this] ()
    {
        QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
        QSettings settings;
        if (settings.contains(Key_LastMVLCScriptDirectory))
        {
            path = settings.value(Key_LastMVLCScriptDirectory).toString();
        }

        QString fileName = QFileDialog::getOpenFileName(
            this, QSL("Load MVLC script file"), path,
            QSL("MVLC scripts (*.mvlcscript);; All Files (*)"));

        if (!fileName.isEmpty())
        {
            QFile file(fileName);
            if (file.open(QIODevice::ReadOnly))
            {
                QTextStream stream(&file);
                ui->te_scriptInput->setPlainText(stream.readAll());
                QFileInfo fi(fileName);
                settings.setValue(Key_LastMVLCScriptDirectory, fi.absolutePath());
            }
        }
    });

    connect(ui->pb_saveScript, &QPushButton::clicked,
            this, [this] ()
    {
        QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
        QSettings settings;
        if (settings.contains(Key_LastMVLCScriptDirectory))
        {
            path = settings.value(Key_LastMVLCScriptDirectory).toString();
        }

        QString fileName = QFileDialog::getSaveFileName(
            this, QSL("Save MVLC script"), path,
            QSL("MVLC scripts (*.mvlcscript);; All Files (*)"));

        if (fileName.isEmpty())
            return;

        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly))
        {
            QMessageBox::critical(this, "File error",
                                  QString("Error opening \"%1\" for writing").arg(fileName));
            return;
        }

        QTextStream stream(&file);
        stream << ui->te_scriptInput->toPlainText();

        if (stream.status() != QTextStream::Ok)
        {
            QMessageBox::critical(this, "File error",
                                  QString("Error writing to \"%1\"").arg(fileName));
            return;
        }

        settings.setValue(Key_LastMVLCScriptDirectory, QFileInfo(fileName).absolutePath());
    });

    connect(ui->pb_clearScript, &QPushButton::clicked,
            this, [this] ()
    {
        ui->te_scriptInput->clear();
    });

    connect(ui->pb_reconnect, &QPushButton::clicked,
            this, [this] ()
    {
        if (m_d->mvlc->isConnected())
        {
            if (auto ec = m_d->mvlc->disconnect())
            {
                logMessage(QString("Error from disconnect(): %1")
                           .arg(ec.message().c_str()));
            }
        }

        if (auto ec = m_d->mvlc->connect())
        {
            logMessage(QString("Error connecting to MVLC: %1")
                       .arg(ec.message().c_str()));
        }
    });


    // FIXME
    connect(ui->pb_readCmdPipe, &QPushButton::clicked,
            this, [this] ()
    {
#if 0
        static const int ManualCmdRead_WordCount = 1024;
        std::vector<u32> readBuffer;
        readBuffer.resize(ManualCmdRead_WordCount);
        size_t bytesTransferred;

        auto ec = m_d->mvlc->read(
            mvlc::Pipe::Command,
            reinterpret_cast<u8 *>(readBuffer.data()),
            readBuffer.size() * sizeof(u32),
            bytesTransferred);

        // IMPORTANT: This silently discards any superfluous bytes.
        readBuffer.resize(bytesTransferred / sizeof(u32));

        if (!readBuffer.empty())
            logBuffer(readBuffer, "Results of manual read from Command Pipe");

        if (ec)
            logMessage(QString("Read error: %1").arg(ec.message().c_str()));
#endif
    });


    // FIXME
    connect(ui->pb_readDataPipe, &QPushButton::clicked,
            this, [this] ()
    {
#if 0
        static const int ManualDataRead_WordCount = 8192;
        std::vector<u32> readBuffer;
        readBuffer.resize(ManualDataRead_WordCount);
        size_t bytesTransferred;

        auto ec = m_d->mvlc->read(
            mvlc::Pipe::Data,
            reinterpret_cast<u8 *>(readBuffer.data()),
            readBuffer.size() * sizeof(u32),
            bytesTransferred);

        // IMPORTANT: This silently discards any superfluous bytes.
        readBuffer.resize(bytesTransferred / sizeof(u32));

        if (!readBuffer.empty())
            logBuffer(readBuffer, "Results of manual read from Data Pipe");

        if (ec)
            logMessage(QString("Read error: %1").arg(ec.message().c_str()));
#endif
    });

    //
    // MVLCDataReader and readout thread
    //

    m_d->readoutThread.setObjectName("MVLC Readout");
    m_d->dataReader = new MVLCDataReader();
    m_d->dataReader->setMVLC(m_d->mvlc);
    m_d->dataReader->moveToThread(&m_d->readoutThread);

    connect(&m_d->readoutThread, &QThread::started,
            m_d->dataReader, &MVLCDataReader::readoutLoop);

    connect(m_d->dataReader, &MVLCDataReader::stopped,
            &m_d->readoutThread, &QThread::quit);

    connect(ui->pb_readerStart, &QPushButton::clicked,
            this, [this] ()
    {
        assert(!m_d->readoutThread.isRunning());

        logMessage("Starting readout");

        if (ui->gb_dataOutputFile->isChecked())
        {
            QString outputFilePath = ui->le_dataOutputFilePath->text();

            if (outputFilePath.isEmpty())
            {
                logMessage("Data Reader Error: output filename is empty");
            }
            else
            {
                std::unique_ptr<QIODevice> outFile = std::make_unique<QFile>(outputFilePath);

                if (!outFile->open(QIODevice::WriteOnly))
                {
                    logMessage(QString("Error opening output file '%1' for writing: %2")
                               .arg(outputFilePath)
                               .arg(outFile->errorString()));
                }
                else
                {
                    logMessage(QString("Writing incoming data to file '%1'.")
                               .arg(outputFilePath));

                    m_d->dataReader->setOutputDevice(std::move(outFile));
                }
            }
        }

        m_d->readoutThread.start();
    });

    // Populate initial output filepath using a previously saved path if
    // available
    {
        QString outDir;
        QSettings settings;

        if (settings.contains(Key_LastMVLCDataOutputDirectory))
        {
            outDir = settings.value(Key_LastMVLCDataOutputDirectory).toString();
        }
        else
        {
            outDir = QStandardPaths::standardLocations(
                QStandardPaths::DocumentsLocation).at(0);
        }

        ui->le_dataOutputFilePath->setText(outDir + "/" + DefaultOutputFilename);
    }

    connect(ui->pb_readerStop, &QPushButton::clicked,
            this, [this] ()
    {
        assert(m_d->readoutThread.isRunning());

        logMessage("Stopping readout");
        // Sets the atomic flag to make the reader break out of the loop.
        m_d->dataReader->stop();
    });

    connect(m_d->dataReader, &MVLCDataReader::started,
            this, [this] ()
    {
        qDebug() << "readout thread started";
        ui->pb_readerStart->setEnabled(false);
        ui->pb_readerStop->setEnabled(true);
        ui->le_readoutStatus->setText("Running");
        ui->pb_reconnect->setEnabled(false);
        ui->pb_readDataPipe->setEnabled(false);

        m_d->tReaderStarted = QDateTime::currentDateTime();
        m_d->tReaderStopped = {};
    });

    connect(&m_d->readoutThread, &QThread::finished,
            this, [this] ()
    {
        qDebug() << "readout thread finished";
        ui->pb_readerStart->setEnabled(true);
        ui->pb_readerStop->setEnabled(false);
        ui->le_readoutStatus->setText("Stopped");
        ui->pb_reconnect->setEnabled(true);
        ui->pb_readDataPipe->setEnabled(true);
        m_d->tReaderStopped = QDateTime::currentDateTime();
    });

    ui->pb_readerStop->setEnabled(false);

    // Reset Reader Stats
    connect(ui->pb_readerResetStats, &QPushButton::clicked,
            this, [this] ()
    {
        auto now = QDateTime::currentDateTime();
        m_d->tReaderStarted = now;
        m_d->tReaderStopped = {};
        m_d->tLastReaderStatUpdate    = now;
        m_d->prevReaderStats = {};
        m_d->dataReader->resetStats();
    });

    // Request that the reader copies and sends out the next buffer it receives.
    connect(ui->pb_readerRequestBuffer, &QPushButton::clicked,
            this, [this] ()
    {
        m_d->dataReader->requestNextBuffer();
    });

    connect(ui->cb_readerLogAll, &QCheckBox::toggled,
            this, [this] (bool b)
    {
        m_d->dataReader->setLogAllBuffers(b);
    });

    connect(ui->pb_readerApplyReadBufferSize, &QPushButton::clicked,
            this, [this] ()
    {
        m_d->dataReader->setReadBufferSize(static_cast<size_t>(
                ui->spin_readerReadBufferSize->value()));
    });

    connect(m_d->dataReader, &MVLCDataReader::bufferReady,
            this, [this] (QVector<u8> buffer)
    {
        logMessage(QString("Received data buffer containing %1 words (%2 bytes).")
                   .arg(buffer.size() / sizeof(u32))
                   .arg(buffer.size()));

        int maxWords = ui->spin_logReaderBufferMaxWords->value();
        int maxBytes = maxWords > 0 ? maxWords * sizeof(u32) : buffer.size();
        maxBytes = std::min(maxBytes, buffer.size());

        logMessage(QString(">>> First %1 data words:").arg(maxBytes / sizeof(u32)));

        BufferIterator iter(buffer.data(), maxBytes);
        ::logBuffer(iter, [this] (const QString &line)
        {
            logMessage(line);
        });

        logMessage(QString("<<< End of buffer log"));
    });

    connect(m_d->dataReader, &MVLCDataReader::frameCheckFailed,
            this, [this] (const FrameCheckData &fcd, const FixedSizeBuffer &buffer)
    {
        size_t payloadOffsetBytes = buffer.payloadBegin - buffer.data.get();
        size_t payloadSizeBytes   = buffer.used - payloadOffsetBytes;

        logMessage(QSL("FrameCheckFailed: buffer size=%1 bytes, payloadSize=%2 bytes buffersChecked=%3")
                   .arg(buffer.used)
                   .arg(payloadSizeBytes)
                   .arg(fcd.buffersChecked)
                   );

        size_t wordCount = buffer.used / sizeof(u32);
        const u32 *startp = reinterpret_cast<const u32 *>(buffer.payloadBegin);
        const u32 *endp   = startp + wordCount;

        const u32 *nextHeader = reinterpret_cast<u32 *>(buffer.payloadBegin) + fcd.nextHeaderOffset;
        const int HalfWindow = 512;
        // limit the window to be logged to the actual buffer size
        const u32 *firstToLog = std::max(reinterpret_cast<const u32 *>(nextHeader - HalfWindow), startp);
        const u32 *lastToLog  = std::min(nextHeader + HalfWindow, endp);

        const u32 *buffp = firstToLog;

        QString strbuf;

        logMessage(QSL("starting logging at word offset %1").arg(buffp - startp));
        logMessage(QSL("logging %1 words").arg(lastToLog - buffp));

        while (buffp < lastToLog)
        {
            strbuf.clear();
            u32 nWords = std::min((ptrdiff_t)(lastToLog - buffp), (ptrdiff_t)8);

            for (u32 i=0; i<nWords; i++)
            {
                if (buffp == nextHeader)
                {
                    strbuf += QString("**%1 ").arg(*buffp, 8, 16, QLatin1Char('0'));
                }
                else
                {
                    strbuf += QString("0x%1 ").arg(*buffp, 8, 16, QLatin1Char('0'));
                }

                ++buffp;
            }

            logMessage(strbuf);
        }

        logMessage("end of frame check log");
    });

    connect(m_d->dataReader, &MVLCDataReader::ethDebugSignal,
            this, &MVLCDevGUI::handleEthDebugSignal);

    connect(m_d->dataReader, &MVLCDataReader::message,
            this, [this] (const QString &msg)
    {
        logMessage("Readout Thread: " + msg);
    });

    connect(ui->pb_browseOutputFile, &QPushButton::clicked,
            this, [this] ()
    {
        QString startDir;
        QSettings settings;

        if (settings.contains(Key_LastMVLCDataOutputDirectory))
        {
            startDir = settings.value(Key_LastMVLCDataOutputDirectory).toString();
        }
        else
        {
            startDir = QStandardPaths::standardLocations(
                QStandardPaths::DocumentsLocation).at(0);
        }

        QString filePath = QFileDialog::getSaveFileName(
            this,                                       // parent
            "Select Data Reader Output File",           // caption
            startDir,                                   // dir
            QString(),                                  // filter
            nullptr,                                    // selectedFilter,
            QFileDialog::Options());                    // options

        qDebug() << __PRETTY_FUNCTION__ << filePath;

        if (!filePath.isEmpty())
        {
            ui->le_dataOutputFilePath->setText(filePath);
            QFileInfo fi(filePath);
            QSettings settings;
            settings.setValue(Key_LastMVLCDataOutputDirectory, fi.path());
        }
    });

    connect(m_d->pb_printReaderBufferSizes, &QPushButton::clicked,
            this, [this] ()
    {
        const auto &sizeHash = m_d->prevReaderStats.readBufferSizes;

        if (sizeHash.isEmpty())
        {
            logMessage("Reader did not receive any buffers yet.");
            return;
        }


        auto sizes = sizeHash.keys();
        std::sort(sizes.begin(), sizes.end());

        QStringList lines;
        lines.reserve(sizeHash.size() + 4);

        lines << ">>> Reader receive buffer sizes:";
        lines << "  size (Bytes) | count";
        lines << "  ------------------------";

        for (size_t size: sizes)
        {
            size_t count = sizeHash[size];

            lines << QString("  %1   | %2")
                .arg(size, 10)
                .arg(count);
        }

        lines << "<<< End receive buffer sizes";

        logMessage(lines.join("\n"));
    });

    connect(m_d->pb_printStackHits, &QPushButton::clicked,
            this, [this] ()
    {
        const auto &hits = m_d->prevReaderStats.stackHits;

        bool didPrint = false;

        for (size_t stackId = 0; stackId < hits.size(); stackId++)
        {
            if (hits[stackId])
            {
                logMessage(QSL("stackId=%1, hits=%2")
                           .arg(stackId)
                           .arg(hits[stackId]));
                didPrint = true;
            }
        }

        if (!didPrint)
            logMessage("No stack hits recorded");
    });

    //
    // UDP Debug Tab Interactions
    //
    connect(ui->pb_udpSend, &QPushButton::clicked,
            this, [this] ()
    {
        try
        {
            auto scriptText = ui->te_udpScriptInput->toPlainText();
            auto cmdList = mvme_mvlc::script::parse(scriptText);
            auto cmdBuffer = mvme_mvlc::script::to_mvlc_command_buffer(cmdList);

            //for (u32 &word: cmdBuffer)
            //{
            //    word = qToBigEndian(word);
            //}


            logBuffer(cmdBuffer, "Outgoing Request Buffer");

            QHostAddress destIP(ui->le_udpDestIP->text());
            quint16 destPort = ui->spin_udpDestPort->value();

            static const qint64 MaxPacketPayloadSize = 1480;

            qint64 bytesLeft = cmdBuffer.size() * sizeof(u32);
            const char *dataPtr = reinterpret_cast<char *>(cmdBuffer.data());
            QUdpSocket sock;
            size_t packetsSent = 0;

            while (bytesLeft > 0)
            {
                qint64 bytesToWrite = std::min(bytesLeft, MaxPacketPayloadSize);
                qint64 bytesWritten = sock.writeDatagram(dataPtr, bytesToWrite, destIP, destPort);

                if (bytesWritten < 0)
                {
                    logMessage(QSL("Error from writeDatagram: %1").arg(sock.errorString()));
                    return;
                }

                bytesLeft -= bytesWritten;
                dataPtr += bytesWritten;
                packetsSent++;
            }

            logMessage(QSL("Sent command buffer using %1 UDP packets").arg(packetsSent));
        }
        catch (const mvme_mvlc::script::ParseError &e)
        {
            logMessage("MVLC Script parse error: " + e.toString());
        }
        catch (const vme_script::ParseError &e)
        {
            logMessage("Embedded VME Script parse error: " + e.toString());
        }
    });

    //
    // Register Editor Tab
    //
    {
        auto layout = qobject_cast<QGridLayout *>(ui->tab_mvlcRegisters->layout());
        layout->addWidget(m_d->registerWidget);

        connect(m_d->registerWidget, &MVLCRegisterWidget::sigLogMessage,
                this, &MVLCDevGUI::logMessage);
    }

    //
    // VME Debug Widget Tab
    //
    {
        auto layout = qobject_cast<QGridLayout *>(ui->tab_vmeDebug->layout());
        layout->addWidget(m_d->vmeDebugWidget);

        connect(m_d->vmeDebugWidget, &VMEDebugWidget::sigLogMessage,
                this, &MVLCDevGUI::logMessage);
    }

    //
    // Periodic updates
    //

    // Pull ReaderStats from MVLCDataReader, calculate deltas and rates and
    // update the stats display.
    connect(updateTimer, &QTimer::timeout,
            this, [this] ()
    {
        auto stats = m_d->dataReader->getStats();

        for (int counterType = 0;
             counterType < ReaderStats::CountersCount;
             counterType++)
        {
            QString text;
            size_t value = stats.counters[counterType];

            if (counterType == ReaderStats::TotalBytesReceived)
            {
                text = (QString("%1 B, %2 MB")
                        .arg(value)
                        .arg(value / (double)Megabytes(1)));
            }
            else
            {
                text = QString::number(stats.counters[counterType]);
            }

            m_d->readerStatLabels[counterType]->setText(text);
        }

        auto endTime = (m_d->readoutThread.isRunning()
                        ?  QDateTime::currentDateTime()
                        : m_d->tReaderStopped);

        s64 secondsElapsed = m_d->tReaderStarted.msecsTo(endTime) / 1000.0;
        auto durationString = makeDurationString(secondsElapsed);

        m_d->l_statRunDuration->setText(durationString);

        ReaderStats &prevStats = m_d->prevReaderStats;

        double dt = (m_d->tLastReaderStatUpdate.isValid()
                     ? m_d->tLastReaderStatUpdate.msecsTo(endTime)
                     : m_d->tReaderStarted.msecsTo(endTime)) / 1000.0;

        u64 deltaBytesRead = calc_delta0(
            stats.counters[ReaderStats::TotalBytesReceived],
            prevStats.counters[ReaderStats::TotalBytesReceived]);

        double bytesPerSecond = deltaBytesRead / dt;
        double mbPerSecond = bytesPerSecond / Megabytes(1);
        if (std::isnan(mbPerSecond))
            mbPerSecond = 0.0;

        u64 deltaFramesSeen = calc_delta0(
            stats.counters[ReaderStats::FramesSeen],
            prevStats.counters[ReaderStats::FramesSeen]);
        double framesPerSecond = deltaFramesSeen / dt;
        if (std::isnan(framesPerSecond))
            framesPerSecond = 0.0;

        u64 deltaReads = calc_delta0(
            stats.counters[ReaderStats::NumberOfAttemptedReads],
            prevStats.counters[ReaderStats::NumberOfAttemptedReads]);
        double readsPerSecond = deltaReads / dt;
        if (std::isnan(readsPerSecond))
            readsPerSecond = 0.0;

        m_d->l_statReadRate->setText(QString("%1 MB/s, %2 reads/s")
                                     .arg(mbPerSecond, 0, 'g', 4)
                                     .arg(readsPerSecond, 0, 'g', 4));


        m_d->l_statFrameRate->setText(QString("%1 Frames/s, frameCheckEnabled=%2")
                                     .arg(framesPerSecond, 0, 'g', 4)
                                     .arg(m_d->dataReader->isStackFrameCheckEnabled()));

        m_d->prevReaderStats = stats;
        m_d->tLastReaderStatUpdate = QDateTime::currentDateTime();
    });

#if 0
    // Poll the read queue size for both pipes
    connect(updateTimer, &QTimer::timeout,
            this, [this] ()
    {
        u32 cmdQueueSize = 0;
        u32 dataQueueSize = 0;

        auto tStart = std::chrono::high_resolution_clock::now();

        m_d->mvlc->getReadQueueSize(Pipe::Command, cmdQueueSize);
        auto tCmd = std::chrono::high_resolution_clock::now();

        m_d->mvlc->getReadQueueSize(Pipe::Data, dataQueueSize);
        auto tData = std::chrono::high_resolution_clock::now();

        auto dtCmd = std::chrono::duration_cast<std::chrono::milliseconds>(tCmd - tStart);
        auto dtData = std::chrono::duration_cast<std::chrono::milliseconds>(tData - tStart);

        ui->le_usbCmdReadQueueSize->setText(QString::number(cmdQueueSize));
        ui->le_usbDataReadQueueSize->setText(QString::number(dataQueueSize));

        ui->label_queueSizePollTime->setText(
            QString("Cmd: %1ms, Data: %2ms, now=%3")
            .arg(dtCmd.count())
            .arg(dtData.count())
            .arg(QTime::currentTime().toString())
            );
    });
#endif

    // update stack error notification counts and rates
    connect(updateTimer, &QTimer::timeout,
            this, [this] ()
    {
        auto now = QDateTime::currentDateTime();

        if (!m_d->tLastStackNotificationUpdate.isValid())
        {
            m_d->tLastStackNotificationUpdate = now;
            return;
        }

        double dt = m_d->tLastStackNotificationUpdate.msecsTo(now);
        auto curCounters = m_d->mvlc->getStackErrorCounters();
        auto &prevCounters = m_d->prevStackErrors;

        QStringList strParts;

        for (u32 stackId = 0; stackId < curCounters.stackErrors.size(); stackId++)
        {
            auto curErrorInfoCounts = curCounters.stackErrors[stackId];
            auto prevErrorInfoCounts = prevCounters.stackErrors[stackId];

            size_t curTotalErrors = 0u;
            size_t prevTotalErrors = 0u;

            for (const auto &kv: curErrorInfoCounts)
                curTotalErrors += kv.second;

            for(const auto &kv: prevErrorInfoCounts)
                prevTotalErrors += kv.second;

            u64 delta = calc_delta0(curTotalErrors, prevTotalErrors);

            if (delta > 0)
            {
                double rate = delta / dt;
                strParts += QString("stack%1: %2").arg(stackId).arg(rate);
            }
        }

        QString labelText;

        if (!strParts.isEmpty())
        {
            labelText = "Error Rates: " + strParts.join(", ");
        }

        {
            u64 delta = prevCounters.nonErrorFrames - curCounters.nonErrorFrames;

            if (delta > 0)
            {
                double rate = delta / dt;
                if (!labelText.isEmpty()) labelText += ", ";
                labelText += QString("non-stack notifications: %1/s").arg(rate);
            }
        }

        ui->label_notificationStats->setText(labelText);

        m_d->tLastStackNotificationUpdate = now;
        prevCounters = curCounters; // updates m_d->prevStackErrors through the reference
    });

    updateTimer->start();

    // load default mvlcscript from resources
    {
        QFile input(":/mvlc/scripts/0-init-mtdcs.mvlcscript");
        input.open(QIODevice::ReadOnly);
        QTextStream inputStream(&input);
        ui->te_scriptInput->setPlainText(inputStream.readAll());
    }

    // Code to run on entering the event loop
    QTimer::singleShot(0, [this]() {
        this->raise(); // Raise this main window
        m_d->mvlc->connect();
    });
}

MVLCDevGUI::~MVLCDevGUI()
{
    m_d->dataReader->stop();
    m_d->readoutThread.quit();
    m_d->readoutThread.wait();
}

void MVLCDevGUI::logMessage(const QString &msg)
{
    emit sigLogMessage(msg);
}

void MVLCDevGUI::logBuffer(const std::vector<u32> &buffer, const QString &info)
{
    QStringList strBuffer;
    strBuffer.reserve(buffer.size() + 2);

    strBuffer << QString(">>> %1, size=%2").arg(info).arg(buffer.size());

    for (size_t i = 0; i < buffer.size(); i++)
    {
        u32 value = buffer.at(i);

        auto str = QString("%1: 0x%2 (%3 dec)")
            .arg(i, 3)
            .arg(value, 8, 16, QLatin1Char('0'))
            .arg(value)
            ;

        if (mvlc::is_known_frame_header(value))
        {
            str += " " + QString::fromStdString(mvlc::decode_frame_header(value));
        }

        strBuffer << str;
    }

    strBuffer << "<<< " + info;

    emit sigLogMessage(strBuffer.join("\n"));
}

void MVLCDevGUI::handleEthDebugSignal(const EthDebugBuffer &debugBuffer, const QString &reason)
{
    {
        QSignalBlocker blocker(m_d->pb_enableEthDebug);
        m_d->pb_enableEthDebug->setChecked(false);
    }

    qDebug() << __PRETTY_FUNCTION__ << debugBuffer.capacity();


    logMessage(QString(">>> Begin Ethernet Header Debug (%1 packets, reason: %2, error occured in packet %3)\n")
               .arg(debugBuffer.size())
               .arg(reason)
               .arg(debugBuffer.capacity() / 2 + 1)
               );

    size_t pktIdx = 0;
    for (const OwningPacketReadResult &rr: debugBuffer)
    {
        const mesytec::mvlc::eth::PacketReadResult &prr = rr.prr;

        logMessage(QString("* pkt %1/%2 size=%3 bytes (%4 words), lossFromPrevious=%5, availablePayloadWords=%6, leftOverBytes=%7")
                   .arg(pktIdx + 1)
                   .arg(debugBuffer.capacity())
                   .arg(prr.bytesTransferred)
                   .arg(prr.bytesTransferred / sizeof(u32))
                   .arg(prr.lostPackets)
                   .arg(prr.availablePayloadWords())
                   .arg(prr.leftoverBytes())
                  );

        logMessage(QString("  header0=0x%1, packetChannel=%2, packetNumber=%3, dataWordCount=%4")
                   .arg(prr.header0(), 8, 16, QLatin1Char('0'))
                   .arg(prr.packetChannel())
                   .arg(prr.packetNumber())
                   .arg(prr.dataWordCount()));

        logMessage(QString("  header1=0x%1, udpTs=%2, nextHeaderPointer=%3\n")
                   .arg(prr.header1(), 8, 16, QLatin1Char('0'))
                   .arg(prr.udpTimestamp())
                   .arg(prr.nextHeaderPointer()));


        static const size_t WordsPerRow = 8;
        u32 *payloadIter = prr.payloadBegin();
        QString strbuf;

        while (payloadIter < prr.payloadEnd())
        {
            size_t wordOffset = payloadIter - prr.payloadBegin();
            size_t wordsLeft  = prr.payloadEnd() - payloadIter;
            size_t nWords = std::min(WordsPerRow, wordsLeft);

            strbuf.clear();
            strbuf += QString("  wOff=%1: ").arg(wordOffset, 4);

            for (u32 i = 0; i < nWords; ++i)
            {
                strbuf += QString("0x%1 ").arg(*payloadIter, 8, 16, QLatin1Char('0'));

                ++payloadIter;
            }

            logMessage(strbuf);
        }

        logMessage("");
        pktIdx++;
    }

    logMessage(QString("<<< End Ethernet Header Debug (%1 packets)").arg(debugBuffer.size()));
}

//
// MVLCRegisterWidget
//

struct RegisterEditorWidgets
{
    QSpinBox *spin_address;

    QLineEdit *le_value;

    QLabel *l_readResult_hex,
           *l_readResult_dec;

    QPushButton *pb_write,
                *pb_read;
};

MVLCRegisterWidget::MVLCRegisterWidget(MVLCObject *mvlc, QWidget *parent)
    : QWidget(parent)
    , m_mvlc(mvlc)
{
    auto layout = new QGridLayout(this);
    int row = 0;

    layout->addWidget(new QLabel("Address"), row, 0);
    layout->addWidget(new QLabel("Value"), row, 1);
    layout->addWidget(new QLabel("Read Result"), row, 2);
    ++row;

    for (int editorIndex = 0; editorIndex < 3; ++editorIndex)
    {
        RegisterEditorWidgets widgets;
        widgets.spin_address = new QSpinBox(this);
        widgets.spin_address->setMinimumWidth(150);
        widgets.spin_address->setMinimum(0x0);
        widgets.spin_address->setMaximum(0xffff);
        widgets.spin_address->setSingleStep(2);
        widgets.spin_address->setDisplayIntegerBase(16);
        widgets.spin_address->setPrefix("0x");
        widgets.spin_address->setValue(0x1100 + 4 * editorIndex);

        widgets.le_value = new QLineEdit(this);
        widgets.l_readResult_hex = new QLabel(this);
        widgets.l_readResult_dec = new QLabel(this);
        widgets.l_readResult_hex->setMinimumWidth(60);

        for (auto label: {widgets.l_readResult_hex, widgets.l_readResult_dec})
        {
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        }

        widgets.pb_write = new QPushButton("Write", this);
        widgets.pb_read = new QPushButton("Read", this);

        auto resultLabelLayout = make_layout<QVBoxLayout, 0>();
        resultLabelLayout->addWidget(widgets.l_readResult_hex);
        resultLabelLayout->addWidget(widgets.l_readResult_dec);

        auto buttonLayout = make_layout<QVBoxLayout, 0>();
        buttonLayout->addWidget(widgets.pb_read);
        buttonLayout->addWidget(widgets.pb_write);

        layout->addWidget(widgets.spin_address, row, 0);
        layout->addWidget(widgets.le_value, row, 1);
        layout->addLayout(resultLabelLayout, row, 2);
        layout->addLayout(buttonLayout, row, 3);

        connect(widgets.pb_read, &QPushButton::clicked,
                this, [this, widgets] ()
        {
            u16 address = widgets.spin_address->value();

            u32 result = readRegister(address);
            //widgets.le_value->setText(QString("0x%1").arg(result, 8, 16, QLatin1Char('0')));
            widgets.l_readResult_hex->setText(QString("0x%1").arg(result, 8, 16, QLatin1Char('0')));
            widgets.l_readResult_dec->setText(QString::number(result));
        });

        connect(widgets.pb_write, &QPushButton::clicked,
                this, [this, widgets] ()
        {
            bool ok = true;
            u16 address = widgets.spin_address->value();
            u32 value   = widgets.le_value->text().toUInt(&ok, 0);
            writeRegister(address, value);
        });

        ++row;
    }

    layout->addWidget(make_separator_frame(), row++, 0, 1, 4); // row- and colspan

    // Stack Info
    {
        auto spin_stackId = new QSpinBox();
        spin_stackId->setMinimum(0);
        spin_stackId->setMaximum(mvlc::stacks::StackCount - 1);

        auto pb_readStackInfo = new QPushButton("Read Info");

        auto l = new QHBoxLayout;
        l->addWidget(new QLabel("Stack Info"));
        l->addWidget(spin_stackId);
        l->addWidget(pb_readStackInfo);
        l->addStretch(1);
        layout->addLayout(l, row++, 0, 1, 4);

        connect(pb_readStackInfo, &QPushButton::clicked,
                this, [this, spin_stackId] ()
        {
            u8 stackId = static_cast<u8>(spin_stackId->value());
            readStackInfo(stackId);
        });

    }

    layout->addWidget(make_separator_frame(), row++, 0, 1, 4); // row- and colspan
    ++row;

    {
        struct RegAndLabel
        {
            u16 reg;
            const char *label;
        };

        // IP-Address Registers
        static const std::vector<RegAndLabel> IPData =
        {
            { 0x4400, "Own IP"},
            { 0x4408, "Own IP DHCP" },
            { 0x440C, "Dest IP Cmd" },
            { 0x4410, "Dest IP Data" },
        };

        auto gb = new QGroupBox("IP Address Settings");
        auto grid = make_layout<QGridLayout, 2, 4>(gb);

        static const int NumCols = 2;
        int gridRow = 0, gridCol = 0;

        for (const auto &ral: IPData)
        {
            auto ipRegWidget = new IPv4RegisterWidget(ral.reg);
            auto gb_inner = new QGroupBox(ral.label);
            auto gb_inner_layout = make_layout<QHBoxLayout>(gb_inner);
            gb_inner_layout->addWidget(ipRegWidget);

            grid->addWidget(gb_inner, gridRow, gridCol++);

            if (gridCol >= NumCols)
            {
                gridRow++;
                gridCol = 0;
            }

            connect(ipRegWidget, &IPv4RegisterWidget::write,
                    this, &MVLCRegisterWidget::writeRegister);

            connect(ipRegWidget, &IPv4RegisterWidget::read,
                    this, [this, ipRegWidget] (u16 reg)
            {
                u32 result = readRegister(reg);
                ipRegWidget->setRegisterValue(reg, result);
            });

            connect(ipRegWidget, &IPv4RegisterWidget::sigLogMessage,
                    this, &MVLCRegisterWidget::sigLogMessage);

        }

        // Dest Port Registers
        static const std::vector<RegAndLabel> PortData =
        {
            { 0x441A,  "Dest Port Cmd" },
            { 0x441C,  "Dest Port Data" },
        };

        gridCol = 0;

        for (const auto &ral: PortData)
        {
            auto le_input = new QLineEdit(this);
            auto pb_read = new QPushButton("Read", this);
            auto pb_write = new QPushButton("Write", this);

            auto gb_inner = new QGroupBox(ral.label, this);
            auto gb_inner_layout = make_layout<QGridLayout>(gb_inner);
            auto reg_str = QString("0x%1").arg(ral.reg, 4, 16, QLatin1Char('0'));
            gb_inner_layout->addWidget(new QLabel(reg_str), 0, 0, 2, 1);
            gb_inner_layout->addWidget(le_input, 0, 1, 2, 1);
            gb_inner_layout->addWidget(pb_read, 0, 2);
            gb_inner_layout->addWidget(pb_write, 1, 2);
            gb_inner_layout->setColumnStretch(1, 1);

            connect(pb_read, &QPushButton::clicked,
                    this, [this, ral, le_input] ()
            {
                u16 value = readRegister(ral.reg);
                le_input->setText(QString("%1").arg(value));
            });

            connect(pb_write, &QPushButton::clicked,
                    this, [this, ral, le_input] ()
            {
                u16 value = le_input->text().toUInt(nullptr, 0);
                writeRegister(ral.reg, value);
            });

            grid->addWidget(gb_inner, gridRow, gridCol++);
        }

        gridRow++;
        gridCol = 0;

        // Add the groupbox to the outer layout
        layout->addWidget(gb, row++, 0, 1, 4);
    }

    layout->setRowStretch(row, 1);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 1);
}

MVLCRegisterWidget::~MVLCRegisterWidget()
{}

void MVLCRegisterWidget::writeRegister(u16 address, u32 value)
{
    if (auto ec = m_mvlc->writeRegister(address, value))
        emit sigLogMessage(QString("Write Register Error: %1").arg(ec.message().c_str()));
}

u32 MVLCRegisterWidget::readRegister(u16 address)
{
    u32 value = 0u;
    if (auto ec = m_mvlc->readRegister(address, value))
        emit sigLogMessage(QString("Read Register Error: %1").arg(ec.message().c_str()));

    return value;
}

void MVLCRegisterWidget::readStackInfo(u8 stackId)
{
    assert(stackId < mvlc::stacks::StackCount);

    u16 offsetRegister = mvlc::stacks::Stack0OffsetRegister + stackId * mvlc::AddressIncrement;
    u16 triggerRegister = mvlc::stacks::Stack0TriggerRegister + stackId * mvlc::AddressIncrement;

    u32 stackOffset = 0u;
    u32 stackTriggers = 0u;

    if (auto ec = m_mvlc->readRegister(offsetRegister, stackOffset))
    {
        emit sigLogMessage(QString("Read Stack Info Error: %1").arg(ec.message().c_str()));
        return;
    }

    if (auto ec = m_mvlc->readRegister(triggerRegister, stackTriggers))
    {
        emit sigLogMessage(QString("Read Stack Info Error: %1").arg(ec.message().c_str()));
        return;
    }

    QStringList strings;
    strings.reserve(1024);

    strings << QString(">>> Info for stack %1").arg(static_cast<int>(stackId));
    strings << QString("  Offset:   0x%1 = 0x%2, %3 dec")
        .arg(offsetRegister, 4, 16, QLatin1Char('0'))
        .arg(stackOffset, 4, 16, QLatin1Char('0'))
        .arg(stackOffset);
    strings << QString("  Triggers: 0x%1 = 0x%2, %3 dec")
        .arg(triggerRegister, 4, 16, QLatin1Char('0'))
        .arg(stackTriggers, 4, 16, QLatin1Char('0'))
        .arg(stackTriggers);

    u16 reg = mvlc::stacks::StackMemoryBegin + stackOffset;
    u32 stackHeader = 0u;

    if (auto ec = m_mvlc->readRegister(reg, stackHeader))
    {
        emit sigLogMessage(QString("Read Stack Info Error: %1").arg(ec.message().c_str()));
        return;
    }

    if ((stackHeader & 0xFF000000) != 0xF3000000)
    {
        strings << QString("    Invalid stack header @0x%1: 0x%2")
            .arg(reg, 4, 16, QLatin1Char('0'))
            .arg(stackHeader, 8, 16, QLatin1Char('0'));
    }
    else
    {
        strings << "  Stack Contents:";

        static const int StackMaxSize = 128;
        int stackSize = 0;

        while (stackSize <= StackMaxSize && reg < mvlc::stacks::StackMemoryEnd)
        {
            u32 value = 0u;
            if (auto ec = m_mvlc->readRegister(reg, value))
            {
                emit sigLogMessage(QString("Read Stack Info Error: %1")
                                   .arg(ec.message().c_str()));
                return;
            }

            strings << QString("   [0x%4, %3] 0x%1: 0x%2")
                .arg(reg, 4, 16, QLatin1Char('0'))
                .arg(value, 8, 16, QLatin1Char('0'))
                .arg(stackSize, 3)
                .arg(stackSize, 3, 16, QLatin1Char('0'))
                ;

            if ((value & 0xFF000000) == 0xF4000000)
            {
                break;
            }

            reg += mvlc::AddressIncrement;
            stackSize++;
        }
    }

    strings << QString("<<< End stack %1 info").arg(static_cast<int>(stackId));

    emit sigLogMessage(strings.join("\n"));

    //for (const auto &notification: m_mvlc->getStackErrorNotifications())
    //{
    //    emit stackErrorNotification(notification);
    //    //emit sigLogBuffer(notification, "Error notification from MVLC");
    //}
}

//
// LogWidget
//
LogWidget::LogWidget(QWidget *parent)
    : QWidget(parent)
    , te_log(new QPlainTextEdit(this))
    , pb_clearLog(new QPushButton("Clear", this))
{
    setWindowTitle("MVLC Dev Tool Log Window");
    auto font = make_monospace_font();
    font.setPointSize(8);
    te_log->setFont(font);

    auto bottomLayout = make_layout<QHBoxLayout>();
    bottomLayout->addWidget(pb_clearLog);
    bottomLayout->addStretch(1);

    auto widgetLayout = make_layout<QVBoxLayout>(this);
    widgetLayout->addWidget(te_log);
    widgetLayout->addLayout(bottomLayout);
    widgetLayout->setStretch(0, 1);

    connect(pb_clearLog, &QPushButton::clicked,
            this, &LogWidget::clearLog);
}

LogWidget::~LogWidget()
{
}

void LogWidget::logMessage(const QString &msg)
{
    te_log->appendPlainText(msg);
    auto bar = te_log->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void LogWidget::clearLog()
{
    te_log->clear();
}

//
// IPv4RegisterWidget
//
IPv4RegisterWidget::IPv4RegisterWidget(u16 regLo, const QString &regName, QWidget *parent)
    : IPv4RegisterWidget(regLo, regLo + sizeof(u16), regName, parent)
{}

IPv4RegisterWidget::IPv4RegisterWidget(u16 regLo, u16 regHi, const QString &regName, QWidget *parent)
    : QWidget(parent)
    , m_regLo(regLo)
    , m_regHi(regHi)
    , le_valLo(new QLineEdit(this))
    , le_valHi(new QLineEdit(this))
    , le_addressInput(new QLineEdit(this))
{
    auto l_regLo = new QLabel(QSL("0x%1").arg(m_regLo, 4, 16, QLatin1Char('0')));
    auto l_regHi = new QLabel(QSL("0x%1").arg(m_regHi, 4, 16, QLatin1Char('0')));
    auto pb_read = new QPushButton(QSL("Read"));
    auto pb_write = new QPushButton(QSL("Write"));

    for (auto le: {le_valLo, le_valHi})
    {
        auto pal = le->palette();
        pal.setColor(QPalette::Base, QSL("#efebe7"));
        le->setPalette(pal);
        le->setReadOnly(true);
    }

    auto layout = new QGridLayout(this);

    int col = 0;

    if (!regName.isEmpty())
        layout->addWidget(new QLabel(regName), 0, col++, 2, 1);

    layout->addWidget(l_regLo, 0, col);
    layout->addWidget(l_regHi, 1, col++);
    layout->addWidget(le_valLo, 0, col);
    layout->addWidget(le_valHi, 1, col++);
    layout->addWidget(le_addressInput, 0, col, 2, 1);
    layout->setColumnStretch(col++, 1);
    layout->addWidget(pb_read, 0, col);
    layout->addWidget(pb_write, 1, col++);

    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    connect(pb_read, &QPushButton::clicked,
            this, [this] ()
    {
        emit read(m_regLo);
        emit read(m_regHi);
    });

    connect(pb_write, &QPushButton::clicked,
            this, [this] ()
    {
        // - take input from le_addressInput
        // - convert to 32-bit value either by numeric conversion or by parsing
        //   IPv4 notation
        // - split into hi and lo parts
        // - emit write for both parts with the corresponding register address

        static const QRegularExpression re(
            R"(^([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})$)");
        auto input = le_addressInput->text();

        auto match = re.match(input);

        u32 ipAddressValue = 0u;

        if (match.hasMatch())
        {
            for (int i=1; i<=4; i++)
            {
                u32 part = match.captured(i).toUInt();
                qDebug() << "i=" << i << "part=" << part;
                ipAddressValue <<= 8;
                ipAddressValue |= part;
            }
        }
        else
        {
            bool ok = false;
            ipAddressValue = input.toUInt(&ok, 0);

            if (!ok)
            {
                emit sigLogMessage("Invalid IP address entered");
                return;
            }
        }

        u16 loPart = (ipAddressValue >>  0) & 0xffff;
        u16 hiPart = (ipAddressValue >> 16) & 0xffff;

        emit sigLogMessage(QString("Parsed IP Address: %1, setting hi=0x%2, lo=0x%3")
                           .arg(format_ipv4(ipAddressValue))
                           .arg(hiPart, 4, 16, QLatin1Char('0'))
                           .arg(loPart, 4, 16, QLatin1Char('0'))
                           );

        le_valLo->clear();
        le_valHi->clear();

        emit write(m_regLo, loPart);
        emit write(m_regHi, hiPart);
    });
}

void IPv4RegisterWidget::setRegisterValue(u16 reg, u16 value)
{
    QLineEdit *le_val = nullptr;

    if (reg == m_regLo)
        le_val = le_valLo;
    else if (reg == m_regHi)
        le_val = le_valHi;
    else
        return;

    le_val->setText(QString("0x%1").arg(value, 4, 16, QLatin1Char('0')));

    u32 loPart = le_valLo->text().toUInt(nullptr, 0);
    u32 hiPart = le_valHi->text().toUInt(nullptr, 0);
    u32 ipAddressValue = (hiPart << 16) | loPart;

    le_addressInput->setText(format_ipv4(ipAddressValue));
}
