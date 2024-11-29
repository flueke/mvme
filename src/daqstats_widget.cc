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
#include "daqstats_widget.h"

#include <boost/range/adaptor/indexed.hpp>
#include <cmath>
#include <QFormLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>

#include <mesytec-mvlc/util/counters.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/mvlc_readout.h>
#include <mesytec-mvlc/mvlc_eth_interface.h>

#include "mvlc_readout_worker.h"
#include "mvme_context.h"
#include "qt_util.h"
#include "sis3153_readout_worker.h"
#include "util/counters.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvlc/mvlc_util.h"

using boost::adaptors::indexed;
using namespace mesytec::mvme;
using namespace mesytec;

static const int UpdateInterval_ms = 1000;

struct DAQStatsWidgetPrivate
{
    DAQStatsWidget *q;

    MVMEContext *context;


    struct CountersHolder
    {
        DAQStats daqStats;
        SIS3153ReadoutWorker::Counters sisCounters;
        mesytec::mvlc::ReadoutWorker::Counters mvlcReadoutCounters;
    };

    CountersHolder prevCounters;
    QDateTime lastUpdateTime;

    QLabel *label_daqDuration,
           *label_buffersRead,
           *label_bufferRates,
           *label_bytesRead,

           *label_sisEventLoss,

           *label_mvlcFrameTypeErrors,
           //*label_mvlcPartialFrameTotalBytes,
           *label_mvlcReceivedPackets,
           *label_mvlcLostPackets,
           *label_mvlcEthThrottling,

           *label_mvlcStackErrors
               ;

    QWidget *genericWidget,
            *sisWidget,
            *mvlcUSBWidget,
            *mvlcETHWidget,
            *mvlcStackErrorsWidget;

    QProgressBar *listfileQueueFillLevel;
    QColor fillLevelBarDefaultColor;

    void update_generic(const DAQStats &stats, const DAQStats &prevStats,
                        double dt_s, double elapsed_s)
    {
        u64 deltaBytesRead = mvlc::util::calc_delta0(stats.totalBytesRead, prevStats.totalBytesRead);
        u64 deltaBuffersRead = mvlc::util::calc_delta0(stats.totalBuffersRead, prevStats.totalBuffersRead);

        double bytesPerSecond   = deltaBytesRead / dt_s;
        double mbPerSecond      = bytesPerSecond / Megabytes(1);
        double buffersPerSecond = deltaBuffersRead / dt_s;

        // Set NaNs to 0 before displaying them.
        if (std::isnan(buffersPerSecond)) buffersPerSecond = 0.0;
        if (std::isnan(mbPerSecond)) mbPerSecond = 0.0;

        auto totalDurationString = makeDurationString(elapsed_s);

        label_daqDuration->setText(totalDurationString);

        auto buffersReadText = QString::number(stats.totalBuffersRead);

        if (stats.buffersWithErrors > 0)
            buffersReadText += QSL(" (processing errors=%1)").arg(stats.buffersWithErrors);

        label_buffersRead->setText(buffersReadText);

        label_bufferRates->setText(QString("%1 buffers/s, %2 MB/s")
                                   .arg(buffersPerSecond, 6, 'f', 2)
                                   .arg(mbPerSecond, 6, 'f', 2)
                                  );

        label_bytesRead->setText(QString("%1 MB")
                                 .arg((double)stats.totalBytesRead / (1024.0*1024.0), 6, 'f', 2)
                                );
    }

    void update_SIS3153(const SIS3153ReadoutWorker::Counters &sisCounters,
                        const SIS3153ReadoutWorker::Counters &prevSISCounters,
                        double dt_s)
    {
        double totalLostEvents = sisCounters.lostEvents;
        double totalGoodEvents = sisCounters.receivedEventsExcludingWatchdog();
        double eventLossRatio  = totalLostEvents / totalGoodEvents;

        u64 deltaLostEvents   = mvlc::util::calc_delta0(sisCounters.lostEvents, prevSISCounters.lostEvents);
        double eventLossRate  = deltaLostEvents / dt_s;

        if (std::isnan(eventLossRatio)) eventLossRatio = 0.0;
        if (std::isnan(eventLossRate)) eventLossRate = 0.0;

        QString lossText = (QString("lost=%1, received=%2\n"
                                    "lossRatio=%3, lossRate=%4 events/s"
                                    )
                            .arg(totalLostEvents)
                            .arg(totalGoodEvents, 0, 'f', 0)
                            .arg(eventLossRatio, 0, 'f', 3)
                            .arg(eventLossRate)
                           );

        label_sisEventLoss->setText(lossText);
    }

