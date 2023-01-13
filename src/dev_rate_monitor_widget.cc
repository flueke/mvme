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
#include "rate_monitor_widget.h"
#include "rate_monitor_samplers.h"
#include "util/typedefs.h"

#include <QApplication>
#include <QPushButton>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const size_t BufferCapacity1 = 600;
    const size_t BufferCapacity2 = 600;
    const size_t BufferCapacity3 = 600;
    const size_t BufferCapacity4 = 600;
    const size_t BufferCapacity5 = 600;

    const s32 NewDataPeriod_ms = 100;
    const s32 NewDataCount = 1;

    QVector<RateSamplerPtr> samplers;

    auto sampler1 = std::make_shared<RateSampler>();
    sampler1->rateHistory = RateHistoryBuffer(BufferCapacity1);
    sampler1->interval = 1.0;
    samplers.push_back(sampler1);

    auto sampler2 = std::make_shared<RateSampler>();
    sampler2->rateHistory = RateHistoryBuffer(BufferCapacity2);
    sampler2->interval = 0.5;
    samplers.push_back(sampler2);

    auto sampler3 = std::make_shared<RateSampler>();
    sampler3->rateHistory = RateHistoryBuffer(BufferCapacity3);
    sampler3->interval = 2.0;
    samplers.push_back(sampler3);

    auto sampler4 = std::make_shared<RateSampler>();
    sampler4->rateHistory = RateHistoryBuffer(BufferCapacity4);
    sampler4->interval = 10.0;
    samplers.push_back(sampler4);

    auto sampler5 = std::make_shared<RateSampler>();
    sampler5->rateHistory = RateHistoryBuffer(BufferCapacity5);
    sampler5->interval = 1.0;
    samplers.push_back(sampler5);

    RateMonitorWidget rmw(samplers);

    rmw.show();

    //
    // Fill timer
    //
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(0.0, 15.0);

    static const double SinOffset = 1.0;
    static const double SinScale = 1.0;
    static const double SinInc = 0.10;
    double x1 = 0.0;
    double x2 = 0.0;

    QTimer fillTimer;

    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            //double value = dist(gen);
            double value = (std::sin(x1 * 0.25) + SinOffset) * SinScale;
            x1 += SinInc;
            sampler1->recordRate(value);
        }
    });

    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = (std::cos(x2 * 0.25) + SinOffset + 0.5) * SinScale;
            x2 += SinInc + 0.125;
            sampler2->recordRate(value);
        }
    });

    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = (std::tan(x2 * 0.25) + SinOffset + 0.5) * SinScale;
            x2 += SinInc + 0.125;
            sampler3->recordRate(value);
        }
    });

    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = (std::atan(x2 * 0.25) + SinOffset + 0.5) * SinScale;
            x2 += SinInc + 0.125;
            sampler4->recordRate(value);
        }
    });

    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = 3.14;
            sampler5->recordRate(value);
        }
    });

    fillTimer.setInterval(NewDataPeriod_ms);
    fillTimer.start();

    QPushButton pb_pause("Pause fill timer");
    pb_pause.setCheckable(true);
    pb_pause.show();

    QObject::connect(&pb_pause, &QPushButton::clicked, &rmw, [&] () {
        if (pb_pause.isChecked())
            fillTimer.stop();
        else
            fillTimer.start(NewDataPeriod_ms);
    });

    return app.exec();
}
