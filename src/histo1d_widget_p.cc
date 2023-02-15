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
#include "histo1d_widget_p.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <qwt_picker_machine.h>
#include <qwt_plot.h>

#include "analysis/analysis.h"
#include "histo_gui_util.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"

using namespace analysis;

Histo1DSubRangeDialog::Histo1DSubRangeDialog(const SinkPtr &histoSink,
                                             HistoSinkCallback sinkModifiedCallback,
                                             double visibleMinX, double visibleMaxX,
                                             QWidget *parent)
    : QDialog(parent)
    , m_sink(histoSink)
    , m_sinkModifiedCallback(sinkModifiedCallback)
    , m_visibleMinX(visibleMinX)
    , m_visibleMaxX(visibleMaxX)
{
    setWindowTitle(QSL("Set histogram range"));

    limits_x = make_axis_limits_ui(QSL("Limit X-Axis"),
                                   std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                   visibleMinX, visibleMaxX, histoSink->hasActiveLimits());

    //
    // buttons bottom
    //
    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    //
    // main layout
    //
    auto layout = new QVBoxLayout(this);

    layout->addWidget(limits_x.outerFrame);
    layout->addStretch();
    layout->addWidget(buttonBox);
}

void Histo1DSubRangeDialog::accept()
{
    bool doClear = false;

    if (limits_x.rb_limited->isChecked())
    {
        doClear = (m_sink->m_xLimitMin != limits_x.spin_min->value()
                   || m_sink->m_xLimitMax != limits_x.spin_max->value());

        m_sink->m_xLimitMin = limits_x.spin_min->value();
        m_sink->m_xLimitMax = limits_x.spin_max->value();
    }
    else
    {
        doClear = (!std::isnan(m_sink->m_xLimitMin)
                   || !std::isnan(m_sink->m_xLimitMax));

        m_sink->m_xLimitMin = make_quiet_nan();
        m_sink->m_xLimitMax = make_quiet_nan();
    }

    if (doClear)
        m_sink->clearState();

    m_sinkModifiedCallback(m_sink);

    QDialog::accept();
}

//
// CutEditor
//

#if 0
namespace
{

static const double PlotTextLayerZ  = 1000.0;
static const int CanStartDragDistancePixels = 4;

QwtPlotMarker *make_position_marker()
{
    auto marker = new QwtPlotMarker;
    marker->setLabelAlignment( Qt::AlignLeft | Qt::AlignTop );
    marker->setLabelOrientation( Qt::Vertical );
    marker->setLineStyle( QwtPlotMarker::VLine );
    marker->setLinePen( Qt::black, 0, Qt::DashDotLine );
    marker->setZ(PlotTextLayerZ);
    return marker;
}

} // end anon ns

IntervalPlotPicker::IntervalPlotPicker(QWidget *plotCanvas)
    : QwtPlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
                    QwtPicker::VLineRubberBand, QwtPicker::ActiveOnly,
                    plotCanvas)
    , m_interval(make_quiet_nan(), make_quiet_nan())
    , m_isDragging(false)
{
}

void IntervalPlotPicker::setInterval(const QwtInterval &interval)
{
    m_interval = interval;
}

QwtInterval IntervalPlotPicker::getInterval() const
{
    return m_interval;
}

void IntervalPlotPicker::widgetMousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton) return;

    if (!hasValidInterval())
    {
        QwtPlotPicker::widgetMousePressEvent(ev);
    }
    else
    {
        qDebug() << __PRETTY_FUNCTION__ << "have a valid interval -> cut is being edited";

        switch (getPointForXCoordinate(ev->pos().x()))
        {
            case PT_None:
                break;

            case PT_Min:
                m_isDragging = true;
                emit pointTypeSelected(PT_Min);
                QwtPlotPicker::widgetMousePressEvent(ev);
                break;

            case PT_Max:
                m_isDragging = true;
                emit pointTypeSelected(PT_Max);
                QwtPlotPicker::widgetMousePressEvent(ev);
                break;
        }
    }
}

