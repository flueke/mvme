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
#include "histo_util.h"
#include "qt_util.h"
#include "histo1d.h"
#include "histo2d.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QRadioButton>

const s64 AxisBinning::Underflow;
const s64 AxisBinning::Overflow;

QString makeAxisTitle(const QString &title, const QString &unit)
{
    QString result;
    if (!title.isEmpty())
    {
        result = title;
        if (!unit.isEmpty())
        {
            result += QString(" [%1]").arg(unit);
        }
    }

    return result;
}

void select_by_resolution(QComboBox *combo, s32 selectedRes)
{
    s32 minBits = 0;
    s32 selectedBits = std::log2(selectedRes);

    if (selectedBits > 0)
    {
        s32 index = selectedBits - minBits - 1;
        index = std::min(index, combo->count() - 1);
        combo->setCurrentIndex(index);
    }
}

QComboBox *make_resolution_combo(s32 minBits, s32 maxBits, s32 selectedBits)
{
    QComboBox *result = new QComboBox;

    for (s32 bits = minBits;
         bits <= maxBits;
         ++bits)
    {
        s32 value = 1 << bits;

        QString text = QString("%1, %2 bit").arg(value, 4).arg(bits, 2);

        result->addItem(text, value);
    }

    select_by_resolution(result, 1 << selectedBits);

    return result;
}

/* inputMin and inputMax are used as the min and max possible values for the spin boxes.
 *
 * limitMin and limitMax are the values used to populate both spinboxes.
 *
 * isLimtied indicates whether limiting is currently in effect and affects
 * which radio button is initially active.
 */
HistoAxisLimitsUI make_axis_limits_ui(const QString &limitButtonTitle, double inputMin, double inputMax,
                                      double limitMin, double limitMax, bool isLimited)
{
    HistoAxisLimitsUI result = {};
    result.rb_limited = new QRadioButton(limitButtonTitle);
    result.rb_fullRange = new QRadioButton(QSL("Full Range"));

    result.spin_min = new QDoubleSpinBox;
    result.spin_max = new QDoubleSpinBox;

    result.limitFrame = new QFrame;
    result.limitFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

    auto limitFrameLayout = new QFormLayout(result.limitFrame);
    limitFrameLayout->setContentsMargins(2, 2, 2, 2);
    limitFrameLayout->addRow(QSL("Min"), result.spin_min);
    limitFrameLayout->addRow(QSL("Max"), result.spin_max);

    result.outerFrame = new QFrame;
    auto outerFrameLayout = new QVBoxLayout(result.outerFrame);
    outerFrameLayout->setContentsMargins(4, 4, 4, 4);
    outerFrameLayout->addWidget(result.rb_limited);
    outerFrameLayout->addWidget(result.limitFrame);
    outerFrameLayout->addWidget(result.rb_fullRange);

    QObject::connect(result.rb_limited, &QAbstractButton::toggled, result.outerFrame, [result] (bool checked) {
        result.limitFrame->setEnabled(checked);
    });

    result.rb_limited->setChecked(true);

    if (!isLimited)
    {
        result.rb_fullRange->setChecked(true);
    }

    result.spin_min->setMinimum(inputMin);
    result.spin_min->setMaximum(inputMax);

    result.spin_max->setMinimum(inputMin);
    result.spin_max->setMaximum(inputMax);

    if (!std::isnan(limitMin))
        result.spin_min->setValue(limitMin);

    if (!std::isnan(limitMax))
        result.spin_max->setValue(limitMax);

    return result;
}

Histo1DPtr make_x_projection(Histo2D *histo)
{
    return make_projection(histo, Qt::XAxis);
}

Histo1DPtr make_x_projection(Histo2D *histo,
                             double startX, double endX,
                             double startY, double endY)
{
    return make_projection(histo, Qt::XAxis, startX, endX, startY, endY);
}

Histo1DPtr make_y_projection(Histo2D *histo)
{
    return make_projection(histo, Qt::YAxis);
}

Histo1DPtr make_y_projection(Histo2D *histo,
                             double startX, double endX,
                             double startY, double endY)
{
    return make_projection(histo, Qt::YAxis, startX, endX, startY, endY);
}

std::shared_ptr<Histo1D> make_projection(Histo2D *histo, Qt::Axis axis)
{
    auto projBinning  = histo->getAxisBinning(axis);
    auto otherBinning = histo->getAxisBinning(axis == Qt::XAxis ? Qt::YAxis : Qt::XAxis);

    return make_projection(histo, axis,
                           projBinning.getMin(), projBinning.getMax(),
                           otherBinning.getMin(), otherBinning.getMax());
}

