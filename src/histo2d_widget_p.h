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
#ifndef __HISTO2D_WIDGET_P_H__
#define __HISTO2D_WIDGET_P_H__

#include <memory>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include "histo_util.h"

namespace analysis
{
    class Histo2DSink;
}

class MVMEContext;

class Histo2DSubRangeDialog: public QDialog
{
    Q_OBJECT
    public:
        using SinkPtr = std::shared_ptr<analysis::Histo2DSink>;
        using HistoSinkCallback = std::function<void (const SinkPtr &)>;
        using MakeUniqueOperatorNameFunction = std::function<QString (const QString &name)>;


        Histo2DSubRangeDialog(const SinkPtr &histoSink,
                              HistoSinkCallback addSinkCallback, HistoSinkCallback sinkModifiedCallback,
                              MakeUniqueOperatorNameFunction makeUniqueOperatorNameFunction,
                              double visibleMinX, double visibleMaxX, double visibleMinY, double visibleMaxY,
                              QWidget *parent = 0);

        virtual void accept() override;

        SinkPtr m_histoSink;
        HistoSinkCallback m_addSinkCallback;
        HistoSinkCallback m_sinkModifiedCallback;

        double m_visibleMinX;
        double m_visibleMaxX;
        double m_visibleMinY;
        double m_visibleMaxY;

        QComboBox *combo_xBins = nullptr;
        QComboBox *combo_yBins = nullptr;
        HistoAxisLimitsUI limits_x;
        HistoAxisLimitsUI limits_y;
        QDialogButtonBox *buttonBox;
        QLineEdit *le_name;
        QGroupBox *gb_createAsNew;
};

#endif /* __HISTO2D_WIDGET_P_H__ */