void IntervalPlotPicker::widgetMouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton) return;

    if (!hasValidInterval())
    {
        QwtPlotPicker::widgetMouseReleaseEvent(ev);
    }
    else
    {
        qDebug() << __PRETTY_FUNCTION__ << "have a valid interval -> cut is being edited";
        m_isDragging = false;
        canvas()->setCursor(Qt::CrossCursor);
        QwtPlotPicker::widgetMouseReleaseEvent(ev);
    }
}

/* When dragging min and max may have to be swapped for the zone item to show
 * the correct area. One point is picked and dragged, the other remains fixed.
 * The cut editor has to know which point is being moved and which is the fixed one.
 * It can then make sure they're in the correct order for the zone item.
 */
void IntervalPlotPicker::widgetMouseMoveEvent(QMouseEvent *ev)
{
    if (hasValidInterval() && !m_isDragging)
    {
        switch (getPointForXCoordinate(ev->pos().x()))
        {
            case PT_None:
                canvas()->setCursor(Qt::CrossCursor);
                break;

            case PT_Min:
            case PT_Max:
                canvas()->setCursor(Qt::SplitHCursor);
                break;
        }
    }

    QwtPlotPicker::widgetMouseMoveEvent(ev);
}

IntervalPlotPicker::SelectedPointType IntervalPlotPicker::getPointForXCoordinate(int pixelX)
{
    int iMinPixel = transform({ m_interval.minValue(), 0.0 }).x();
    int iMaxPixel = transform({ m_interval.maxValue(), 0.0 }).x();

    if (std::abs(pixelX - iMinPixel) < CanStartDragDistancePixels)
    {
        return PT_Min;
    }
    else if (std::abs(pixelX - iMaxPixel) < CanStartDragDistancePixels)
    {
        return PT_Max;
    }

    return PT_None;
}

IntervalEditor::IntervalEditor(Histo1DWidget *histoWidget, QObject *parent)
    : QObject(parent)
    , m_histoWidget(histoWidget)
    , m_picker(new IntervalPlotPicker(histoWidget->getPlot()->canvas()))
    , m_zone(std::make_unique<QwtPlotZoneItem>())
    , m_marker1(make_position_marker())
    , m_marker2(make_position_marker())
    , m_interval(make_quiet_nan(), make_quiet_nan())
    , m_selectedPointType(SelectedPointType::PT_None)
{
    m_picker->setEnabled(false);
    auto zoneBrush = m_zone->brush();
    zoneBrush.setStyle(Qt::DiagCrossPattern);
    m_zone->setBrush(zoneBrush);

    // QwtPlot by default deletes any attached items when it is destroyed.
    m_zone->attach(histoWidget->getPlot());
    m_marker1->attach(histoWidget->getPlot());
    m_marker2->attach(histoWidget->getPlot());

    // Note: do not call this->hide() here as that will invoke
    // Histo1DWidget::replot() where the widget might not have been fully
    // constructed yet.
    m_zone->hide();
    m_marker1->hide();
    m_marker2->hide();

    auto sigSelected = static_cast<void (QwtPlotPicker::*) (const QPointF &)>(
        &QwtPlotPicker::selected);

    auto sigMoved = static_cast<void (QwtPlotPicker::*) (const QPointF &)>(
        &QwtPlotPicker::moved);

    connect(m_picker, sigSelected, this, &IntervalEditor::onPickerPointSelected);
    connect(m_picker, sigMoved, this, &IntervalEditor::onPickerPointMoved);
    connect(m_picker, &IntervalPlotPicker::pointTypeSelected,
            this, &IntervalEditor::onPointTypeSelected);
}

IntervalEditor::~IntervalEditor()
{
    qDebug() << __PRETTY_FUNCTION__ << this;

    if (isEditing())
    {
        endEdit();
    }

    m_zone->detach();
    m_marker1->detach();
    m_marker2->detach();

    delete m_picker;
}

