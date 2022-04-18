#include "dev_histo_ui.h"
#include <QApplication>
#include <QPen>
#include <QFileDialog>
#include <QMouseEvent>
#include <QTimer>
#include <QPainterPath>
#include <QwtPainter>
#include <QwtPickerPolygonMachine>
#include <QwtPlotPicker>
#include <spdlog/spdlog.h>

#include "mvme_session.h"
#include "histo_ui.h"
#include "scrollzoomer.h"

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
    //auto zoomer = new ScrollZoomer(w->getPlot()->canvas());
    auto zoomer = new QwtPlotZoomer(w->getPlot()->canvas());
    zoomer->setObjectName("zoomer");
    zoomer->setEnabled(false);

    install_checkable_toolbar_action(
        w, "Zoom", "zoomAction",
        zoomer, [zoomer] (bool checked)
        {
            spdlog::info("zoom action toggled, checked={}", checked);
            zoomer->setEnabled(checked);
        });


    return zoomer;
}

class PlotPicker: public QwtPlotPicker
{
    public:
        using QwtPlotPicker::QwtPlotPicker;

        // make the protected QwtPlotPicker::reset() public
        void reset() override
        {
            QwtPlotPicker::reset();
        }
};

#if 1
QList<QwtPickerMachine::Command> NewIntervalPickerMachine::transition(
    const QwtEventPattern &eventPattern, const QEvent *event)
{
    QList< QwtPickerMachine::Command > cmdList;

    switch ( event->type() )
    {
        case QEvent::Enter:
            if (state() == 0)
            {
                spdlog::info("NewIntervalPickerMachine: Enter, Begin, Append");
                cmdList += Begin;
                cmdList += Append;
                setState(1);
            } break;
        case QEvent::MouseButtonPress:
            {
                if ( eventPattern.mouseMatch( QwtEventPattern::MouseSelect1,
                                             static_cast< const QMouseEvent* >( event ) ) )
                {
                    if ( state() == 0 )
                    {
                        spdlog::info("NewIntervalPickerMachine: MouseButtonPress, Begin, Append");
                        cmdList += Begin;
                        cmdList += Append;
                        setState( 1 );
                    }
                    else if (state() == 1)
                    {
                        spdlog::info("NewIntervalPickerMachine: state==1, Move, Append");
                        cmdList += Move;
                        cmdList += Append;
                        setState(state() + 1);
                    }
                    else if (state() == 2)
                    {
                        spdlog::info("NewIntervalPickerMachine: state==2, Move");
                        cmdList += Move;
                        setState(state() + 1);
                    }
                }
                else if ( eventPattern.mouseMatch( QwtEventPattern::MouseSelect2,
                                                  static_cast< const QMouseEvent* >( event ) ) )
                {
                    spdlog::info("NewIntervalPickerMachine: End (canceled)");
                    if (state() != 0)
                        cmdList += Remove;
                    cmdList += End;
                    setState(0);
                }
            } break;
        case QEvent::MouseMove:
        case QEvent::Wheel:
            {
                if ( state() != 0 )
                    cmdList += Move;
                break;
            }
        case QEvent::MouseButtonRelease:
            {
                if ( state() == 3 )
                {
                    spdlog::info("NewIntervalPickerMachine: End");
                    cmdList += End;
                    setState( 0 );
                }
                break;
            }
        default:
            break;
    }

    return cmdList;
}

NewIntervalPickerMachine::~NewIntervalPickerMachine()
{}

class NewIntervalPicker: public PlotPicker
{
    public:
        using PlotPicker::PlotPicker;

    protected:
        void transition(const QEvent *event) override
        {
            switch (event->type())
            {
                case QEvent::MouseButtonRelease:
                    if (mouseMatch(QwtEventPattern::MouseSelect2,
                                   static_cast<const QMouseEvent *>(event)))
                    {
                        end(false);
                    }
                    else
                    {
                        PlotPicker::transition(event);
                    }
                    break;

                default:
                    PlotPicker::transition(event);
                    break;
            }
        }
};
#endif

QwtPlotPicker *install_new_interval_picker(PlotWidget *w)
{
    auto picker = new NewIntervalPicker(
        QwtPlot::xBottom, QwtPlot::yLeft,
        QwtPicker::VLineRubberBand,
        QwtPicker::ActiveOnly,
        w->getPlot()->canvas());

    picker->setStateMachine(new AutoBeginClickPointMachine);

    picker->setObjectName("NewIntervalPicker");
    picker->setEnabled(false);

    install_checkable_toolbar_action(
        w, "New Interval", "newIntervalPickerAction",
        picker, [picker] (bool checked)
        {
            spdlog::info("newIntervalPickerAction toggled, checked={}", checked);
            picker->setEnabled(checked);
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

    group->addAction(w->findChild<QAction *>("zoomAction"));
    group->addAction(w->findChild<QAction *>("polyPickerAction"));
    group->addAction(w->findChild<QAction *>("trackerPickerAction"));
    group->addAction(w->findChild<QAction *>("clickPointPickerAction"));
    group->addAction(w->findChild<QAction *>("dragPointPickerAction"));
    group->addAction(w->findChild<QAction *>("newIntervalPickerAction"));

    if (auto firstAction = group->actions().first())
        firstAction->setChecked(true);

#if 0
    for (auto action: w->getToolBar()->actions())
    {
        QObject::connect(action, &QAction::toggled,
                         w->getToolBar(), [w] ()
                         {
                             //spdlog::info("toolbar update");
                             //QTimer::singleShot(0, w, [w] () { w->getToolBar()->update(); });
                         });
    }
#endif
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
    install_tracker_picker(&plotWidget1);
    install_clickpoint_picker(&plotWidget1);
    install_dragpoint_picker(&plotWidget1);
    install_new_interval_picker(&plotWidget1);
    debug_watch_plot_pickers(plotWidget1.getPlot());
    auto exclusiveActions = group_picker_actions(&plotWidget1);


    const int ReplotInterval = 100;
    QTimer replotTimer;
    QObject::connect(&replotTimer, &QTimer::timeout,
                     plotWidget1.getPlot(), &QwtPlot::replot);

    replotTimer.setInterval(ReplotInterval);
    replotTimer.start();

    return app.exec();
}
