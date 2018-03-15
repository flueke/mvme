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
            sampler1->record_rate(value);
        }
    });

    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = (std::cos(x2 * 0.25) + SinOffset + 0.5) * SinScale;
            x2 += SinInc + 0.125;
            sampler2->record_rate(value);
        }
    });

    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = (std::tan(x2 * 0.25) + SinOffset + 0.5) * SinScale;
            x2 += SinInc + 0.125;
            sampler3->record_rate(value);
        }
    });

    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = (std::atan(x2 * 0.25) + SinOffset + 0.5) * SinScale;
            x2 += SinInc + 0.125;
            sampler4->record_rate(value);
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