void IntervalEditor::setInterval(const QwtInterval &interval)
{
    qDebug() << __PRETTY_FUNCTION__ << "initial interval validity" << interval.isValid();
    m_interval = interval.normalized();

    if (interval.isValid())
    {
        qDebug() << __PRETTY_FUNCTION__ << "got a valid interval";
        // edit a valid interval by dragging one of the borders
        m_picker->setStateMachine(new QwtPickerDragPointMachine);

        setMarker1Value(m_interval.minValue());
        setMarker2Value(m_interval.maxValue());
    }
    else
    {
        m_marker1->setXValue(make_quiet_nan());
        m_marker2->setXValue(make_quiet_nan());
        // create a new interval by clicking two points
        m_picker->setStateMachine(new AutoBeginClickPointMachine);
        hide();
    }

    m_picker->setInterval(m_interval);
    m_zone->setInterval(m_interval);

}

QwtInterval IntervalEditor::getInterval() const
{
    return m_interval;
}

void IntervalEditor::show()
{
    if (m_zone->interval().isValid())
        m_zone->show();

    if (!std::isnan(m_marker1->xValue()))
        m_marker1->show();

    if (!std::isnan(m_marker2->xValue()))
        m_marker2->show();

    replot();
}

void IntervalEditor::hide()
{
    m_zone->hide();
    m_marker1->hide();
    m_marker2->hide();

    replot();
}

void IntervalEditor::newInterval()
{
    QwtInterval invalid(make_quiet_nan(), make_quiet_nan());
    setInterval(invalid);
    beginEdit();
}

void IntervalEditor::beginEdit()
{
    auto picker = m_histoWidget->getActivePlotPicker();

    if (picker != m_picker)
    {
        qDebug() << __PRETTY_FUNCTION__ << "setting prev picker to" << picker
            << "and activating IntervalPlotPicker";
        m_prevPicker = picker;
        m_histoWidget->activatePlotPicker(m_picker);
    }
    show();
}

void IntervalEditor::endEdit()
{
    qDebug() << __PRETTY_FUNCTION__;

    if (m_prevPicker)
    {
        qDebug() << __PRETTY_FUNCTION__ << "restoring prev picker" << m_prevPicker;
        m_histoWidget->activatePlotPicker(m_prevPicker);
        m_prevPicker = nullptr;
    }
}

Histo1DWidget *IntervalEditor::getHistoWidget() const
{
    return m_histoWidget;
}

QwtPlot *IntervalEditor::getPlot() const
{
    return m_histoWidget->getPlot();
}

void IntervalEditor::onPickerPointSelected(const QPointF &point)
{
    qDebug() << __PRETTY_FUNCTION__ << point;

    double x = point.x();

    if (std::isnan(m_interval.minValue()))
    {
        /* The user selected the first point. */
        assert(std::isnan(m_interval.maxValue()));

        qDebug() << __PRETTY_FUNCTION__ << "setting minValue =" << x;

        m_interval.setMinValue(x);

        setMarker1Value(m_interval.minValue());

        m_zone->setInterval(m_interval);

        replot();
    }
    else if (std::isnan(m_interval.maxValue()))
    {
        /* The 2nd point has been selected. Update markers and interval and
         * transition to edit mode. */
        qDebug() << __PRETTY_FUNCTION__ << "setting maxValue =" << x;

        m_interval.setMaxValue(x);
        m_interval = m_interval.normalized();

        setMarker1Value(m_interval.minValue());
        setMarker2Value(m_interval.maxValue());

        m_zone->setInterval(m_interval);
        m_picker->setInterval(m_interval);

        m_picker->setStateMachine(new QwtPickerDragPointMachine);

        qDebug() << this << "emit intervalCreated";
        emit intervalCreated(m_interval);

        show();
    }
    else
    {
        /* Two points have been picked before. This means we transitioned to
         * edit mode, the user picked and dragged a point and released the
         * mouse. */

        using SPT = IntervalPlotPicker::SelectedPointType;

        QwtInterval newInterval = m_interval;

        switch (m_selectedPointType)
        {
            case SPT::PT_Min:
                newInterval.setMinValue(x);
                break;

            case SPT::PT_Max:
                newInterval.setMaxValue(x);
                break;

            InvalidDefaultCase;
        }

        newInterval = newInterval.normalized();

        if (newInterval != m_interval)
        {
            qDebug() << this << "emit intervalModified";
            emit intervalModified(m_interval);
        }

        m_interval = newInterval;
        m_picker->setInterval(m_interval);
        m_zone->setInterval(m_interval);
        setMarker1Value(m_interval.minValue());
        setMarker2Value(m_interval.maxValue());
        m_selectedPointType = SPT::PT_None;
        show();
    }
}

