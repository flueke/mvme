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
#include "histo2d_widget_p.h"
#include "analysis/analysis.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>

using namespace analysis;

Histo2DSubRangeDialog::Histo2DSubRangeDialog(const std::shared_ptr<Histo2DSink> &histoSink,
                                             HistoSinkCallback addSinkCallback, HistoSinkCallback sinkModifiedCallback,
                                             MakeUniqueOperatorNameFunction makeUniqueOperatorNameFunction,
                                             double visibleMinX, double visibleMaxX, double visibleMinY, double visibleMaxY,
                                             QWidget *parent)
    : QDialog(parent)
    , m_histoSink(histoSink)
    , m_addSinkCallback(addSinkCallback)
    , m_sinkModifiedCallback(sinkModifiedCallback)
    , m_visibleMinX(visibleMinX)
    , m_visibleMaxX(visibleMaxX)
    , m_visibleMinY(visibleMinY)
    , m_visibleMaxY(visibleMaxY)
{
    setWindowTitle(QSL("Subrange Histogram"));

    limits_x = make_axis_limits_ui(QSL("X Limits"),
                                   std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                   visibleMinX, visibleMaxX, true);

    limits_y = make_axis_limits_ui(QSL("Y Limits"),
                                   std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                   visibleMinY, visibleMaxY, true);

    //
    // create as new
    //
    le_name = new QLineEdit;
    le_name->setText(makeUniqueOperatorNameFunction(histoSink->m_histo->objectName()));
    combo_xBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);
    combo_yBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);

    auto createAsNewFrame = new QFrame;
    auto createAsNewFrameLayout = new QFormLayout(createAsNewFrame);

    createAsNewFrame->setContentsMargins(2, 2, 2, 2);
    createAsNewFrameLayout->addRow(QSL("Name"), le_name);
    createAsNewFrameLayout->addRow(QSL("X Resolution"), combo_xBins);
    createAsNewFrameLayout->addRow(QSL("Y Resolution"), combo_yBins);

    select_by_resolution(combo_xBins, histoSink->m_xBins);
    select_by_resolution(combo_yBins, histoSink->m_yBins);

    gb_createAsNew = new QGroupBox(QSL("Create as new Histogram"));
    gb_createAsNew->setCheckable(true);

    connect(gb_createAsNew, &QGroupBox::toggled, this, [createAsNewFrame] (bool doCreateAsNew) {
        createAsNewFrame->setEnabled(doCreateAsNew);
    });

    gb_createAsNew->setChecked(false);

    auto createAsNewGroupBoxLayout = new QHBoxLayout(gb_createAsNew);
    createAsNewGroupBoxLayout->setContentsMargins(0, 0, 0, 0);
    createAsNewGroupBoxLayout->addWidget(createAsNewFrame);

    //
    // buttons bottom
    //
    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    //
    // main layout
    //
    auto layout = new QVBoxLayout(this);

    limits_x.outerFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    limits_y.outerFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

    layout->addWidget(limits_x.outerFrame);
    layout->addWidget(limits_y.outerFrame);
    layout->addWidget(gb_createAsNew);
    layout->addStretch();
    layout->addWidget(buttonBox);
}

void Histo2DSubRangeDialog::accept()
{
    std::shared_ptr<Histo2DSink> targetSink;

    if (gb_createAsNew->isChecked())
    {
        // Clones the existing sink
        targetSink = std::make_shared<Histo2DSink>();
        targetSink->m_inputX.connectPipe(m_histoSink->m_inputX.inputPipe, m_histoSink->m_inputX.paramIndex);
        targetSink->m_inputY.connectPipe(m_histoSink->m_inputY.inputPipe, m_histoSink->m_inputY.paramIndex);
        targetSink->m_xAxisTitle = m_histoSink->m_xAxisTitle;
        targetSink->m_yAxisTitle = m_histoSink->m_yAxisTitle;
        targetSink->m_xBins = combo_xBins->currentData().toInt();
        targetSink->m_yBins = combo_yBins->currentData().toInt();

        targetSink->setObjectName(le_name->text());
    }
    else
    {
        targetSink = m_histoSink;
    }

    bool doClear = false;

    if (limits_x.rb_limited->isChecked())
    {
        doClear |= (targetSink->m_xLimitMin != limits_x.spin_min->value()
            || targetSink->m_xLimitMax != limits_x.spin_max->value());

        targetSink->m_xLimitMin = limits_x.spin_min->value();
        targetSink->m_xLimitMax = limits_x.spin_max->value();
    }
    else
    {
        doClear |= (!std::isnan(targetSink->m_xLimitMin)
            || !std::isnan(targetSink->m_xLimitMax));

        targetSink->m_xLimitMin = make_quiet_nan();
        targetSink->m_xLimitMax = make_quiet_nan();
    }

    if (limits_y.rb_limited->isChecked())
    {
        doClear |= (targetSink->m_yLimitMin != limits_y.spin_min->value()
            || targetSink->m_yLimitMax != limits_y.spin_max->value());

        targetSink->m_yLimitMin = limits_y.spin_min->value();
        targetSink->m_yLimitMax = limits_y.spin_max->value();
    }
    else
    {
        doClear |= (!std::isnan(targetSink->m_yLimitMin)
            || !std::isnan(targetSink->m_yLimitMax));

        targetSink->m_yLimitMin = make_quiet_nan();
        targetSink->m_yLimitMax = make_quiet_nan();
    }

    if (gb_createAsNew->isChecked())
    {
        m_addSinkCallback(targetSink);
    }
    else
    {
        if (doClear)
            targetSink->clearState();

        m_sinkModifiedCallback(targetSink);
    }

    QDialog::accept();
}
