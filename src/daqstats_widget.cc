/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include <cmath>
#include <QLabel>
#include <QFormLayout>
#include <QTimer>

#include "mvlc_readout_worker.h"
#include "mvme_context.h"
#include "qt_util.h"
#include "sis3153_readout_worker.h"
#include "util/counters.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvlc/mvlc_impl_eth.h"
#include "mvlc/mvlc_util.h"

static const int UpdateInterval_ms = 1000;

struct DAQStatsWidgetPrivate
{
    DAQStatsWidget *q;

    MVMEContext *context;


    struct CountersHolder
    {
        DAQStats daqStats;
        SIS3153ReadoutWorker::Counters sisCounters;
        MVLCReadoutCounters mvlcCounters;
        mesytec::mvlc::eth::PipeStats mvlcDataPipeStats;
    };

    CountersHolder prevCounters;
    QDateTime lastUpdateTime;

    QLabel *label_daqDuration,
           *label_buffersRead,
           *label_bufferErrors,
           *label_bufferRates,
           *label_readSize,
           *label_bytesRead,

           *label_sisEventLoss,

           *label_mvlcFrameTypeErrors,
           //*label_mvlcPartialFrameTotalBytes,
           *label_mvlcReceivedPackets,
           *label_mvlcLostPackets,
           *label_mvlcStackErrors
               ;

    QWidget *genericWidget,
            *sisWidget,
            *mvlcUSBWidget,
            *mvlcETHWidget,
            *mvlcStackErrorsWidget;