void IntervalEditor::onPointTypeSelected(IntervalPlotPicker::SelectedPointType pt)
{
    using SPT = IntervalPlotPicker::SelectedPointType;

    QString ptstr;

    switch (pt)
    {
        case SPT::PT_None:
            ptstr = "None";
            break;

        case SPT::PT_Min:
            m_marker1->hide();
            ptstr = "Min";
            break;

        case SPT::PT_Max:
            m_marker2->hide();
            ptstr = "Max";
            break;

    }
    qDebug() << __PRETTY_FUNCTION__ << "selected point type:" << ptstr;

    m_selectedPointType = pt;
}

void IntervalEditor::onPickerPointMoved(const QPointF &point)
{
    using SPT = IntervalPlotPicker::SelectedPointType;

    double x = point.x();

    if (std::isnan(m_interval.maxValue()))
    {
        /* One point has been selected, the other is not set yet. Adjust the
         * zone item to highlight the area between the known point and the
         * point under the mouse cursor. */

        auto zoneInterval = m_interval;
        zoneInterval.setMaxValue(x);
        zoneInterval = zoneInterval.normalized();
        m_zone->setInterval(zoneInterval);
        m_zone->show();
        replot();
    }
    else if (m_selectedPointType != SPT::PT_None)
    {
        auto zoneInterval = m_interval;

        switch (m_selectedPointType)
        {
            case SPT::PT_Min:
                zoneInterval.setMinValue(x);
                break;

            case SPT::PT_Max:
                zoneInterval.setMaxValue(x);
                break;

            InvalidDefaultCase;
        }

        zoneInterval = zoneInterval.normalized();
        m_zone->setInterval(zoneInterval);
        replot();
    }
}

void IntervalEditor::replot()
{
    m_histoWidget->replot();
}

void IntervalEditor::setMarker1Value(double x)
{
    m_marker1->setXValue(x);
    m_marker1->setLabel(QString("    x1=%1").arg(x));
    m_marker1->show();
}

void IntervalEditor::setMarker2Value(double x)
{
    m_marker2->setXValue(x);
    m_marker2->setLabel(QString("    x2=%1").arg(x));
    m_marker2->show();
}

