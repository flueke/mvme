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
#include "analysis/sink_widget_factory.h"

#include "analysis/analysis.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"
#include "mvme_context.h"

namespace analysis
{

QWidget *sink_widget_factory(const SinkPtr &sink, AnalysisServiceProvider *serviceProvider, QWidget *parent)
{
    QWidget *result = nullptr;

    if (auto h1dSink = std::dynamic_pointer_cast<Histo1DSink>(sink))
    {
        auto w = new Histo1DWidget(h1dSink->getHistos(), parent);
        result = w;

        w->setServiceProvider(serviceProvider);

        w->setSink(h1dSink, [serviceProvider] (const std::shared_ptr<Histo1DSink> &sink) {
            serviceProvider->analysisOperatorEdited(sink);
        });

        // Check if the histosinks input is a CalibrationMinMax and if so set
        // it on the widget.
        if (auto inPipe = sink->getSlot(0)->inputPipe)
        {
            if (auto calib = std::dynamic_pointer_cast<CalibrationMinMax>(
                    inPipe->getSource()->shared_from_this()))
            {
                w->setCalibration(calib);
            }
        }
    }
    else if (auto h2dSink = std::dynamic_pointer_cast<Histo2DSink>(sink))
    {
        auto w = new Histo2DWidget(h2dSink->getHisto(), parent);
        result = w;

        auto eventId   = h2dSink->getEventId();
        auto userLevel = h2dSink->getUserLevel();

        w->setServiceProvider(serviceProvider);

        w->setSink(
            h2dSink,
            // addSinkCallback
            [serviceProvider, eventId, userLevel] (const std::shared_ptr<Histo2DSink> &sink) {
                serviceProvider->addAnalysisOperator(eventId, sink, userLevel);
            },
            // sinkModifiedCallback
            [serviceProvider] (const std::shared_ptr<Histo2DSink> &sink) {
                serviceProvider->analysisOperatorEdited(sink);
            },
            // makeUniqueOperatorNameFunction
            [serviceProvider] (const QString &name) {
                return make_unique_operator_name(serviceProvider->getAnalysis(), name);
        });
    }
    else
    {
        InvalidCodePath;
    }

    if (result)
    {
        serviceProvider->getWidgetRegistry()->addObjectWidget(result, sink.get(), sink->getId().toString());
    }

    return result;
}

}
