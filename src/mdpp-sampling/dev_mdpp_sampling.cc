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

static const int ReplotInterval = 16;
static const int TraceUpdateInterval = 500;

int main(int argc, char **argv)
{
    spdlog::set_level(spdlog::level::info);
    QApplication app(argc, argv);
    mvme_init("dev_histo_ui");

    QAction actionQuit("&Quit");
    actionQuit.setShortcut(QSL("Ctrl+Q"));
    actionQuit.setShortcutContext(Qt::ApplicationShortcut);

    QObject::connect(&actionQuit, &QAction::triggered, &app, &QApplication::quit);

    TracePlotWidget plotWidget1;
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

    GlTracePlotWidget glWidget(nullptr);
    glWidget.show();

    plotWidget1.addAction(&actionQuit);
    glWidget.addAction(&actionQuit);

    return app.exec();
}
