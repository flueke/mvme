#ifndef __HISTO1D_WIDGET_P_H__
#define __HISTO1D_WIDGET_P_H__

#include "histo1d_widget.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <qwt_picker_machine.h>
#include <qwt_plot_picker.h>

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
class MovePickerMachine: public QwtPickerMachine
{
    public:
        virtual QwtPickerMachine::CommandList transition(const QwtEventPattern &, const QEvent *ev) override
        {
            QwtPickerMachine::CommandList cmdList;

            if (ev->type() == QEvent::MouseMove)
            {
                cmdList += Move;
            }

            return cmdList;
        }
};
#endif

class AutoBeginPlotPicker: public QwtPlotPicker
{
    Q_OBJECT
    public:
        AutoBeginPlotPicker(int xAxis, int yAxis, RubberBand rubberBand, DisplayMode trackerMode, QWidget *canvas)
            : QwtPlotPicker(xAxis, yAxis, rubberBand, trackerMode, canvas)
        {
            qDebug() << __PRETTY_FUNCTION__;
            canvas->setMouseTracking(true);
        }

        virtual void widgetMouseMoveEvent(QMouseEvent *ev) override
        {
            if (!isActive())
            {
                begin();
                //append(e->pos());
            }
            QwtPlotPicker::widgetMouseMoveEvent(ev);
        }
};

#endif /* __HISTO1D_WIDGET_P_H__ */
