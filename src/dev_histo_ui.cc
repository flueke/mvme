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

void debug_watch_plot_pickers(QWidget *w)
{
    for (auto picker: w->findChildren<QwtPlotPicker *>())
    {
        QObject::connect(
            picker, &QwtPicker::activated,
            picker, [picker] (bool on)
            {
                spdlog::info("picker {} activated, on={}",
                             picker->objectName().toStdString(), on);
            });

#if 0
        QObject::connect(
            picker, &QwtPlotPicker::moved,
            picker, [picker] (const QPointF &pos)
            {
                spdlog::info("picker {} moved to coordinate ({}, {})",
                             picker->objectName().toStdString(), pos.x(), pos.y());
            });
#endif

        QObject::connect(
            picker, qOverload<const QPointF &>(&QwtPlotPicker::selected),
            picker, [picker] (const QPointF &pos)
            {
                spdlog::info("picker {} selected plot point coordinate ({}, {})",
                             picker->objectName().toStdString(), pos.x(), pos.y());
            });

        QObject::connect(
            picker, qOverload<const QRectF &>(&QwtPlotPicker::selected),
            picker, [picker] (const QRectF &pos)
            {
                (void) pos;
                spdlog::info("picker {} selected plot rect",
                             picker->objectName().toStdString());
            });

        QObject::connect(
            picker, qOverload<const QVector<QPointF> &>(&QwtPlotPicker::selected),
            picker, [picker] (const QVector<QPointF> &points)
            {
                (void) points;
                spdlog::info("picker {} selected {} plot points",
                             picker->objectName().toStdString(), points.size());
            });

        QObject::connect(
            picker, &QwtPlotPicker::appended,
            picker, [picker] (const QPointF &pos)
            {
                spdlog::info("picker {} appeneded plot point coordinate ({}, {})",
                             picker->objectName().toStdString(), pos.x(), pos.y());
            });
    }
}

template<typename Context, typename Functor>
QAction *install_checkable_toolbar_action(
    PlotWidget *w, const QString &label, const QString &actionName,
    Context context, Functor functor)
{
    auto toolbar = w->getToolBar();
    auto action = toolbar->addAction(label);
    action->setObjectName(actionName);
    action->setCheckable(true);
    QObject::connect(action, &QAction::toggled, context, functor);
    return action;
}

// Ownership of the zoomer goes to the plot.
QwtPlotZoomer *install_scrollzoomer(PlotWidget *w)
{
    auto zoomer = new ScrollZoomer(w->getPlot()->canvas());
    //auto zoomer = new QwtPlotZoomer(w->getPlot()->canvas());
    zoomer->setObjectName("zoomer");
    zoomer->setEnabled(false);

    install_checkable_toolbar_action(
        w, "Zoom", "zoomAction",
        zoomer, [zoomer] (bool checked)
        {
            spdlog::info("zoom action toggled, checked={}", checked);
            zoomer->setEnabled(checked);
            // Show/hide scrollbars unless fully zoomed out.
            if (zoomer->zoomRectIndex() != 0)
            {
                if (auto sb = zoomer->horizontalScrollBar())
                    sb->setVisible(checked);

                if (auto sb = zoomer->verticalScrollBar())
                    sb->setVisible(checked);
            }
        });


    return zoomer;
}

NewIntervalPicker *install_new_interval_picker(PlotWidget *w)
{
    auto picker = new NewIntervalPicker(w->getPlot());

    picker->setObjectName("NewIntervalPicker");
    picker->setEnabled(false);

    install_checkable_toolbar_action(
        w, "New Interval", "newIntervalPickerAction",
        picker, [picker] (bool checked)
        {
            spdlog::info("newIntervalPickerAction toggled, checked={}", checked);
            picker->setEnabled(checked);
            picker->reset();
        });

    QObject::connect(
        picker, &NewIntervalPicker::intervalSelected,
        picker, [] (const QwtInterval &interval)
        {
            spdlog::info("NewIntervalPicker: intervalSelected: ({}, {})",
                         interval.minValue(), interval.maxValue());
        });

    // React to canceled from the picker. TODO: move this to the outside where
    // actions are managed
    QObject::connect(
        picker, &NewIntervalPicker::canceled,
        w, [w] ()
        {
            spdlog::info("NewIntervalPicker: canceled, activating zoomAction");
            if (auto zoomAction = w->findChild<QAction *>("zoomAction"))
            {
                zoomAction->setChecked(true);
            }
        });

    return picker;
}

QwtPlotPicker *install_poly_picker(PlotWidget *w)
{
    auto picker = new PlotPicker(
        QwtPlot::xBottom, QwtPlot::yLeft,
        QwtPicker::PolygonRubberBand,
        QwtPicker::ActiveOnly,
        w->getPlot()->canvas());

    picker->setStateMachine(new QwtPickerPolygonMachine);

    picker->setObjectName("polyPicker");
    picker->setEnabled(false);

    install_checkable_toolbar_action(
        w, "Polygon", "polyPickerAction",
        picker, [picker] (bool checked)
        {
            spdlog::info("polyPickerAction toggled, checked={}", checked);
            picker->setEnabled(checked);
            picker->reset();
        });

    return picker;
}

