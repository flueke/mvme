/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include "analysis/analysis.h"
#include "histo_gui_util.h"

#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QRadioButton>
#include <QVBoxLayout>
#include <qwt_plot.h>
#include <qwt_picker_machine.h>

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
    if (limits_x.rb_limited->isChecked())
    {
        m_sink->m_xLimitMin = limits_x.spin_min->value();
        m_sink->m_xLimitMax = limits_x.spin_max->value();
    }
    else
    {
        m_sink->m_xLimitMin = make_quiet_nan();
        m_sink->m_xLimitMax = make_quiet_nan();
    }

    m_sinkModifiedCallback(m_sink);

    QDialog::accept();
}

//
// CutEditor
//

namespace
{

static const double PlotTextLayerZ  = 1000.0;
static const int CanStartDragDistancePixels = 4;

QwtPlotMarker *make_position_marker()
{
    auto marker = new QwtPlotMarker;
    marker->setLabelAlignment( Qt::AlignLeft | Qt::AlignBottom );
    marker->setLabelOrientation( Qt::Vertical );
    marker->setLineStyle( QwtPlotMarker::VLine );
    marker->setLinePen( Qt::black, 0, Qt::DashDotLine );
    marker->setZ(PlotTextLayerZ);
    //marker->attach(plot);
    //marker->hide();
    return marker;
}

} // end anon ns

IntervalCutEditorPicker::IntervalCutEditorPicker(IntervalCutEditor *cutEditor)
    : QwtPlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
                    QwtPicker::VLineRubberBand, QwtPicker::ActiveOnly,
                    cutEditor->getPlot()->canvas())
    , m_interval(make_quiet_nan(), make_quiet_nan())
{
}

void IntervalCutEditorPicker::setInterval(const QwtInterval &interval)
{
    m_interval = interval;
}

QwtInterval IntervalCutEditorPicker::getInterval() const
{
    return m_interval;
}

void IntervalCutEditorPicker::widgetMousePressEvent(QMouseEvent *ev)
{
    if (std::isnan(m_interval.minValue()) || std::isnan(m_interval.maxValue()))
    {
        QwtPlotPicker::widgetMousePressEvent(ev);
    }
    else
    {
        qDebug() << __PRETTY_FUNCTION__ << "assuming a cut is being edited";
    }
}

void IntervalCutEditorPicker::widgetMouseReleaseEvent(QMouseEvent *ev)
{
    if (std::isnan(m_interval.minValue()) || std::isnan(m_interval.maxValue()))
    {
        QwtPlotPicker::widgetMouseReleaseEvent(ev);
    }
    else
    {
        qDebug() << __PRETTY_FUNCTION__ << "assuming a cut is being edited";
    }
}

void IntervalCutEditorPicker::widgetMouseMoveEvent(QMouseEvent *ev)
{
    if (std::isnan(m_interval.minValue()) || std::isnan(m_interval.maxValue()))
    {
        QwtPlotPicker::widgetMouseMoveEvent(ev);
    }
    else
    {
        int iMinPixel = transform({ m_interval.minValue(), 0.0 }).x();
        int iMaxPixel = transform({ m_interval.maxValue(), 0.0 }).x();
        auto mousePos = ev->pos();

        //if (!m_isDragging)
        {
            if (std::abs(mousePos.x() - iMinPixel) < CanStartDragDistancePixels)
            {
                canvas()->setCursor(Qt::SplitHCursor);
            }
            else if (std::abs(mousePos.x() - iMaxPixel) < CanStartDragDistancePixels)
            {
                canvas()->setCursor(Qt::SplitHCursor);
            }
            else
            {
                canvas()->setCursor(Qt::CrossCursor);
            }
        }
        QwtPlotPicker::widgetMouseMoveEvent(ev);
    }
}

IntervalCutEditor::IntervalCutEditor(Histo1DWidget *parent)
    : QObject(parent)
    , m_histoWidget(parent)
    , m_picker(new IntervalCutEditorPicker(this))
    , m_zone(std::make_unique<QwtPlotZoneItem>())
    , m_marker1(make_position_marker())
    , m_marker2(make_position_marker())
    , m_interval(make_quiet_nan(), make_quiet_nan())
{
    m_picker->setEnabled(false);
    m_zone->attach(parent->getPlot());
    m_marker1->attach(parent->getPlot());
    m_marker2->attach(parent->getPlot());

    // Note: do not call hide() here as that will invoke
    // Histo1DWidget::replot() where the widget might not have been fully
    // constructed yet.
    m_zone->hide();
    m_marker1->hide();
    m_marker2->hide();

    auto sigSelected = static_cast<void (QwtPlotPicker::*) (const QPointF &)>(
        &QwtPlotPicker::selected);

    auto sigMoved = static_cast<void (QwtPlotPicker::*) (const QPointF &)>(
        &QwtPlotPicker::moved);

    connect(m_picker, sigSelected, this, &IntervalCutEditor::onPickerPointSelected);
    connect(m_picker, sigMoved, this, &IntervalCutEditor::onPickerPointMoved);
}