    using MVLCCounters = mesytec::mvlc::ReadoutWorker::Counters;

    void updateReaodutCountersMVLC_USB(
        const mesytec::mvlc::ReadoutWorker::Counters &counters,
        const mesytec::mvlc::ReadoutWorker::Counters &prevCounters,
        double dt_s)
    {
        #define TYPE_AND_VAL(foo) decltype(foo), foo
        u64 frameTypeErrorRate =
            mvlc::util::calc_rate0<TYPE_AND_VAL(&MVLCCounters::usbFramingErrors)>(
                counters, prevCounters, dt_s);

        label_mvlcFrameTypeErrors->setText(
            (QString("%1, %2 errors/s")
             .arg(counters.usbFramingErrors)
             .arg(frameTypeErrorRate)));

#if 0
        u64 partialFrameTotalBytesRate =
            calc_rate0<TYPE_AND_VAL(&MVLCReadoutCounters::partialFrameTotalBytes)>(
                mvlcReadoutCounters, prevMVLCCounters, dt_s);

        label_mvlcPartialFrameTotalBytes->setText(
            (QString("%1, %2")
             .arg(mvlcReadoutCounters.partialFrameTotalBytes)
             .arg(format_number(partialFrameTotalBytesRate, "bytes/s", UnitScaling::Binary, 0, 'f', 0))
             ));
#endif
        #undef TYPE_AND_VAL
    }

    void updateReadoutCountersMVLC_ETH(const mesytec::mvlc::eth::PipeStats &dataPipeStats,
                         const mesytec::mvlc::eth::PipeStats &prevStats,
                         double dt_s)
    {
        u64 packetRate = mvlc::util::calc_rate0(
            dataPipeStats.receivedPackets, prevStats.receivedPackets, dt_s);

        u64 packetLossRate = mvlc::util::calc_rate0(
            dataPipeStats.lostPackets, prevStats.lostPackets, dt_s);

        label_mvlcReceivedPackets->setText(
            (QString("%1, %2 packets/s")
             .arg(dataPipeStats.receivedPackets)
             .arg(packetRate)));

        label_mvlcLostPackets->setText(
            (QString("%1, %2 packets/s")
             .arg(dataPipeStats.lostPackets)
             .arg(packetLossRate)));
    }

    void updateMVLC_ETH_Throttling(const mesytec::mvlc::eth::MVLC_ETH_Interface *mvlcEth)
    {
        auto counters = mvlcEth->getThrottleCounters();

        label_mvlcEthThrottling->setText(
            QSL("delay: cur=%3 µs, avg=%4 µs, max=%5 µs")
            .arg(counters.currentDelay)
            .arg(counters.avgDelay, 0, 'f', 2)
            .arg(counters.maxDelay));
    }

    void updateMVLCStackErrors(const mesytec::mvlc::StackErrorCounters &counters)
    {
        QString text;
        bool needNewline = false;

        for (const auto &kv: counters.stackErrors | indexed(0))
        {
            unsigned stackId = kv.index();
            const auto &counts = kv.value();

            if (counts.empty())
                continue;

            for (auto it = counts.begin(); it != counts.end(); ++it)
            {
                const auto &errorInfo = it->first;
                const auto &count = it->second;


                text += (QSL("stackId=%1, stackLine=%2, flags=%3, count=%4\n")
                         .arg(stackId)
                         .arg(errorInfo.line)
                         .arg(mesytec::mvlc::format_frame_flags(errorInfo.flags).c_str())
                         .arg(count));
            }
        }

        if (counters.nonErrorFrames)
        {
            if (needNewline)
                text += QSL("\n");
            text += QString("unexpected non-error frames: %1").arg(counters.nonErrorFrames);
        }

        label_mvlcStackErrors->setText(text);
    }

