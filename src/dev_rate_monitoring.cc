#include "rate_monitoring.h"

#include <QApplication>
#include <QDebug>
#include <QTimer>
#include <random>
#include "util/typedefs.h"
#include <qwt_legend.h>
#include <qwt_plot.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const size_t BufferCapacity = 100;
    const s32 ReplotPeriod_ms = 500;
    auto rateHistory = std::make_shared<RateHistoryBuffer>(BufferCapacity);

    rateHistory->push_back(1);
    rateHistory->push_back(2);
    rateHistory->push_back(3);
    rateHistory->push_back(4);
    rateHistory->push_back(5);

    // Plot and external legend
    RateMonitorPlotWidget plotWidget;
    QwtLegend legend;

    QObject::connect(plotWidget.getPlot(), &QwtPlot::legendDataChanged,
                     &legend, &QwtLegend::updateLegend);

    legend.setDefaultItemMode(QwtLegendData::Checkable);


    // set plot data and show widgets
    plotWidget.setRateHistoryBuffer(rateHistory);
    plotWidget.show();
    //legend.show();

    // Fill and replot using a timer
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(0.0, 15.0);

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &plotWidget, [&] () {

        double value = dist(gen);
        rateHistory->push_back(value);
        plotWidget.replot();
    });

    timer.setInterval(ReplotPeriod_ms);
    timer.start();

    return app.exec();
}