    void update_generic(const DAQStats &stats, const DAQStats &prevStats,
                        double dt_s, double elapsed_s)
    {
        u64 deltaBytesRead = calc_delta0(stats.totalBytesRead, prevStats.totalBytesRead);
        u64 deltaBuffersRead = calc_delta0(stats.totalBuffersRead, prevStats.totalBuffersRead);

        double bytesPerSecond   = deltaBytesRead / dt_s;
        double mbPerSecond      = bytesPerSecond / Megabytes(1);
        double buffersPerSecond = deltaBuffersRead / dt_s;
        double avgReadSize      = deltaBytesRead / static_cast<double>(deltaBuffersRead);

        u64 bufferProcessingErrors = stats.buffersWithErrors;

        // Set NaNs to 0 before displaying them.
        if (std::isnan(buffersPerSecond)) buffersPerSecond = 0.0;
        if (std::isnan(mbPerSecond)) mbPerSecond = 0.0;
        if (std::isnan(avgReadSize)) avgReadSize = 0.0;

        auto totalDurationString = makeDurationString(elapsed_s);

        label_daqDuration->setText(totalDurationString);
        label_buffersRead->setText(QString::number(stats.totalBuffersRead));
        label_bufferErrors->setText(QString::number(bufferProcessingErrors));
        label_readSize->setText(QString::number(avgReadSize));

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

        u64 deltaLostEvents   = calc_delta0(sisCounters.lostEvents, prevSISCounters.lostEvents);
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

    void update_MVLC_USB(const MVLCReadoutCounters &mvlcCounters,
                         const MVLCReadoutCounters &prevMVLCCounters,
                         double dt_s)
    {
        u64 frameTypeErrorRate =
            calc_rate0<TYPE_AND_VAL(&MVLCReadoutCounters::frameTypeErrors)>(
                mvlcCounters, prevMVLCCounters, dt_s);

        label_mvlcFrameTypeErrors->setText(
            (QString("%1, %2 errors/s")
             .arg(mvlcCounters.frameTypeErrors)
             .arg(frameTypeErrorRate)));

#if 0
        u64 partialFrameTotalBytesRate =
            calc_rate0<TYPE_AND_VAL(&MVLCReadoutCounters::partialFrameTotalBytes)>(
                mvlcCounters, prevMVLCCounters, dt_s);

        label_mvlcPartialFrameTotalBytes->setText(
            (QString("%1, %2")
             .arg(mvlcCounters.partialFrameTotalBytes)
             .arg(format_number(partialFrameTotalBytesRate, "bytes/s", UnitScaling::Binary, 0, 'f', 0))
             ));
#endif
    }

    void update_MVLC_ETH(const mesytec::mvlc::eth::PipeStats &dataPipeStats,
                         const mesytec::mvlc::eth::PipeStats &prevStats,
                         double dt_s)
    {
        u64 packetRate = calc_rate0(
            dataPipeStats.receivedPackets, prevStats.receivedPackets, dt_s);

        u64 packetLossRate = calc_rate0(
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

    void updateWidget(VMEReadoutWorker *readoutWorker)
    {
        auto controller = readoutWorker->getVMEController();
        auto sisWorker = qobject_cast<SIS3153ReadoutWorker *>(readoutWorker);
        auto mvlc = qobject_cast<mesytec::mvlc::MVLC_VMEController *>(controller);
        bool is_MVLC_USB = mvlc && mvlc->getType() == VMEControllerType::MVLC_USB;
        bool is_MVLC_ETH = mvlc && mvlc->getType() == VMEControllerType::MVLC_ETH;

        sisWidget->setVisible(sisWorker != nullptr);
        mvlcUSBWidget->setVisible(is_MVLC_USB);
        mvlcETHWidget->setVisible(is_MVLC_ETH);
        mvlcStackErrorsWidget->setVisible(mvlc != nullptr);

        auto daqStats  = context->getDAQStats();
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

        if (auto mvlcWorker = qobject_cast<MVLCReadoutWorker *>(readoutWorker))
        {
            auto mvlcCounters = mvlcWorker->getReadoutCounters();

            if (is_MVLC_USB)
            {
                update_MVLC_USB(mvlcCounters, prevCounters.mvlcCounters, dt_s);
                prevCounters.mvlcCounters = mvlcCounters;
            }

            if (is_MVLC_ETH)
            {
                auto mvlc_eth = reinterpret_cast<mesytec::mvlc::eth::Impl *>(
                    mvlc->getImpl());
                auto guard = mvlc->getLocks().lockBoth();
                auto dataPipeStats = mvlc_eth->getPipeStats()[mesytec::mvlc::DataPipe];

                update_MVLC_ETH(dataPipeStats, prevCounters.mvlcDataPipeStats, dt_s);
                prevCounters.mvlcDataPipeStats = dataPipeStats;
            }
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
    m_d->label_bufferErrors = new QLabel;
    m_d->label_bufferRates = new QLabel;
    m_d->label_readSize = new QLabel;
    m_d->label_bytesRead = new QLabel;

    m_d->label_sisEventLoss = new QLabel;

    m_d->label_mvlcFrameTypeErrors = new QLabel;
    //m_d->label_mvlcPartialFrameTotalBytes = new QLabel;
    m_d->label_mvlcReceivedPackets = new QLabel;
    m_d->label_mvlcLostPackets = new QLabel;
    m_d->label_mvlcStackErrors = new QLabel;

    QList<QLabel *> labels =
    {
        m_d->label_daqDuration,
        m_d->label_buffersRead,
        m_d->label_bufferErrors,
        m_d->label_bufferRates,
        m_d->label_readSize,
        m_d->label_bytesRead,
        m_d->label_sisEventLoss,
        m_d->label_mvlcFrameTypeErrors,
        //m_d->label_mvlcPartialFrameTotalBytes,
        m_d->label_mvlcReceivedPackets,
        m_d->label_mvlcLostPackets,
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

    auto genericLayout = make_layout<QFormLayout, 0, 2>(m_d->genericWidget);
    auto sisLayout = make_layout<QFormLayout, 0, 2>(m_d->sisWidget);
    auto mvlcUSBLayout = make_layout<QFormLayout, 0, 2>(m_d->mvlcUSBWidget);
    auto mvlcETHLayout = make_layout<QFormLayout, 0, 2>(m_d->mvlcETHWidget);
    auto mvlcStackErrorsLayout = make_layout<QFormLayout, 0, 2>(m_d->mvlcStackErrorsWidget);

    auto vboxLayout = make_layout<QVBoxLayout, 0, 0>(this);
    vboxLayout->addWidget(m_d->genericWidget);
    vboxLayout->addWidget(m_d->sisWidget);
    vboxLayout->addWidget(m_d->mvlcUSBWidget);
    vboxLayout->addWidget(m_d->mvlcETHWidget);
    vboxLayout->addWidget(m_d->mvlcStackErrorsWidget);

    genericLayout->addRow("Running time:", m_d->label_daqDuration);
    genericLayout->addRow("Buffers read:", m_d->label_buffersRead);
    genericLayout->addRow("Buffer Errors:", m_d->label_bufferErrors);
    genericLayout->addRow("Data rates:", m_d->label_bufferRates);
    genericLayout->addRow("Avg. read size:", m_d->label_readSize);
    genericLayout->addRow("Bytes read:", m_d->label_bytesRead);

    sisLayout->addRow("Event Loss:", m_d->label_sisEventLoss);

    mvlcUSBLayout->addRow("MVLC USB Frame Type Errors:", m_d->label_mvlcFrameTypeErrors);
    //mvlcUSBLayout->addRow("MVLC USB Partial Frame Bytes:", m_d->label_mvlcPartialFrameTotalBytes);

    mvlcETHLayout->addRow("MVLC ETH received packets:", m_d->label_mvlcReceivedPackets);
    mvlcETHLayout->addRow("MVLC ETH lost packets:", m_d->label_mvlcLostPackets);

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
    m_d->updateWidget(m_d->context->getReadoutWorker());
}
