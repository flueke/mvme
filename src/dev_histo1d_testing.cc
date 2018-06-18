#include "histo1d_widget.h"
#include <QApplication>
#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include "qwt_plot_curve.h"

static const u32 nBins = 4;
static const double xMin = 0.0;
static const double xMax = 4.0;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    auto histo = std::make_shared<Histo1D>(nBins, 0.0, 4.0);

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
