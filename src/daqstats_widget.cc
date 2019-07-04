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

static const int UpdateInterval_ms = 1000;

struct DAQStatsWidgetPrivate
{
    MVMEContext *context;
    DAQStats prevStats;
    SIS3153ReadoutWorker::Counters prevSISCounters;
    MVLCReadoutCounters prevMVLCCounters;
    mesytec::mvlc::eth::PipeStats prevDataPipeStats;
    QDateTime lastUpdateTime;

    QLabel *label_daqDuration,
           *label_buffersRead,
           *label_bufferErrors,
           *label_bufferRates,
           *label_readSize,
           *label_bytesRead,
           *label_sisEventLoss,

           *label_mvlcFrameTypeErrors,
           *label_mvlcPartialFrameTotalBytes,

           *label_mvlcReceivedPackets,
           *label_mvlcLostPackets
               ;

    QFormLayout *formLayout;
};

DAQStatsWidget::DAQStatsWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new DAQStatsWidgetPrivate)
{
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
    m_d->label_mvlcPartialFrameTotalBytes = new QLabel;
    m_d->label_mvlcReceivedPackets = new QLabel;
    m_d->label_mvlcLostPackets = new QLabel;

    QList<QWidget *> labels = {
        m_d->label_daqDuration,
        m_d->label_buffersRead,
        m_d->label_bufferErrors,
        m_d->label_bufferRates,
        m_d->label_readSize,
        m_d->label_bytesRead,
        m_d->label_sisEventLoss,
        m_d->label_mvlcFrameTypeErrors,
        m_d->label_mvlcPartialFrameTotalBytes,
        m_d->label_mvlcReceivedPackets,
        m_d->label_mvlcLostPackets
    };

    for (auto label: labels)
    {
        label->setParent(this);
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    }

    auto formLayout = new QFormLayout(this);
    m_d->formLayout = formLayout;

    formLayout->addRow("Running time:", m_d->label_daqDuration);
    formLayout->addRow("Buffers read:", m_d->label_buffersRead);
    formLayout->addRow("Buffer Errors:", m_d->label_bufferErrors);
    formLayout->addRow("Data rates:", m_d->label_bufferRates);
    formLayout->addRow("Avg. read size:", m_d->label_readSize);
    formLayout->addRow("Bytes read:", m_d->label_bytesRead);
    formLayout->addRow("Event Loss:", m_d->label_sisEventLoss);
    formLayout->addRow("MVLC USB Frame Type Errors:", m_d->label_mvlcFrameTypeErrors);
    formLayout->addRow("MVLC USB Partial Frame Bytes:", m_d->label_mvlcPartialFrameTotalBytes);
    formLayout->addRow("MVLC ETH received Packets:", m_d->label_mvlcReceivedPackets);
    formLayout->addRow("MVLC ETH lost packets:", m_d->label_mvlcLostPackets);

    //formLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    //formLayout->setSizeConstraint(QLayout::SetFixedSize);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    updateWidget();

    auto timer = new QTimer(this);
    timer->setInterval(UpdateInterval_ms);
    timer->start();

    connect(timer, &QTimer::timeout, this, &DAQStatsWidget::updateWidget);
    connect(context, &MVMEContext::daqStateChanged, this, [this](const DAQState &state) {
        if (state == DAQState::Running)
        {
            m_d->prevStats = {};
            m_d->lastUpdateTime = {};
        }
    });
}

DAQStatsWidget::~DAQStatsWidget()
{
    delete m_d;
}

