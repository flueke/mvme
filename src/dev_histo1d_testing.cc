/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "histo1d_widget.h"
#include <QApplication>
#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include "qwt_plot_curve.h"

static const u32 nBins = 16;
static const double xMin = 0.0;
static const double xMax = 16.0;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    auto histo = std::make_shared<Histo1D>(nBins, xMin, xMax);

    histo->fill(0.0, 1.0);
    histo->fill(1.0, 2.0);
    histo->fill(2.0, 3.0);
    histo->fill(3.0, 2.0);

    histo->fill(0.0, 1.0);
    histo->fill(1.0, 2.0);
    histo->fill(2.0, 3.0);
    histo->fill(3.0, 2.0);

    histo->fill(0.0, 1.0);
    histo->fill(1.0, 2.0);
    histo->fill(2.0, 3.0);
    histo->fill(3.0, 2.0);

    histo->fill(0.0, 1.0);
    histo->fill(1.0, 2.0);
    histo->fill(2.0, 3.0);
    histo->fill(3.0, 2.0);

    Histo1DWidget histoWidget(histo);
    histoWidget.show();

#if 0
    QFrame settingsWidget;
    auto settingsLayout = new QFormLayout(&settingsWidget);

    auto cb_inverted = new QCheckBox;
    settingsLayout->addRow("Inverted", cb_inverted);

    QObject::connect(cb_inverted, &QCheckBox::toggled,
                     &histoWidget, [&histoWidget] (bool checked) {
        histoWidget.getPlotCurve()->setCurveAttribute(QwtPlotCurve::Inverted, checked);
        histoWidget.replot();
    });

    settingsWidget.show();
#endif

    return app.exec();
}