std::shared_ptr<Histo1D> make_projection(Histo2D *histo, Qt::Axis axis,
                                         double startX, double endX,
                                         double startY, double endY)
{
    //qDebug() << __PRETTY_FUNCTION__
    //    << axis
    //    << "startX" << startX << "endX" << endX
    //    << "startY" << startY << "endY" << endY;


    double projStart = (axis == Qt::XAxis ? startX : startY);
    double projEnd   = (axis == Qt::XAxis ? endX : endY);

    double otherStart = (axis == Qt::XAxis ? startY : startX);
    double otherEnd   = (axis == Qt::XAxis ? endY : endX);

    auto projBinning  = histo->getAxisBinning(axis);
    auto otherBinning = histo->getAxisBinning(axis == Qt::XAxis ? Qt::YAxis : Qt::XAxis);

    s64 projStartBin = projBinning.getBinBounded(projStart);
    s64 projEndBin   = projBinning.getBinBounded(projEnd) + 1;

    s64 otherStartBin = otherBinning.getBinBounded(otherStart);
    s64 otherEndBin   = otherBinning.getBinBounded(otherEnd) + 1;

    s64 nProjBins = (projEndBin - projStartBin);

    //qDebug() << __PRETTY_FUNCTION__
    //    << "projEndBin" << projEndBin
    //    << "otherEndBin" << otherEndBin
    //    << "nProjBins" << nProjBins;


    // adjust start and end to low edge of corresponding bin
    projStart = projBinning.getBinLowEdge(projStartBin);
    projEnd   = projBinning.getBinLowEdge(projEndBin);

    auto result = std::make_shared<Histo1D>(nProjBins, projStart, projEnd);
    result->setAxisInfo(Qt::XAxis, histo->getAxisInfo(axis));
    result->setObjectName(histo->objectName()
                          + (axis == Qt::XAxis ? QSL(" X") : QSL(" Y"))
                          + QSL(" Projection"));

    u32 destBin = 0;

    for (u32 binI = projStartBin;
         binI < projEndBin;
         ++binI)
    {
        double value = 0.0;

        for (u32 binJ = otherStartBin;
             binJ < otherEndBin;
             ++binJ)
        {
            if (axis == Qt::XAxis)
            {
                value += histo->getBinContent(binI, binJ);
            }
            else
            {
                value += histo->getBinContent(binJ, binI);
            }
        }

        // Using 'value' for the entry count too, assuming that the bin value in
        // the original histogram resulted from incrementing that bins 'value'
        // times.
        result->setBinContent(destBin++, value, value);
    }

    return result;
}

/* Creates a projection for a combined view of the given histograms list.
 * Assumes that all histograms in the list have the same x-axis binning!. */
// TODO: ResReduction
Histo1DPtr make_projection(const Histo1DList &histos, Qt::Axis axis,
                           double startX, double endX,
                           double startY, double endY)
{
    Q_ASSERT(!histos.isEmpty());
    Q_ASSERT(axis == Qt::XAxis || axis == Qt::YAxis);

    double projStart = (axis == Qt::XAxis ? startX : startY);
    double projEnd   = (axis == Qt::XAxis ? endX : endY);

    double otherStart = (axis == Qt::XAxis ? startY : startX);
    double otherEnd   = (axis == Qt::XAxis ? endY : endX);

    // Create an artificial binning of the combined views x-axis
    AxisBinning xBinning(histos.size(), 0.0, histos.size());

    AxisBinning projBinning;
    AxisBinning otherBinning;

    if (axis == Qt::XAxis)
    {
        projBinning  = xBinning;
        otherBinning = histos[0]->getAxisBinning(Qt::XAxis);

        for (const auto &histo: histos)
        {
            otherBinning.setBins(std::min(otherBinning.getBins(), histo->getAxisBinning(Qt::XAxis).getBins()));
            otherBinning.setMin(std::min(otherBinning.getMin(), histo->getAxisBinning(Qt::XAxis).getMin()));
            otherBinning.setMax(std::max(otherBinning.getMax(), histo->getAxisBinning(Qt::XAxis).getMax()));
        }
    }
    else if (axis == Qt::YAxis)
    {
        projBinning  = histos[0]->getAxisBinning(Qt::XAxis);
        otherBinning = xBinning;

        for (const auto &histo: histos)
        {
            projBinning.setBins(std::min(projBinning.getBins(), histo->getAxisBinning(Qt::XAxis).getBins()));
            projBinning.setMin(std::min(projBinning.getMin(), histo->getAxisBinning(Qt::XAxis).getMin()));
            projBinning.setMax(std::max(projBinning.getMax(), histo->getAxisBinning(Qt::XAxis).getMax()));
        }
    }

    qDebug() << "projBinning:" << projBinning.getMin() << projBinning.getMax()
             << "otherBinning:" << otherBinning.getMin() << otherBinning.getMax();

    s64 projStartBin = projBinning.getBinBounded(projStart);
    s64 projEndBin   = projBinning.getBinBounded(projEnd);

    s64 otherStartBin = otherBinning.getBinBounded(otherStart);
    s64 otherEndBin   = otherBinning.getBinBounded(otherEnd);

    //qDebug() << __PRETTY_FUNCTION__
    //    << "projEndBin" << projEndBin
    //    << "otherEndBin" << otherEndBin;

    s64 nProjBins = (projEndBin - projStartBin) + 1;

    // adjust start and end to low edge of corresponding bin
    projStart = projBinning.getBinLowEdge(projStartBin);
    projEnd   = projBinning.getBinLowEdge(projEndBin + 1);
    const auto projBinWidth = projBinning.getBinWidth();

    auto result = std::make_shared<Histo1D>(nProjBins, projStart, projEnd);

    // FIXME: TODO: set object names, set window title, etc
    //result->setAxisInfo(Qt::XAxis, histo->getAxisInfo(axis));
    //result->setObjectName(histo->objectName() + (axis == Qt::XAxis ? QSL(" X") : QSL(" Y")) + QSL(" Projection"));
    //
    u32 destBin = 0;

    for (u32 binI = projStartBin;
         binI <= projEndBin;
         ++binI)
    {
        double value = 0.0;

        for (u32 binJ = otherStartBin;
             binJ <= otherEndBin;
             ++binJ)
        {
            // Note: Cannot sample directly from the source histogram bins as
            // the scaling of that histogram may be different than the
            // calculated projection binning.
            if (axis == Qt::XAxis)
            {
                auto lowEdge = projBinning.getBinLowEdge(binJ);
                value += histos[binI]->getCounts(lowEdge, lowEdge + projBinWidth);
            }
            else
            {
                auto lowEdge = projBinning.getBinLowEdge(binI);
                value += histos[binJ]->getCounts(lowEdge, lowEdge + projBinWidth);
            }
        }

        result->setBinContent(destBin++, value, value);
    }

    return result;
}

