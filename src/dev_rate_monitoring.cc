#include "rate_monitoring.h"

#include <qwt_plot_curve.h>
#include <QApplication>
#include <QDebug>
#include <QTimer>
#include <QBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <random>
#include "util/typedefs.h"
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <iostream>

namespace pt = boost::property_tree;
using std::cout;
using std::endl;

static void dump_tree(const pt::ptree &tree, int indent = 0)
{
    for (auto it = tree.ordered_begin(); it != tree.not_found(); it++)
    {
        for (s32 i = 0; i < indent; i++) cout << "  ";

        if (it->second.empty())
        {
            cout << it->first << " -> " << it->second.get_value<bool>(false) << endl;
        }
        else
        {
            cout << it->first << " ->" << endl;
            dump_tree(it->second, indent + 1);
        }
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const size_t BufferCapacity = 1000;
    const size_t BufferCapacity2 = 750;
    const s32 ReplotPeriod_ms = 1000;
    const s32 NewDataPeriod_ms = 250;
    const s32 NewDataCount = 10;

    auto rateHistory = std::make_shared<RateHistoryBuffer>(BufferCapacity);
    auto rateHistory2 = std::make_shared<RateHistoryBuffer>(BufferCapacity2);

    // Plot and external legend
    auto plotWidget = new RateMonitorPlotWidget;
    QwtLegend legend;

    QObject::connect(plotWidget->getPlot(), &QwtPlot::legendDataChanged,
                     &legend, &QwtLegend::updateLegend);

    legend.setDefaultItemMode(QwtLegendData::Checkable);


    // set plot data and show widgets
    plotWidget->addRate(rateHistory, "My Rate 1");
    plotWidget->addRate(rateHistory2, "My Rate 2", Qt::red);

    auto leftWidget = new QWidget;
    auto leftLayout = new QVBoxLayout(leftWidget);

    // y scale
    auto gb_scale = new QGroupBox("Y Scale");
    {
        auto rb_scaleLin = new QRadioButton;
        auto rb_scaleLog = new QRadioButton;

        QObject::connect(rb_scaleLin, &QRadioButton::toggled, gb_scale, [=](bool checked) {
            if (checked) plotWidget->setYAxisScale(AxisScale::Linear);
        });

        QObject::connect(rb_scaleLog, &QRadioButton::toggled, gb_scale, [=](bool checked) {
            if (checked) plotWidget->setYAxisScale(AxisScale::Logarithmic);
        });

        rb_scaleLin->setChecked(true);

        auto l = new QFormLayout(gb_scale);
        l->addRow("Linear", rb_scaleLin);
        l->addRow("Logarithmic", rb_scaleLog);

        leftLayout->addWidget(gb_scale);
    }

    // misc options
    {
        auto cb_xAxisReversed = new QCheckBox;
        auto cb_antiAlias = new QCheckBox;
        auto combo_curveStyle = new QComboBox;

        combo_curveStyle->addItem("Lines", QwtPlotCurve::Lines);
        combo_curveStyle->addItem("Sticks", QwtPlotCurve::Sticks);
        combo_curveStyle->addItem("Steps", QwtPlotCurve::Steps);
        combo_curveStyle->addItem("Dots", QwtPlotCurve::Dots);

        auto spin_penWidth = new QSpinBox;
        spin_penWidth->setMinimum(1);
        spin_penWidth->setMaximum(10);

        auto combo_legendPosition = new QComboBox;
        combo_legendPosition->addItem("None", -1);
        combo_legendPosition->addItem("Left", QwtPlot::LeftLegend);
        combo_legendPosition->addItem("Right", QwtPlot::RightLegend);
        combo_legendPosition->addItem("Bottom", QwtPlot::BottomLegend);
        combo_legendPosition->addItem("Top", QwtPlot::TopLegend);

        QObject::connect(cb_xAxisReversed, &QCheckBox::toggled, plotWidget, [=](bool checked) {
            plotWidget->setXAxisReversed(checked);
        });

        QObject::connect(cb_antiAlias, &QCheckBox::toggled, plotWidget, [=](bool checked) {
            for (auto curve: plotWidget->getPlotCurves())
                curve->setRenderHint(QwtPlotItem::RenderAntialiased, checked);
        });

        QObject::connect(combo_curveStyle, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                         plotWidget, [=](int index) {
            for (auto curve: plotWidget->getPlotCurves())
                curve->setStyle(static_cast<QwtPlotCurve::CurveStyle>(combo_curveStyle->currentData().toInt()));
        });

        QObject::connect(spin_penWidth, static_cast<void (QSpinBox::*) (int)>(&QSpinBox::valueChanged),
                         plotWidget, [=](int width) {
            for (auto curve: plotWidget->getPlotCurves())
            {
                auto pen = curve->pen();
                pen.setWidth(width);
                curve->setPen(pen);
            }
         });

        QObject::connect(combo_legendPosition, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                         plotWidget, [=](int index) {
            int data = combo_legendPosition->currentData().toInt();
            if (data < 0)
            {
                plotWidget->getPlot()->insertLegend(nullptr);
            }
            else
            {
                plotWidget->getPlot()->insertLegend(
                    new QwtLegend,
                    static_cast<QwtPlot::LegendPosition>(data));
            }
        });

        auto l = new QFormLayout;
        l->addRow("Reverse X", cb_xAxisReversed);
        l->addRow("Antialiasing", cb_antiAlias);
        l->addRow("Curve Style", combo_curveStyle);
        l->addRow("Line Width", spin_penWidth);
        l->addRow("External Legend Position", combo_legendPosition);

        leftLayout->addLayout(l);
    }

    leftLayout->addStretch(1);

    QWidget mainWidget;
    auto mainLayout = new QHBoxLayout(&mainWidget);
    mainLayout->addWidget(leftWidget);
    mainLayout->addWidget(plotWidget);

    mainWidget.show();

    //
    // Replot timer
    //
    QTimer replotTimer;
    QObject::connect(&replotTimer, &QTimer::timeout, plotWidget, [&] () {
        plotWidget->replot();
    });

    replotTimer.setInterval(ReplotPeriod_ms);
    replotTimer.start();

    //
    // Fill timer
    //
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(0.0, 15.0);

    static const double SinOffset = 1.0;
    static const double SinScale = 1.0;
    static const double SinInc = 0.10;
    double x1 = 0.0;
    double x2 = 0.0;

    QTimer fillTimer;
    // Fill rateHistory1
    QObject::connect(&fillTimer, &QTimer::timeout, plotWidget, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            //double value = dist(gen);
            double value = (std::sin(x1 * 0.25) + SinOffset) * SinScale;
            x1 += SinInc;
            rateHistory->push_back(value);
        }
    });

    // Fill rateHistory2
    QObject::connect(&fillTimer, &QTimer::timeout, plotWidget, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = (std::cos(x2 * 0.25) + SinOffset) * SinScale;
            x2 += SinInc;
            rateHistory2->push_back(value);
        }
    });

    fillTimer.setInterval(NewDataPeriod_ms);
    fillTimer.start();



