#include "rate_monitor_widget.h"
#include "rate_monitor_samplers.h"
#include "util/typedefs.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const size_t BufferCapacity1 = 3600;
    const size_t BufferCapacity2 = 750;
    const s32 ReplotPeriod_ms = 1000;
    const s32 NewDataPeriod_ms = 250;
    const s32 NewDataCount = 50;

    QVector<RateSamplerPtr> samplers;

    // plot data
    auto sampler1 = std::make_shared<RateSampler>();
    sampler1->rateHistory = RateHistoryBuffer(BufferCapacity1);
    samplers.push_back(sampler1);

    auto sampler2 = std::make_shared<RateSampler>();
    sampler2->rateHistory = RateHistoryBuffer(BufferCapacity2);
    samplers.push_back(sampler2);

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
    // Fill rateHistory1
    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            //double value = dist(gen);
            double value = (std::sin(x1 * 0.25) + SinOffset) * SinScale;
            x1 += SinInc;
            sampler1->record_rate(value);
        }
    });

#if 1
    // Fill rateHistory2
    QObject::connect(&fillTimer, &QTimer::timeout, &rmw, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = (std::cos(x2 * 0.25) + SinOffset + 0.5) * SinScale;
            x2 += SinInc + 0.125;
            sampler2->record_rate(value);
        }
    });
#endif

    fillTimer.setInterval(NewDataPeriod_ms);
    fillTimer.start();

    return app.exec();
}