inline constexpr Qt::Axis opposite(Qt::Axis axis)
{
    return axis == Qt::XAxis ? Qt::YAxis : Qt::XAxis;
}

Histo1DList slice(Histo2D *histo, Qt::Axis axis,
    double startX, double endX, double startY, double endY,
    ResolutionReductionFactors rrfs)
{
    const Qt::Axis main_axis  = axis;
    const Qt::Axis slice_axis = opposite(axis);

    const u32 main_rrf  = axis == Qt::XAxis ? rrfs.x : rrfs.y;
    const u32 slice_rrf = axis == Qt::XAxis ? rrfs.y : rrfs.x;

    const double main_start = axis == Qt::XAxis ? startX : startY;
    const double main_end   = axis == Qt::XAxis ? endX : endY;

    const double slice_start = axis == Qt::XAxis ? startY : startX;
    const double slice_end   = axis == Qt::XAxis ? endY : endX;

    const auto main_binning  = histo->getAxisBinning(main_axis);
    const auto slice_binning = histo->getAxisBinning(slice_axis);

    const u32 main_startBin = main_binning.getBinBounded(main_start, main_rrf);
    const u32 main_endBin   = main_binning.getBinBounded(main_end, main_rrf) + 1;

    const u32 slice_startBin = slice_binning.getBinBounded(slice_start, slice_rrf);
    const u32 slice_endBin   = slice_binning.getBinBounded(slice_end, slice_rrf) + 1;

    const auto main_bins = main_endBin - main_startBin;
    const auto slice_bins = slice_endBin - slice_startBin;

    qDebug("slice: main_start=%lf, main_end=%lf, main_startBin=%u, main_endBin=%u, main_bins=%u",
        main_start, main_end, main_startBin, main_endBin, main_bins);
    qDebug("slice: slice_start=%lf, slice_end=%lf, slice_startBin=%u, slice_endBin=%u, slice_bins=%u",
        slice_start, slice_end, slice_startBin, slice_endBin, slice_bins);

    Histo1DList result;
    result.reserve(main_bins);

    for (u32 bin = main_startBin; bin < main_endBin; ++bin)
    {
        auto h1d = std::make_shared<Histo1D>(slice_bins, slice_start, slice_end);

        h1d->setTitle(QSL("%1 [%2 slice %3/%4, start=%5, end=%6")
            .arg(histo->getTitle())
            .arg(main_axis == Qt::XAxis ? "X" : "Y")
            .arg(1 + bin - main_startBin)
            .arg(main_bins)
            .arg(main_binning.getBinLowEdge(bin, main_rrf))
            .arg(main_binning.getBinLowEdge(bin+1, main_rrf))
        );

        for (u32 slice_bin = slice_startBin, dest_bin=0; slice_bin < slice_endBin; ++slice_bin, ++dest_bin)
        {
            u32 xBin = axis == Qt::XAxis ? bin : slice_bin;
            u32 yBin = axis == Qt::XAxis ? slice_bin : bin;
            double value = histo->getBinContent(xBin, yBin, rrfs);
            bool ok = h1d->setBinContent(dest_bin, value, value);
            if (!ok)
                qDebug("slice: warning, could not write to dest_bin=%u", dest_bin);
        }

        result.push_back(h1d);
    }

    return result;
}