#if 0
    // ====================================================
    // boost property tree test
    // ====================================================

    pt::ptree tree;

    tree.put("readout.bytes", true);
    tree.put("readout.buffers", true);

    tree.put("streamProc.bytes", false);
    tree.put("streamProc.events.0", true);
    tree.put("streamProc.events.1", false);

    tree.put("streamProc.modules.0.0", true);
    tree.put("streamProc.modules.0.1", false);

#if 1
    for (auto it = tree.begin(); it != tree.end(); it++)
    {
        cout << it->first << " -> " << it->second.get_value<bool>(false) << endl;
    }

    cout << endl << endl;
#endif

    dump_tree(tree);

    pt::write_json(cout, tree);

    cout << endl << endl;
    {
        auto child = tree.get_child("streamProc.modules.0.0", {});

        cout << "a child: " << child.get_value<bool>() << endl;
    }

    //cout << tree.find("streamProc.modules.0.0")->second.get_value<bool>(false) << endl;
    //cout << tree.find("streamProc.modules.0.1")->second.get_value<bool>(false) << endl;
#endif

    RateMonitorNode rmRoot;

    StreamProcessorSampler streamProcSampler;
    rmRoot.addDirectChild("streamProc", streamProcSampler.createTree());
    //rmRoot.createBranch("streamProc", streamProcSampler.createTree());

    QTextStream qout(stdout);
    dump_tree(qout, rmRoot);

    return app.exec();
}
