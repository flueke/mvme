/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017 mesytec GmbH & Co. KG <info@mesytec.com>
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
