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

static const int UpdateInterval_ms = 1000;

struct DAQStatsWidgetPrivate
{
    MVMEContext *context;
    DAQStats prevStats;
    SIS3153ReadoutWorker::Counters prevSISCounters;
    QDateTime lastUpdateTime;

    QLabel *label_daqDuration,
           *label_buffersRead,
           *label_bufferErrors,
           *label_bufferRates,
           *label_readSize,
           *label_bytesRead,
           *label_sisEventLoss
               ;
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

    QList<QWidget *> labels = {
        m_d->label_daqDuration,
        m_d->label_buffersRead,
        m_d->label_bufferErrors,
        m_d->label_bufferRates,
        m_d->label_readSize,
        m_d->label_bytesRead,
        m_d->label_sisEventLoss,
    };

    for (auto label: labels)
    {
        label->setParent(this);
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    }

    auto formLayout = new QFormLayout(this);

    formLayout->addRow("Running time:", m_d->label_daqDuration);
    formLayout->addRow("Buffers read:", m_d->label_buffersRead);
    formLayout->addRow("Buffer Errors:", m_d->label_bufferErrors);
    formLayout->addRow("Data rates:", m_d->label_bufferRates);
    formLayout->addRow("Avg. read size:", m_d->label_readSize);
    formLayout->addRow("Bytes read:", m_d->label_bytesRead);
    formLayout->addRow("Event Loss:", m_d->label_sisEventLoss);

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

    // SIS3153 specific packet loss count and rate
    SIS3153ReadoutWorker::Counters sisCounters;

    if (auto rdoWorker = qobject_cast<SIS3153ReadoutWorker *>(m_d->context->getReadoutWorker()))
    {
        sisCounters = rdoWorker->getCounters();

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

        m_d->label_sisEventLoss->show();
        m_d->label_sisEventLoss->setText(lossText);
    }
    else
    {
        m_d->label_sisEventLoss->hide();
    }

    // Store current counts
    m_d->prevStats = daqStats;
    m_d->prevSISCounters = sisCounters;
    m_d->lastUpdateTime = QDateTime::currentDateTime();
}
