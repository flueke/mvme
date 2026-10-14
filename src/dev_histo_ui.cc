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
#include "histo_ui.h"
#include "scrollzoomer.h"
#include "scrollbar.h"

// old codebase
#include "histo_gui_util.h"

using namespace histo_ui;

void logger(const QString &msg)
{
    spdlog::info(msg.toStdString());
}

void setup_intervals_combo(
    PlotWidget *w,
    NewIntervalPicker *newIntervalPicker,
    IntervalEditorPicker *intervalEditorPicker)
{
    auto combo = new QComboBox;
    combo->setObjectName("intervalsCombo");
    combo->setEditable(true);
    combo->setMinimumWidth(150);
    combo->addItem("-");

    QObject::connect(newIntervalPicker, &NewIntervalPicker::intervalSelected,
                     combo, [=] (const QwtInterval &interval)
                     {
                         auto name = QSL("interval%1").arg(combo->count()-1);
                         combo->addItem(
                             name,
                             QVariantList { interval.minValue(), interval.maxValue() });
                         combo->setCurrentIndex(combo->count() - 1);
                     });

    QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                     w, [=] (int comboIndex)
                     {
                         auto varList = combo->itemData(comboIndex).toList();
                         QwtInterval interval;
                         if (varList.size() == 2)
                         {
                             qDebug() << varList[0] << varList[1];
                             interval = {varList[0].toDouble(),
                                         varList[1].toDouble()};
                         }
                         intervalEditorPicker->setInterval(interval);
                     });
    w->getToolBar()->addWidget(combo);
}


int main(int argc, char **argv)
{
    spdlog::set_level(spdlog::level::info);
    QApplication app(argc, argv);
    mvme_init("dev_histo_ui");

    PlotWidget plotWidget1;
    plotWidget1.show();

    //watch_mouse_move(&plotWidget1);
    setup_axis_scale_changer(&plotWidget1, QwtPlot::yLeft, "Y-Scale");
    install_scrollzoomer(&plotWidget1);
    install_poly_picker(&plotWidget1);
    //install_tracker_picker(&plotWidget1);
    //install_clickpoint_picker(&plotWidget1);
    //install_dragpoint_picker(&plotWidget1);
    auto newIntervalPicker = install_new_interval_picker(&plotWidget1);
    auto intervalEditorPicker = install_interval_editor(&plotWidget1);
    //install_rate_estimation_tool(&plotWidget1);
    setup_intervals_combo(&plotWidget1, newIntervalPicker, intervalEditorPicker);

    debug_watch_plot_pickers(plotWidget1.getPlot());

#if 0
    // log PlotWidget enter/leave events
    QObject::connect(&plotWidget1, &PlotWidget::mouseEnteredPlot,
                     &plotWidget1, [] ()
                     {
                         spdlog::info("plotWidget1: mouse entered plot");
                     });

    QObject::connect(&plotWidget1, &PlotWidget::mouseLeftPlot,
                     &plotWidget1, [] ()
                     {
                         spdlog::info("plotWidget1: mouse left plot");
                     });
#endif

#if 0
    {
        auto w = new QWidget;
        w->setAttribute(Qt::WA_DeleteOnClose);
        auto vbox = new QVBoxLayout(w);
        auto exclusiveActions = group_picker_actions(&plotWidget1);

        for (auto action: exclusiveActions->actions())
        {
            auto l = new QHBoxLayout;

            auto pbEn = new QPushButton("Check");
            auto pbDis = new QPushButton("Uncheck");

            l->addWidget(new QLabel(action->text()));
            l->addWidget(pbEn);
            l->addWidget(pbDis);

            QObject::connect(pbEn, &QPushButton::clicked, [action] () { action->setChecked(true); });
            QObject::connect(pbDis, &QPushButton::clicked, [action] () { action->setChecked(false); });

            vbox->addLayout(l);
        }

        vbox->addStretch(1);
        w->show();
    }
#endif


    const int ReplotInterval = 1000;
    QTimer replotTimer;
    QObject::connect(&replotTimer, &QTimer::timeout,
                     plotWidget1.getPlot(), &QwtPlot::replot);

    replotTimer.setInterval(ReplotInterval);
    replotTimer.start();

    return app.exec();
}