    void updateWidget(VMEReadoutWorker *readoutWorker)
    {
        auto controller = readoutWorker->getVMEController();
        auto sisWorker = qobject_cast<SIS3153ReadoutWorker *>(readoutWorker);
        auto mvlc = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(controller);
        bool is_MVLC_USB = mvlc && mvlc->getType() == VMEControllerType::MVLC_USB;
        bool is_MVLC_ETH = mvlc && mvlc->getType() == VMEControllerType::MVLC_ETH;

        sisWidget->setVisible(sisWorker != nullptr);
        mvlcUSBWidget->setVisible(is_MVLC_USB);
        mvlcETHWidget->setVisible(is_MVLC_ETH);
        mvlcStackErrorsWidget->setVisible(mvlc != nullptr);
#if 0
        listfileQueueFillLevel->setVisible(mvlc != nullptr
            && context->getDAQState() != DAQState::Idle
            && context->getListFileOutputInfo().enabled);
#else
        listfileQueueFillLevel->setVisible(false);
#endif

        auto daqStats = context->getDAQStats();
        auto startTime = daqStats.startTime;
        auto endTime   = (context->getDAQState() == DAQState::Idle
                          ? daqStats.endTime
                          : QDateTime::currentDateTime());

        double dt_s = 0.0;

        if (lastUpdateTime.isValid())
            dt_s = lastUpdateTime.msecsTo(endTime) / 1000.0;
        else
            dt_s = startTime.msecsTo(endTime) / 1000.0;

        double elapsed_s = startTime.msecsTo(endTime) / 1000.0;

        update_generic(daqStats, prevCounters.daqStats, dt_s, elapsed_s);
        prevCounters.daqStats = daqStats;

        if (sisWorker)
        {
            auto sisCounters = sisWorker->getCounters();

            update_SIS3153(sisCounters, prevCounters.sisCounters, dt_s);
            prevCounters.sisCounters = sisCounters;
        }

        if (auto mvlcReadoutWorker = qobject_cast<MVLCReadoutWorker *>(readoutWorker))
        {
            auto mvlcReadoutCounters = mvlcReadoutWorker->getReadoutCounters();

            if (is_MVLC_USB)
            {
                updateReaodutCountersMVLC_USB(mvlcReadoutCounters, prevCounters.mvlcReadoutCounters, dt_s);
                prevCounters.mvlcReadoutCounters = mvlcReadoutCounters;
            }

            if (is_MVLC_ETH)
            {
                updateReadoutCountersMVLC_ETH(
                    mvlcReadoutCounters.ethStats[mesytec::mvlc::DataPipe],
                    prevCounters.mvlcReadoutCounters.ethStats[mesytec::mvlc::DataPipe],
                    dt_s);

                auto mvlcEth = dynamic_cast<mesytec::mvlc::eth::MVLC_ETH_Interface *>(mvlc->getImpl());
                assert(mvlcEth);

                updateMVLC_ETH_Throttling(mvlcEth);
            }

            if (mvlc)
                updateMVLCStackErrors(mvlc->getMVLC().getStackErrorCounters());

            {
                auto rangeMin = 0;
                auto rangeMax = mvlcReadoutCounters.listfileWriterCounters.bufferQueueCapacity;
                auto value = mvlcReadoutCounters.listfileWriterCounters.bufferQueueSize;

                // Subtract 1 from rangeMax to account for the buffer that's
                // currently being processed by the listfile writer. If not done
                // the display will show 9/10 all the time while in reality all
                // buffers are used and we want 9/9 being shown.
                //rangeMax -= 1;

                // Same as above but adjust the value so instead of 0/10 we get 1/10
                if (context->getDAQState() != DAQState::Idle)
                {
                    if (value < rangeMax)
                        value += 1;
                }
                else
                    value = 0; // daq is idle -> always show 0 buffers in use

                QColor color(fillLevelBarDefaultColor);

                if (value == rangeMax && rangeMax > 0)
                    color = Qt::red;

                listfileQueueFillLevel->setRange(rangeMin, rangeMax);
                listfileQueueFillLevel->setValue(value);
                auto pal = listfileQueueFillLevel->palette();
                pal.setColor(QPalette::Highlight, color);
                listfileQueueFillLevel->setPalette(pal);
            }

            prevCounters.mvlcReadoutCounters = mvlcReadoutCounters;
        }

        lastUpdateTime = QDateTime::currentDateTime();
    }
};

