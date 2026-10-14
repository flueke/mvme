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
#include <QRegularExpression>

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

Histo1DPtr make_x_projection(Histo2D *histo, const ResolutionReductionFactors &rrf)
{
    return make_projection(histo, Qt::XAxis, rrf);
}

Histo1DPtr make_x_projection(Histo2D *histo,
                             double startX, double endX,
                             double startY, double endY,
                             const ResolutionReductionFactors &rrf)
{
    return make_projection(histo, Qt::XAxis, startX, endX, startY, endY, rrf);
}

Histo1DPtr make_y_projection(Histo2D *histo, const ResolutionReductionFactors &rrf)
{
    return make_projection(histo, Qt::YAxis, rrf);
}

Histo1DPtr make_y_projection(Histo2D *histo,
                             double startX, double endX,
                             double startY, double endY,
                             const ResolutionReductionFactors &rrf)
{
    return make_projection(histo, Qt::YAxis, startX, endX, startY, endY, rrf);
}

std::shared_ptr<Histo1D> make_projection(Histo2D *histo, Qt::Axis axis, const ResolutionReductionFactors &rrf)
{
    auto projBinning  = histo->getAxisBinning(axis);
    auto otherBinning = histo->getAxisBinning(axis == Qt::XAxis ? Qt::YAxis : Qt::XAxis);

    return make_projection(histo, axis,
                           projBinning.getMin(), projBinning.getMax(),
                           otherBinning.getMin(), otherBinning.getMax(), rrf);
}

std::shared_ptr<Histo1D> make_projection(Histo2D *histo, Qt::Axis axis,
                                         double startX, double endX,
                                         double startY, double endY,
                                         const ResolutionReductionFactors &rrf)
{
    qDebug() << __PRETTY_FUNCTION__
        << axis
        << "startX" << startX << "endX" << endX
        << "startY" << startY << "endY" << endY;

    auto projRrf = (axis == Qt::XAxis) ? rrf.x : rrf.y;
    auto otherRrf = (axis == Qt::XAxis) ? rrf.y : rrf.x;

    double projStart = (axis == Qt::XAxis ? startX : startY);
    double projEnd   = (axis == Qt::XAxis ? endX : endY);

    double otherStart = (axis == Qt::XAxis ? startY : startX);
    double otherEnd   = (axis == Qt::XAxis ? endY : endX);

    auto projBinning  = histo->getAxisBinning(axis);
    auto otherBinning = histo->getAxisBinning(axis == Qt::XAxis ? Qt::YAxis : Qt::XAxis);

    s64 projStartBin = projBinning.getBinBounded(projStart, projRrf);
    s64 projEndBin   = projBinning.getBinBounded(projEnd, projRrf) + 1;

    s64 otherStartBin = otherBinning.getBinBounded(otherStart, otherRrf);
    s64 otherEndBin   = otherBinning.getBinBounded(otherEnd, otherRrf) + 1;

    s64 nProjBins = (projEndBin - projStartBin); // number of bins the projection actually touches.


    qDebug() << __PRETTY_FUNCTION__
        << "axis" << axis
        << "projRrf" << projRrf
        << "otherRrf" << otherRrf
        << "projStartBin " << projStartBin
        << "projEndBin" << projEndBin
        << "otherStartBin" << otherStartBin
        << "otherEndBin" << otherEndBin
        << "nProjBins" << nProjBins;

    qDebug() << "projBinning used for the physical resolution:"
             << "min" << projBinning.getMin()
             << "max" << projBinning.getMax()
             << "bins" << projBinning.getBins(projRrf);
    qDebug() << "projecting from" << projStart << "to" << projEnd << ", total of"
            << (projEndBin - projStartBin) << "bins";


    // Adjust start and end to the low edge of the corresponding bin.
    projStart = projBinning.getBinLowEdge(projStartBin, projRrf);
    projEnd   = projBinning.getBinLowEdge(projEndBin, projRrf);

    // Create dest binning covering only the zoomed region.
    auto destBinning = AxisBinning(nProjBins, projStart, projEnd);
    qDebug() << "destBinning:"
             << "min" << destBinning.getMin()
             << "max" << destBinning.getMax()
             << "bins" << destBinning.getBins();

    auto result = std::make_shared<Histo1D>(destBinning);
    result->setAxisInfo(Qt::XAxis, histo->getAxisInfo(axis));
    result->setObjectName(histo->objectName()
                          + (axis == Qt::XAxis ? QSL(" X") : QSL(" Y"))
                          + QSL(" Projection"));

    u32 destBin = 0;
    ResolutionReductionFactors samplingRrf;
    if (axis == Qt::XAxis)
    {
        samplingRrf = ResolutionReductionFactors{projRrf, otherRrf};
    }
    else
    {
        samplingRrf = ResolutionReductionFactors{otherRrf, projRrf};
    }

    qDebug() << "samplingRrf: x =" << samplingRrf.x
        << "y =" << samplingRrf.y;


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
                value += histo->getBinContent(binI, binJ, samplingRrf);
            }
            else
            {
                value += histo->getBinContent(binJ, binI, samplingRrf);
            }
        }

        // Using 'value' for the entry count too, assuming that the bin value in
        // the original histogram resulted from incrementing that bins 'value'
        // times.
        result->setBinContent(destBin++, value, value);
    }

    return result;
}