void IntervalCutEditor::setInterval(const QwtInterval &interval)
{
    qDebug() << __PRETTY_FUNCTION__ << "initial interval validity" << interval.isValid();
    m_interval = interval.normalized();

    if (interval.isValid())
    {
        qDebug() << __PRETTY_FUNCTION__ << "got a valid interval";
        // edit a valid interval by dragging one of the borders
        m_picker->setStateMachine(new QwtPickerDragPointMachine);

        m_marker1->setXValue(m_interval.minValue());
        m_marker1->setLabel(QString("    x1=%1").arg(m_interval.minValue()));

        m_marker2->setXValue(m_interval.maxValue());
        m_marker2->setLabel(QString("    x2=%1").arg(m_interval.maxValue()));
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

QwtInterval IntervalCutEditor::getInterval() const
{
    return m_interval;
}

void IntervalCutEditor::show()
{
    if (m_zone->interval().isValid())
        m_zone->show();

    if (!std::isnan(m_marker1->xValue()))
        m_marker1->show();

    if (!std::isnan(m_marker2->xValue()))
        m_marker2->show();

    replot();
}

void IntervalCutEditor::hide()
{
    m_zone->hide();
    m_marker1->hide();
    m_marker2->hide();

    replot();
}

void IntervalCutEditor::newCut()
{
    QwtInterval invalid(make_quiet_nan(), make_quiet_nan());
    setInterval(invalid);
    beginEdit();
}

void IntervalCutEditor::beginEdit()
{
    m_prevPicker = m_histoWidget->getActivePlotPicker();
    m_histoWidget->activatePlotPicker(m_picker);
    show();
}

void IntervalCutEditor::endEdit()
{
    m_histoWidget->activatePlotPicker(m_prevPicker);
    m_prevPicker = nullptr;
    hide();
}

Histo1DWidget *IntervalCutEditor::getHistoWidget() const
{
    return m_histoWidget;
}

QwtPlot *IntervalCutEditor::getPlot() const
{
    return m_histoWidget->getPlot();
}

void IntervalCutEditor::onPickerPointSelected(const QPointF &point)
{
    qDebug() << __PRETTY_FUNCTION__ << point;

    double x = point.x();

    if (std::isnan(m_interval.minValue()))
    {
        assert(std::isnan(m_interval.maxValue()));

        qDebug() << __PRETTY_FUNCTION__ << "setting minValue =" << x;

        m_interval.setMinValue(x);

        m_marker1->setXValue(m_interval.minValue());
        m_marker1->setLabel(QString("    x1=%1").arg(m_interval.minValue()));
        m_marker1->show();

        m_zone->setInterval(m_interval);

        replot();
    }
    else if (std::isnan(m_interval.maxValue()))
    {
        qDebug() << __PRETTY_FUNCTION__ << "setting maxValue =" << x;

        m_interval.setMaxValue(x);
        m_interval = m_interval.normalized();

        m_marker1->setXValue(m_interval.minValue());
        m_marker1->setLabel(QString("    x1=%1").arg(m_interval.minValue()));

        m_marker2->setXValue(m_interval.maxValue());
        m_marker2->setLabel(QString("    x2=%1").arg(m_interval.maxValue()));

        m_zone->setInterval(m_interval);
        m_picker->setInterval(m_interval);

        m_picker->setStateMachine(new QwtPickerDragPointMachine);

        show();
    }
    else
    {
        /* Two points have been picked. This means we transitioned to edit mode
         * using a QwtPickerDragPointMachine. Updates are immediate now and
         * handled in onPickerPointMoved(). */
    }
}

void IntervalCutEditor::onPickerPointMoved(const QPointF &point)
{
    qDebug() << __PRETTY_FUNCTION__ << point;

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
}

void IntervalCutEditor::replot()
{
    m_histoWidget->replot();
}