void DAQStatsWidget::updateWidget()
{
    auto daqStats = m_d->context->getDAQStats();

    auto startTime = daqStats.startTime;
    auto endTime   = (m_d->context->getDAQState() == DAQState::Idle
                      ? daqStats.endTime
                      : QDateTime::currentDateTime());
    auto totalDuration_s = startTime.secsTo(endTime);
    auto totalDurationString = makeDurationString(totalDuration_s);

    double dt;

    if (m_d->lastUpdateTime.isValid())
    {
        dt = m_d->lastUpdateTime.msecsTo(endTime);
    }
    else
    {
        dt = startTime.msecsTo(endTime);
    }

    dt /= 1000.0;

    u64 deltaBytesRead = calc_delta0(daqStats.totalBytesRead, m_d->prevStats.totalBytesRead);
    u64 deltaBuffersRead = calc_delta0(daqStats.totalBuffersRead, m_d->prevStats.totalBuffersRead);

    double bytesPerSecond   = deltaBytesRead / dt;
    double mbPerSecond      = bytesPerSecond / Megabytes(1);
    double buffersPerSecond = deltaBuffersRead / dt;
    double avgReadSize      = deltaBytesRead / static_cast<double>(deltaBuffersRead);

    u64 bufferProcessingErrors = daqStats.buffersWithErrors;

    // Set NaNs to 0 before displaying them.
    if (std::isnan(buffersPerSecond)) buffersPerSecond = 0.0;
    if (std::isnan(mbPerSecond)) mbPerSecond = 0.0;
    if (std::isnan(avgReadSize)) avgReadSize = 0.0;

    m_d->label_daqDuration->setText(totalDurationString);
    m_d->label_buffersRead->setText(QString::number(daqStats.totalBuffersRead));
    m_d->label_bufferErrors->setText(QString::number(bufferProcessingErrors));
    m_d->label_readSize->setText(QString::number(avgReadSize));

    m_d->label_bufferRates->setText(QString("%1 buffers/s, %2 MB/s")
                                    .arg(buffersPerSecond, 6, 'f', 2)
                                    .arg(mbPerSecond, 6, 'f', 2)
                                    );

    m_d->label_bytesRead->setText(
            QString("%1 MB")
            .arg((double)daqStats.totalBytesRead / (1024.0*1024.0), 6, 'f', 2)
            );

    auto update_label_visibility = [this] (auto &labels, bool show)
    {
        std::for_each(labels.begin(), labels.end(), [this, show] (QLabel *label) {
            m_d->formLayout->labelForField(label)->setVisible(show);
            label->setVisible(show);
        });
    };

    //
    // SIS3153 specific counters
    //
    SIS3153ReadoutWorker::Counters sisCounters;

    auto sisLabels = { m_d->label_sisEventLoss };

    auto rdoWorker = m_d->context->getReadoutWorker();
    auto sisWorker = qobject_cast<SIS3153ReadoutWorker *>(rdoWorker);
    update_label_visibility(sisLabels, sisWorker != nullptr);

    if (sisWorker)
    {
        sisCounters = sisWorker->getCounters();

        double totalLostEvents = sisCounters.lostEvents;
        double totalGoodEvents = sisCounters.receivedEventsExcludingWatchdog();
        double eventLossRatio  = totalLostEvents / totalGoodEvents;

        u64 deltaLostEvents   = calc_delta0(sisCounters.lostEvents, m_d->prevSISCounters.lostEvents);
        double eventLossRate  = deltaLostEvents / dt;

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

        m_d->label_sisEventLoss->setText(lossText);
    }

    //
    // MVLC specific statistics
    //
    auto mvlcUSBLabels = { m_d->label_mvlcFrameTypeErrors, m_d->label_mvlcPartialFrameTotalBytes };
    auto mvlcETHLabels = { m_d->label_mvlcReceivedPackets, m_d->label_mvlcLostPackets };
    auto mvlcWorker = qobject_cast<MVLCReadoutWorker *>(rdoWorker);

    bool is_MVLC_USB = (mvlcWorker && mvlcWorker->getMVLC())
        ? mvlcWorker->getMVLC()->getType() == VMEControllerType::MVLC_USB
        : false;

    bool is_MVLC_ETH = (mvlcWorker && mvlcWorker->getMVLC())
        ? mvlcWorker->getMVLC()->getType() == VMEControllerType::MVLC_ETH
        : false;

    update_label_visibility(mvlcUSBLabels, is_MVLC_USB);
    update_label_visibility(mvlcETHLabels, is_MVLC_ETH);

    MVLCReadoutCounters mvlcCounters;

    if (is_MVLC_USB)
    {
        assert(mvlcWorker);

        mvlcCounters = mvlcWorker->getReadoutCounters();
        auto &prevMVLCCounters = m_d->prevMVLCCounters;

        auto frameTypeErrors = format_delta_and_rate(
            mvlcCounters.frameTypeErrors, prevMVLCCounters.frameTypeErrors, dt,
            "errors");

        auto partialFrameTotalBytes = format_delta_and_rate(
            mvlcCounters.partialFrameTotalBytes, prevMVLCCounters.partialFrameTotalBytes, dt,
            "bytes", UnitScaling::Binary);

        m_d->label_mvlcFrameTypeErrors->setText(
            (QString("%1, %2")
             .arg(mvlcCounters.frameTypeErrors)
             .arg(frameTypeErrors.second)));

        m_d->label_mvlcPartialFrameTotalBytes->setText(
            (QString("%1, %2")
             .arg(mvlcCounters.partialFrameTotalBytes)
             .arg(partialFrameTotalBytes.second)));
    }


    mesytec::mvlc::eth::PipeStats dataPipeStats;

    if (is_MVLC_ETH)
    {
        assert(mvlcWorker);
        {
            auto mvlc_eth = reinterpret_cast<mesytec::mvlc::eth::Impl *>(
                mvlcWorker->getMVLC()->getImpl());
            auto guard = mvlcWorker->getMVLC()->getLocks().lockBoth();
            dataPipeStats = mvlc_eth->getPipeStats()[mesytec::mvlc::DataPipe];
        }

        u64 packetRate = calc_rate0(
            dataPipeStats.receivedPackets, m_d->prevDataPipeStats.receivedPackets, dt);

        u64 packetLossRate = calc_rate0(
            dataPipeStats.lostPackets, m_d->prevDataPipeStats.lostPackets, dt);

        m_d->label_mvlcReceivedPackets->setText(
            (QString("%1, %2 packets/s")
             .arg(dataPipeStats.receivedPackets)
             .arg(packetRate)));

        m_d->label_mvlcLostPackets->setText(
            (QString("%1, %2 packets/s")
             .arg(dataPipeStats.lostPackets)
             .arg(packetLossRate)));
    }

    // Store current counts
    m_d->prevStats = daqStats;
    m_d->prevSISCounters = sisCounters;
    m_d->prevMVLCCounters = mvlcCounters;
    m_d->prevDataPipeStats = dataPipeStats;
    m_d->lastUpdateTime = QDateTime::currentDateTime();
}