// Creates a projection for a combined view of the given histograms list.
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

    //qDebug() << "projBinning:" << projBinning.getMin() << projBinning.getMax()
    //         << "otherBinning:" << otherBinning.getMin() << otherBinning.getMax();

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

    if (axis == Qt::XAxis)
    {
        AxisInfo axisInfo;
        axisInfo.title = "Histogram #";
        result->setAxisInfo(Qt::XAxis, axisInfo);
    }
    else
    {
        result->setAxisInfo(Qt::XAxis, histos[0]->getAxisInfo(Qt::XAxis));
    }

    auto histoName = histos[0]->objectName();
    histoName.replace(QRegularExpression(R"(\s*\[\d+\]$)"), QSL("[n]"));
    result->setObjectName(histoName + (axis == Qt::XAxis ? QSL(" X") : QSL(" Y")) + QSL(" Projection"));
    result->setTitle(QSL("%1 %2 Projection").arg(histoName).arg(axis == Qt::XAxis ? "X" : "Y"));

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
            // calculated projection binning. Have to use getCounts() instead.
            if (axis == Qt::XAxis)
            {
                // binJ indexes otherBinning (data axis), not projBinning (histo-index axis)
                auto lowEdge = otherBinning.getBinLowEdge(binJ);
                value += histos[binI]->getCounts(lowEdge, lowEdge + otherBinning.getBinWidth());
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

QRectF snap_to_bin_edges(const AxisBinning &xBinning, const AxisBinning &yBinning,
                         const QRectF &rect, const ResolutionReductionFactors &rrf)
{
    auto binXLo = xBinning.getBinBounded(rect.left(), rrf.x);
    auto binXHi = xBinning.getBinBounded(rect.right(), rrf.x);
    auto binYLo = yBinning.getBinBounded(rect.top(), rrf.y);
    auto binYHi = yBinning.getBinBounded(rect.bottom(), rrf.y);

    assert(binXLo <= binXHi);
    assert(binYLo <= binYHi);

    if (binXLo > 0 && binXLo > rect.left())
        --binXLo;

    if (binXHi < xBinning.getBinCount() - 1 && binXHi < rect.right())
        ++binXHi;

    if (binYLo > 0 && binYLo < rect.bottom())
        --binYLo;

    if (binYHi < yBinning.getBinCount() - 1 && binYHi > rect.top())
        ++binYHi;

    auto left = xBinning.getBinLowEdge(binXLo, rrf.x);
    auto right = xBinning.getBinLowEdge(binXHi, rrf.x);
    auto top = yBinning.getBinLowEdge(binYHi, rrf.y);
    auto bot = yBinning.getBinLowEdge(binYLo, rrf.y);

    // handle empty axis binnings, e.g. there's no y axis binning for h1d widgets
    if (std::isnan(left))
        left = rect.left();

    if (std::isnan(right))
        right = rect.right();

    if (std::isnan(top))
        top = rect.top();

    if (std::isnan(bot))
        bot = rect.bottom();

    QRectF result;
    result.setLeft(left);
    result.setRight(right);
    result.setTop(top);
    result.setBottom(bot);

    //qDebug() << "snap_to_bin_edges: input rect" << rect << "snapped rect" << result;

    return result;
}
