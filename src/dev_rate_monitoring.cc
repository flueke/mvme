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
#include "rate_monitor_plot_widget.h"
#include "rate_monitor_samplers.h"
#include "util/typedefs.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFormLayout>
#include <QGroupBox>
#include <QPen>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTimer>
#include <qwt_legend.h>
#include <qwt_curve_fitter.h>
#if QWT_VERSION >= 0x060200
#include <qwt_spline_curve_fitter.h>
#endif
#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <random>

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

    const size_t BufferCapacity1 = 3600;
    const size_t BufferCapacity2 = 750;
    const s32 ReplotPeriod_ms = 1000;
    const s32 NewDataPeriod_ms = 150;
    const s32 NewDataCount = 50;

    // plot data
    auto sampler1 = std::make_shared<RateSampler>();
    sampler1->rateHistory = RateHistoryBuffer(BufferCapacity1);

    auto sampler2 = std::make_shared<RateSampler>();
    sampler2->rateHistory = RateHistoryBuffer(BufferCapacity2);


    // Plot and external legend
    auto plotWidget = new RateMonitorPlotWidget;
    QwtLegend legend;

    QObject::connect(plotWidget->getPlot(), &QwtPlot::legendDataChanged,
                     &legend, &QwtLegend::updateLegend);

    legend.setDefaultItemMode(QwtLegendData::Checkable);

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

        QObject::connect(cb_antiAlias, &QCheckBox::toggled, plotWidget, [=](bool checked) {
            for (auto curve: plotWidget->getPlotCurves())
                curve->setRenderHint(QwtPlotItem::RenderAntialiased, checked);
        });

        QObject::connect(combo_curveStyle, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                         plotWidget, [=](int) {
            for (auto curve: plotWidget->getPlotCurves())
            {
                auto style = static_cast<QwtPlotCurve::CurveStyle>(combo_curveStyle->currentData().toInt());
                curve->setStyle(style);
                if (style == QwtPlotCurve::Lines)
                {
                    curve->setCurveFitter(new QwtSplineCurveFitter);
                }
            }
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
                         plotWidget, [=](int) {
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

        auto pb_clearSampler1 = new QPushButton("clear");
        QObject::connect(pb_clearSampler1, &QPushButton::clicked, plotWidget, [=]() {
            sampler1->clearHistory();
        });

        auto pb_clearSampler2 = new QPushButton("clear");
        QObject::connect(pb_clearSampler2, &QPushButton::clicked, plotWidget, [=]() {
            sampler2->clearHistory();
        });

        auto l = new QFormLayout;
        l->addRow("Antialiasing", cb_antiAlias);
        l->addRow("Curve Style", combo_curveStyle);
        l->addRow("Line Width", spin_penWidth);
        l->addRow("External Legend Position", combo_legendPosition);
        l->addRow("Clear sampler1", pb_clearSampler1);
        l->addRow("Clear sampler2", pb_clearSampler2);

        leftLayout->addLayout(l);
    }

    leftLayout->addStretch(1);

    QWidget mainWidget;
    auto mainLayout = new QHBoxLayout(&mainWidget);
    mainLayout->addWidget(leftWidget);
    mainLayout->addWidget(plotWidget);

    mainWidget.resize(800, 600);
    mainWidget.show();

    //
    // Replot timer
    //
    QTimer replotTimer;
    QObject::connect(&replotTimer, &QTimer::timeout, plotWidget, [&] () {
        plotWidget->replot();

        qDebug("sampler1: lastValue=%lf, lastRate=%lf, lastDelta=%lf, totalSamples=%lf, maxValue=%lf",
               sampler1->lastValue, sampler1->lastRate, sampler1->lastDelta, sampler1->totalSamples, get_max_value(sampler1->rateHistory));
    });

    replotTimer.setInterval(ReplotPeriod_ms);
    replotTimer.start();

    plotWidget->addRateSampler(sampler1, "My Rate 1");
#if 1
    //plotWidget->addRateSampler(sampler2, "My Rate 2", Qt::red);
#endif

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
            sampler1->recordRate(value);
        }
    });

#if 1
    // Fill rateHistory2
    QObject::connect(&fillTimer, &QTimer::timeout, plotWidget, [&] () {
        for (s32 i = 0; i < NewDataCount; i++)
        {
            double value = (std::cos(x2 * 0.25) + SinOffset + 0.5) * SinScale;
            x2 += SinInc + 0.125;
            sampler2->recordRate(value);
        }
    });
#endif

    fillTimer.setInterval(NewDataPeriod_ms);
    fillTimer.start();

    RateMonitorNode rmRoot;

    StreamProcessorSampler streamProcSampler;
    DAQStatsSampler daqStatsSampler;
    SIS3153Sampler sisSampler;

    *rmRoot.putBranch("streamProc") = streamProcSampler.createTree();
    *rmRoot.putBranch("readout") = daqStatsSampler.createTree();
    *rmRoot.putBranch("readout.sis3153") = sisSampler.createTree();

    rmRoot.assertParentChildIntegrity();

    QTextStream qout(stdout);
    dump_tree(qout, rmRoot);

    return app.exec();
}
