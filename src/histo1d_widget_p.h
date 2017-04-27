#ifndef __HISTO1D_WIDGET_P_H__
#define __HISTO1D_WIDGET_P_H__

#include "histo1d_widget.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
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

/* A picker machine that starts a point selection as soon as the mouse moves
 * inside the canvas. On releasing mouse button 1 the point is selected and
 * picking ends.
 */
class AutoBeginClickPointMachine: public QwtPickerMachine
{
    public:
        static const int StateInitial   = 0;
        static const int StatePickPoint = 1;

        AutoBeginClickPointMachine()
            : QwtPickerMachine(PointSelection)
        {
            setState(StateInitial);
        }

        virtual QList<QwtPickerMachine::Command> transition(const QwtEventPattern &eventPattern, const QEvent *ev) override
        {
            QList<QwtPickerMachine::Command> cmdList;

            switch (ev->type())
            {
                case QEvent::Enter:
                case QEvent::MouseMove:
                    {
                        if (state() == StateInitial)
                        {
                            cmdList += Begin;
                            cmdList += Append;
                            setState(StatePickPoint);
                        }
                        else // StatePickPoint
                        {
                            // Moves the last point appended to the point
                            // selection to the current cursor position.
                            cmdList += Move;
                        }
                    } break;

                case QEvent::MouseButtonRelease:
                    {
                        // End selection in mouse button 1 release
                        if (eventPattern.mouseMatch(QwtEventPattern::MouseSelect1,
                                                    reinterpret_cast<const QMouseEvent *>(ev)))
                        {
                            if (state() == StatePickPoint)
                            {
                                cmdList += End;
                                setState(StateInitial);
                            }
                        }
                    } break;

                default:
                    break;
            }

            return cmdList;
        }
};

#endif /* __HISTO1D_WIDGET_P_H__ */