DAQStatsWidget::DAQStatsWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new DAQStatsWidgetPrivate)
{
    *m_d = {};
    m_d->q = this;
    m_d->context = context;

    //set_widget_font_pointsize(this, 8);

    m_d->label_daqDuration = new QLabel;
    m_d->label_buffersRead = new QLabel;
    m_d->label_bufferRates = new QLabel;
    m_d->label_bytesRead = new QLabel;

    m_d->label_sisEventLoss = new QLabel;

    m_d->label_mvlcFrameTypeErrors = new QLabel;
    //m_d->label_mvlcPartialFrameTotalBytes = new QLabel;
    m_d->label_mvlcReceivedPackets = new QLabel;
    m_d->label_mvlcLostPackets = new QLabel;
    m_d->label_mvlcEthThrottling = new QLabel;
    m_d->label_mvlcStackErrors = new QLabel;

    QList<QLabel *> labels =
    {
        m_d->label_daqDuration,
        m_d->label_buffersRead,
        m_d->label_bufferRates,
        m_d->label_bytesRead,
        m_d->label_sisEventLoss,
        m_d->label_mvlcFrameTypeErrors,
        //m_d->label_mvlcPartialFrameTotalBytes,
        m_d->label_mvlcReceivedPackets,
        m_d->label_mvlcLostPackets,
        m_d->label_mvlcEthThrottling,
        m_d->label_mvlcStackErrors,
    };

    for (auto label: labels)
    {
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    }

    m_d->genericWidget = new QWidget;
    m_d->sisWidget = new QWidget;
    m_d->mvlcUSBWidget = new QWidget;
    m_d->mvlcETHWidget = new QWidget;
    m_d->mvlcStackErrorsWidget = new QWidget;
    m_d->listfileQueueFillLevel = new QProgressBar;
    m_d->listfileQueueFillLevel->setAlignment(Qt::AlignCenter);
    m_d->listfileQueueFillLevel->setFormat("Listfile queue fill level: %p% (%v of %m buffers in use)");
    m_d->listfileQueueFillLevel->setRange(0, 10);
    m_d->listfileQueueFillLevel->setValue(0);
    m_d->fillLevelBarDefaultColor = m_d->listfileQueueFillLevel->palette().highlight().color();

    auto genericLayout = make_layout<QFormLayout, 0, 2>(m_d->genericWidget);
    auto sisLayout = make_layout<QFormLayout, 0, 2>(m_d->sisWidget);
    auto mvlcUSBLayout = make_layout<QFormLayout, 0, 2>(m_d->mvlcUSBWidget);
    auto mvlcETHLayout = make_layout<QFormLayout, 0, 2>(m_d->mvlcETHWidget);
    auto mvlcStackErrorsLayout = make_layout<QFormLayout, 0, 2>(m_d->mvlcStackErrorsWidget);
    auto listfileQueueLayout = make_layout<QFormLayout, 0, 2>();
    listfileQueueLayout->addRow(m_d->listfileQueueFillLevel);

    auto vboxLayout = make_layout<QVBoxLayout, 0, 0>(this);
    vboxLayout->addWidget(m_d->genericWidget);
    vboxLayout->addWidget(m_d->sisWidget);
    vboxLayout->addWidget(m_d->mvlcUSBWidget);
    vboxLayout->addWidget(m_d->mvlcETHWidget);
    vboxLayout->addLayout(listfileQueueLayout);
    vboxLayout->addWidget(m_d->mvlcStackErrorsWidget);

    genericLayout->addRow("Running time:", m_d->label_daqDuration);
    genericLayout->addRow("Buffers read:", m_d->label_buffersRead);
    genericLayout->addRow("Bytes read:", m_d->label_bytesRead);
    genericLayout->addRow("Data rates:", m_d->label_bufferRates);

    sisLayout->addRow("Event Loss:", m_d->label_sisEventLoss);

    mvlcUSBLayout->addRow("MVLC USB Frame Type Errors:", m_d->label_mvlcFrameTypeErrors);
    //mvlcUSBLayout->addRow("MVLC USB Partial Frame Bytes:", m_d->label_mvlcPartialFrameTotalBytes);

    mvlcETHLayout->addRow("MVLC ETH received packets:", m_d->label_mvlcReceivedPackets);
    mvlcETHLayout->addRow("MVLC ETH lost packets:", m_d->label_mvlcLostPackets);
    mvlcETHLayout->addRow("MVLC ETH throttling:", m_d->label_mvlcEthThrottling);

    mvlcStackErrorsLayout->addRow("MVLC Stack Errors:", m_d->label_mvlcStackErrors);

    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    updateWidget();

    auto timer = new QTimer(this);
    timer->setInterval(UpdateInterval_ms);
    timer->start();

    connect(timer, &QTimer::timeout,
            this, &DAQStatsWidget::updateWidget);

    connect(context, &MVMEContext::vmeControllerSet,
            this, &DAQStatsWidget::updateWidget);
}

DAQStatsWidget::~DAQStatsWidget()
{
    delete m_d;
}

void DAQStatsWidget::updateWidget()
{
    if (auto rdoWorker = m_d->context->getReadoutWorker())
        m_d->updateWidget(rdoWorker);
}
