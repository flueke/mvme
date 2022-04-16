#include <QApplication>
#include <QFileDialog>
#include <spdlog/spdlog.h>

#include "mvme_session.h"
#include "histo_ui.h"
#include "scrollzoomer.h"

using namespace histo_ui;

void logger(const QString &msg)
{
    spdlog::info(msg.toStdString());
}

void install_scrollzoomer(PlotWidget *w)
{
    auto zoomer = new ScrollZoomer(w->getPlot()->canvas());
    zoomer->setObjectName("zoomer");
}

class TestPicker: public ScrollZoomer
{
    public:
        //using QwtPlotPicker::QwtPlotPicker;
        using ScrollZoomer::ScrollZoomer;

    protected:
        void move(const QPoint &pos) override
        {
            spdlog::info("TestPicker::move(): mouse moved to ({}, {})",
                         pos.x(), pos.y());
            ScrollZoomer::move(pos);
        }
};

void watch_mouse_move(PlotWidget *w)
{
    QObject::connect(w, &PlotWidget::mouseMoveOnPlot,
                     [] (const QPointF &f)
                     {
                         spdlog::info("watch_mouse_move: mouse moved to ({}, {})",
                                      f.x(), f.y());
                     });
}

int main(int argc, char **argv)
{
    spdlog::set_level(spdlog::level::info);
    QApplication app(argc, argv);
    mvme_init("dev_histo_ui");

    spdlog::info("foo!");

    PlotWidget plotWidget1;
    plotWidget1.show();

    install_scrollzoomer(&plotWidget1);
    //watch_mouse_move(&plotWidget1);
    watch_mouse_move(&plotWidget1);

    return app.exec();
}
