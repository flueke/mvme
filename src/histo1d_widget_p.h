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
#ifndef __HISTO1D_WIDGET_P_H__
#define __HISTO1D_WIDGET_P_H__

#include "histo1d_widget.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <qwt_interval.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_zoneitem.h>

class Histo1DSubRangeDialog: public QDialog
{
    Q_OBJECT
    public:
        using SinkPtr = Histo1DWidget::SinkPtr;
        using HistoSinkCallback = Histo1DWidget::HistoSinkCallback;

        Histo1DSubRangeDialog(const SinkPtr &histoSink,
                              HistoSinkCallback sinkModifiedCallback,
                              double visibleMinX, double visibleMaxX,
                              QWidget *parent = 0);

        virtual void accept() override;

        SinkPtr m_sink;
        HistoSinkCallback m_sinkModifiedCallback;

        double m_visibleMinX;
        double m_visibleMaxX;

        HistoAxisLimitsUI limits_x;
        QDialogButtonBox *buttonBox;
};

#if 0
/* ConditionInterval display and editing:
 *
 * Display is done using a QwtPlotZoneItem to color the interval and two
 * QwtPlotMarkers to show the borders and border coordinates.
 *
 * Editing:
 * Initially the normal zoomer interaction is enabled with the interval shown
 * as described above.  Transition to edit mode is triggered either externally
 * or by the user using a toolbar button or similar. (The h1d widget calls into
 * the IntervalEditor object and tells it to transition.)
 *
 * Invalid intervals are supported for cut creation. In this case the AutoBeginClickPointMachine
 * is used to pick two initial points.
 *
 * Once the interval is valid a QwtPickerDragPointMachine is used to drag one
 * of the interval borders around.
 *
 */

class IntervalPlotPicker: public QwtPlotPicker
{
    Q_OBJECT
    public:
        enum SelectedPointType
        {
            PT_None,
            PT_Min,
            PT_Max
        };

    signals:
        void pointTypeSelected(SelectedPointType pt);

    public:
        explicit IntervalPlotPicker(QWidget *plotCanvas);

        void setInterval(const QwtInterval &interval);
        QwtInterval getInterval() const;

    protected:
        virtual void widgetMousePressEvent(QMouseEvent *) override;
        virtual void widgetMouseReleaseEvent(QMouseEvent *) override;
        virtual void widgetMouseMoveEvent(QMouseEvent *) override;

    private:
        SelectedPointType getPointForXCoordinate(int pixelX);

        bool hasValidInterval() const
        {
            return !std::isnan(m_interval.minValue()) && !std::isnan(m_interval.maxValue());
        }

        QwtInterval m_interval;
        bool m_isDragging;
};

// abstraction for editing a single interval. sets the correct picker on the
// histo1dwidget and shows/hides markers and a plot zone item. also replots the
// histowidget if neccessary.
class IntervalEditor: public QObject
{
    Q_OBJECT
    signals:
        void intervalCreated(const QwtInterval &interval);
        void intervalModified(const QwtInterval &interval);

    public:
        using SelectedPointType = IntervalPlotPicker::SelectedPointType;

        IntervalEditor(Histo1DWidget *histoWidget, QObject *parent = nullptr);
        ~IntervalEditor();

        void setInterval(const QwtInterval &interval);
        QwtInterval getInterval() const;

        void show();
        void hide();
        void newInterval();
        void beginEdit();
        void endEdit();

        bool isVisible() const { return m_zone->isVisible(); }
        bool isEditing() const { return getHistoWidget()->getActivePlotPicker() == m_picker; }

        Histo1DWidget *getHistoWidget() const;
        QwtPlot *getPlot() const;

    private:
        void onPickerPointSelected(const QPointF &point);
        void onPickerPointMoved(const QPointF &point);
        void onPointTypeSelected(IntervalPlotPicker::SelectedPointType pt);
        void replot();
        void setMarker1Value(double x);
        void setMarker2Value(double x);

        Histo1DWidget *m_histoWidget;
        IntervalPlotPicker *m_picker;
        std::unique_ptr<QwtPlotZoneItem> m_zone;
        std::unique_ptr<QwtPlotMarker> m_marker1;
        std::unique_ptr<QwtPlotMarker> m_marker2;
        QwtPlotPicker *m_prevPicker = nullptr;
        QwtInterval m_interval;
        SelectedPointType m_selectedPointType;
};

class IntervalCutDialog: public QDialog
{
    Q_OBJECT
    public:
        explicit IntervalCutDialog(Histo1DWidget *histoWidget);
        ~IntervalCutDialog();

        virtual void accept();
        virtual void reject();
        void apply();

    private:
        Histo1DWidget *m_histoWidget;
        QComboBox *combo_cuts;
        QPushButton *pb_new,
                    *pb_edit;
        QDialogButtonBox *m_bb;

        IntervalEditor *m_editor;
};
#endif

#endif /* __HISTO1D_WIDGET_P_H__ */
