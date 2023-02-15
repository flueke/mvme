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
#include "histo1d_widget.h"
#include <QApplication>
#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include "qwt_plot_curve.h"

[[nodiscard]] std::unique_ptr<Histo1DWidget> make_histo_widget(const std::shared_ptr<Histo1D> &histo)
{
    auto histoWidget = std::make_unique<Histo1DWidget>(histo);
    add_widget_close_action(histoWidget.get());
    histoWidget->setWindowIcon(QIcon(":/window_icon.png"));
    histoWidget->show();
    histoWidget->raise();
    return histoWidget;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    auto make_test_histo0 = []
    {
        static const u32 nBins = 16;
        static const double xMin = 0.0;
        static const double xMax = 16.0;

        auto histo = std::make_shared<Histo1D>(nBins, xMin, xMax);
        histo->setObjectName("Test Histo 0");

        for (int i = 0; i < 4; ++i)
        {
            histo->fill(i, 1.0);
            histo->fill(i, 2.0);
            histo->fill(i, 3.0);
            histo->fill(i, 2.0);
        }
        return histo;
    };

    auto make_test_histo1 = []
    {
        static const u32 nBins = 1024;
        static const double xMin = -10;
        static const double xMax = +10;

        auto histo = std::make_shared<Histo1D>(nBins, xMin, xMax);
        histo->setObjectName("Test Histo 1");

        for (u32 i=0; i<nBins; ++i)
        {
            auto x = histo->getBinCenter(i);
            histo->fill(x, std::sin(x));
        }

        return histo;
    };

    //auto w0 = make_histo_widget(make_test_histo0());
    auto w1 = make_histo_widget(make_test_histo1());


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