QwtPlotPicker *install_tracker_picker(PlotWidget *w)
{
    auto picker = new PlotPicker(
        QwtPlot::xBottom, QwtPlot::yLeft,
        QwtPicker::NoRubberBand,
        QwtPicker::ActiveOnly,
        w->getPlot()->canvas());

    picker->setStateMachine(new QwtPickerTrackerMachine);

    picker->setObjectName("trackerPicker");
    picker->setEnabled(false);

    install_checkable_toolbar_action(
        w, "Tracker", "trackerPickerAction",
        picker, [picker] (bool checked)
        {
            spdlog::info("trackerPickerAction toggled, checked={}", checked);
            picker->setEnabled(checked);
        });

    return picker;
}

QwtPlotPicker *install_clickpoint_picker(PlotWidget *w)
{
    auto picker = new PlotPicker(
        QwtPlot::xBottom, QwtPlot::yLeft,
        QwtPicker::VLineRubberBand,
        QwtPicker::AlwaysOn,
        w->getPlot()->canvas());

    picker->setStateMachine(new QwtPickerClickPointMachine);

    picker->setObjectName("ClickPointPicker");
    picker->setEnabled(false);

    install_checkable_toolbar_action(
        w, "ClickPoint", "clickPointPickerAction",
        picker, [picker] (bool checked)
        {
            spdlog::info("clickPointPickerAction toggled, checked={}", checked);
            picker->setEnabled(checked);
        });

    return picker;
}

QwtPlotPicker *install_dragpoint_picker(PlotWidget *w)
{
    auto picker = new PlotPicker(
        QwtPlot::xBottom, QwtPlot::yLeft,
        QwtPicker::VLineRubberBand,
        QwtPicker::AlwaysOn,
        w->getPlot()->canvas());

    picker->setStateMachine(new QwtPickerDragPointMachine);

    picker->setObjectName("DragPointPicker");
    picker->setEnabled(false);

    install_checkable_toolbar_action(
        w, "DragPoint", "dragPointPickerAction",
        picker, [picker] (bool checked)
        {
            spdlog::info("dragPointPickerAction toggled, checked={}", checked);
            picker->setEnabled(checked);
        });

    return picker;
}

QActionGroup *group_picker_actions(PlotWidget *w)
{
    auto group = new QActionGroup(w);
    group->setObjectName("exclusivePlotActions");
    //group->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

    auto add_if_found = [group, w] (auto actionName)
    {
        if (auto action = w->findChild<QAction *>(actionName))
            group->addAction(action);
    };

    add_if_found("zoomAction");
    add_if_found("polyPickerAction");
    add_if_found("trackerPickerAction");
    add_if_found("clickPointPickerAction");
    add_if_found("dragPointPickerAction");
    add_if_found("newIntervalPickerAction");

    if (auto firstAction = group->actions().first())
        firstAction->setChecked(true);

    return group;
}

void watch_mouse_move(PlotWidget *w)
{
    QObject::connect(w, &PlotWidget::mouseMoveOnPlot,
                     [] (const QPointF &f)
                     {
                         spdlog::info("watch_mouse_move: mouse moved to ({}, {})",
                                      f.x(), f.y());
                     });
}

void install_intervals_combo(PlotWidget *w, NewIntervalPicker *picker)
{
    auto combo = new QComboBox;
    combo->setMinimumWidth(150);
    combo->addItem("-");

    QObject::connect(picker, &NewIntervalPicker::intervalSelected,
                     combo, [=] (const QwtInterval &interval)
                     {
                         combo->addItem(
                             "interval",
                             QVariantList { interval.minValue(), interval.maxValue() });
                         // FIXME: cancel used to force going back to the zoomer.
                         // Maybe add something that tracks the last active
                         // tool and goes back to that?
                         picker->cancel();
                     });

    QObject::connect(combo, qOverload<int>(&QComboBox::activated),
                     w, [=] (int comboIndex)
                     {
                         auto varList = combo->itemData(comboIndex).toList();
                         if (varList.size() == 2)
                         {
                             qDebug() << varList[0] << varList[1];
                         }
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
    install_scrollzoomer(&plotWidget1);
    install_poly_picker(&plotWidget1);
    //install_tracker_picker(&plotWidget1);
    //install_clickpoint_picker(&plotWidget1);
    //install_dragpoint_picker(&plotWidget1);
    auto intervalPicker = install_new_interval_picker(&plotWidget1);
    install_intervals_combo(&plotWidget1, intervalPicker);

    debug_watch_plot_pickers(plotWidget1.getPlot());
    auto exclusiveActions = group_picker_actions(&plotWidget1);


    {
        auto w = new QWidget;
        w->setAttribute(Qt::WA_DeleteOnClose);
        auto vbox = new QVBoxLayout(w);

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


    const int ReplotInterval = 100;
    QTimer replotTimer;
    QObject::connect(&replotTimer, &QTimer::timeout,
                     plotWidget1.getPlot(), &QwtPlot::replot);

    replotTimer.setInterval(ReplotInterval);
    replotTimer.start();

    return app.exec();
}
