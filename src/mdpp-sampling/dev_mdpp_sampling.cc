#include <QApplication>
#include <QPushButton>
#include <QPen>
#include <QFileDialog>
#include <QMouseEvent>
#include <QTimer>
#include <QPainterPath>
#include <qwt_painter.h>
#include <qwt_picker_machine.h>
#include <qwt_plot_picker.h>
#include <spdlog/spdlog.h>
#include <QComboBox>

#include "mvme_session.h"
#include "mdpp-sampling/mdpp_sampling.h"

using namespace mesytec::mvme;

static const int ReplotInterval = 50;
static const int GlWidgetUpdateInterval = 16;
static const int TraceUpdateInterval = 500;

void fill_0(std::vector<float> &dest, float dt)
{
    for (auto i=0; i<dest.size(); ++i)
    {
        auto t = i * dt;
        dest[i] = 0.5 * sin(2 * M_PI * 1 * t) + 0.3 * sin(2 * M_PI * 2 * t);
    }
}

void fill_1(std::vector<float> &dest, float dt)
{
    for (auto i=0; i<dest.size(); ++i)
    {
        dest[i] = (sin(i / 10.0 - M_PI / 2) + 1) / (2.0 + i/100.0);
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    mvme_init("dev_mdpp_sampling");
    spdlog::set_level(spdlog::level::info);
    //mesytec::mvlc::set_global_log_level(spdlog::level::debug);

    QAction actionQuit("&Quit");
    actionQuit.setShortcut(QSL("Ctrl+Q"));
    actionQuit.setShortcutContext(Qt::ApplicationShortcut);

    QObject::connect(&actionQuit, &QAction::triggered, &app, &QApplication::quit);

    TracePlotWidget plotWidget1;
    plotWidget1.addAction(&actionQuit);
    plotWidget1.show();

    QTimer replotTimer;
    QObject::connect(&replotTimer, &QTimer::timeout,
                     &plotWidget1, &histo_ui::IPlotWidget::replot);

    replotTimer.setInterval(ReplotInterval);
    replotTimer.start();

    ChannelTrace trace0;
    trace0.channel = 0;
    trace0.samples.resize(64);

    size_t cycle = 0;

    auto update_trace0 = [&cycle, &trace0] ()
    {
        for (auto i=0; i<trace0.samples.size(); ++i)
        {
            auto y = -100 + i * i * 0.1;
            trace0.samples[i] = y;
        }

        ++cycle;
    };

    plotWidget1.setTrace(&trace0);

    QTimer traceUpdateTimer;

    QObject::connect(&traceUpdateTimer, &QTimer::timeout,
                     plotWidget1.getPlot(), update_trace0);

    traceUpdateTimer.setInterval(TraceUpdateInterval);
    traceUpdateTimer.start();
    update_trace0();

    std::vector<float> raw_trace0(256);
    std::vector<float> raw_trace1(256);

    fill_0(raw_trace0, 1.0);
    fill_1(raw_trace1, 1.0);

    QTimer glWidgetUpdateTimer;

    GlTracePlotWidget glWidget(nullptr);
    glWidget.setWindowTitle("GlTracePlotWidget 0");
    QObject::connect(&glWidgetUpdateTimer, &QTimer::timeout, &glWidget, [&] {glWidget.update();});
    glWidget.addAction(&actionQuit);
    glWidget.show();
    glWidget.setTrace(raw_trace0.data(), raw_trace0.size());

    GlTracePlotWidget glWidget1(nullptr);
    glWidget1.setWindowTitle("GlTracePlotWidget 1");
    QObject::connect(&glWidgetUpdateTimer, &QTimer::timeout, &glWidget1, [&] {glWidget1.update();});
    glWidget1.addAction(&actionQuit);
    glWidget1.show();
    glWidget1.setTrace(raw_trace1.data(), raw_trace1.size());

    glWidgetUpdateTimer.setInterval(GlWidgetUpdateInterval);
    glWidgetUpdateTimer.start();

    return app.exec();
}