//
// IntervalCutDialog
//
IntervalCutDialog::IntervalCutDialog(Histo1DWidget *histoWidget)
    : QDialog(histoWidget)
    , m_histoWidget(histoWidget)
    , combo_cuts(new QComboBox(this))
    , pb_new(new QPushButton(QIcon(QSL(":/scissors_plus.png")), QSL("New Cut"), this))
    , pb_edit(new QPushButton(QIcon(QSL(":/pencil.png")), QSL("Edit Cut"), this))
    , m_bb(new QDialogButtonBox(this))
    , m_editor(new IntervalEditor(histoWidget, this))
{
    auto ss = QSL("font-size: 8pt;");
    qDebug() << __PRETTY_FUNCTION__ << ss;
    setStyleSheet(ss);

    setWindowTitle("Interval Cuts");

    combo_cuts->setEditable(false);

    m_bb->setStandardButtons(QDialogButtonBox::Ok
                             | QDialogButtonBox::Cancel
                             | QDialogButtonBox::Apply);

    auto contentLayout = new QGridLayout;

    // first row: only "Select Cut" label
    // second row: non-editable combo | edit cut | new cut
    //
    // on new cut: histo gets focus, user clicks once, moves mouse, clicks again
    // and has created an interval. then a popup dialog prompting for the new
    // cuts name pops up. giving a valid name and accepting the popup dialog
    // creates the cut object and selects it in the combo. we're now in "cut
    // display mode". zooming is enabled in the histo widget.
    //
    // XXX: would it be more elegant to not have to use a popup menu and instead
    // use the combo in editable mode somehow to get the new cuts name?

    // first row: label and cut selection combo spanning two columns
    // second row: New and Edit buttons
    contentLayout->addWidget(new QLabel("Selected Cut"), 0, 0);
    contentLayout->addWidget(combo_cuts, 1, 0);
    contentLayout->addWidget(pb_edit, 1, 1);
    contentLayout->addWidget(pb_new,  1, 2);

    auto dialogLayout = new QVBoxLayout(this);
    dialogLayout->addLayout(contentLayout);
    dialogLayout->addStretch(1);
    dialogLayout->addWidget(m_bb);

    //
    // interactions
    //
    auto new_cut = [this] ()
    {
        m_editor->newInterval();
    };

    auto on_interval_created = [this] (const QwtInterval &interval)
    {
        QString cutName = QSL("New Cut");

        // cut name dialog
        {
            auto le_cutName = new QLineEdit;
            le_cutName->setText(cutName);

            auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

            QDialog dialog(m_histoWidget);
            auto layout = new QFormLayout(&dialog);
            layout->addRow("Cut Name", le_cutName);
            layout->addRow(buttons);

            QObject::connect(buttons, &QDialogButtonBox::accepted,
                             &dialog, &QDialog::accept);

            QObject::connect(buttons, &QDialogButtonBox::rejected,
                             &dialog, &QDialog::reject);

            if (dialog.exec() == QDialog::Rejected)
            {
                m_editor->hide();
                //m_q->replot();
                return;
            }

            cutName = le_cutName->text();
        }

        // FIXME: this should not be here. instead a function like
        // create_interval_cut_from_histowidget() should be called here.
        auto serviceProvider = m_histoWidget->getServiceProvider();
        auto sink = m_histoWidget->getSink();

        assert(serviceProvider);
        assert(sink);

        if (!serviceProvider || !sink)
        {
            InvalidCodePath;
            // TODO: display an error
            return;
        }

        // Create the IntervalCondition analysis object. The number of intervals will
        // be the same as the number of histograms in the Histo1DSink belonging to
        // the histogram currently being displayed. Each interval of the condition
        // will be set to the current intervals values.
        QVector<QwtInterval> intervals(sink->getNumberOfHistos(), interval);
        auto cond = std::make_shared<analysis::IntervalCondition>();
        cond->setIntervals(intervals);
        cond->setObjectName(cutName);

        auto xInput = sink->getSlot(0)->inputPipe;
        auto xIndex = sink->getSlot(0)->paramIndex;

        AnalysisPauser pauser(serviceProvider);
        cond->connectInputSlot(0, xInput, xIndex);

        serviceProvider->getAnalysis()->addOperator(sink->getEventId(), 0, cond);
    };

    connect(m_bb, &QDialogButtonBox::clicked,
            this, [this] (QAbstractButton *button) {
        switch (m_bb->buttonRole(button))
        {
            case QDialogButtonBox::AcceptRole:
                accept();
                break;
            case QDialogButtonBox::RejectRole:
                reject();
                break;
            case QDialogButtonBox::ApplyRole:
                apply();
                break;
            InvalidDefaultCase;
        }
    });

    connect(pb_new, &QPushButton::clicked, this, new_cut);
    connect(m_editor, &IntervalEditor::intervalCreated, this, on_interval_created);

    //connect(combo_cuts, static_cast<void (QComboBox::*) (int index)>(
    //        &QComboBox::currentIndexChanged),
}

IntervalCutDialog::~IntervalCutDialog()
{
    qDebug() << __PRETTY_FUNCTION__ << this;
}

void IntervalCutDialog::accept()
{
    QDialog::accept();
    close();
}

void IntervalCutDialog::reject()
{
    QDialog::reject();
    close();
}

void IntervalCutDialog::apply()
{
}
#endif
