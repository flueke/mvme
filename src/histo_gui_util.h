#ifndef __MVME_HISTO_GUI_UTIL_H__
#define __MVME_HISTO_GUI_UTIL_H__

#include <QEvent>
#include <QSlider>
#include <qwt_picker_machine.h>
#include <qwt_plot_picker.h>

QSlider *make_res_reduction_slider(QWidget *parent = nullptr);

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

        virtual QList<QwtPickerMachine::Command> transition(const QwtEventPattern &eventPattern,
                                                            const QEvent *ev) override
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
                        // End selection on mouse button 1 release
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

#endif /* __MVME_HISTO_GUI_UTIL_H__ */
