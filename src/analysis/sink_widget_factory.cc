#include "analysis/sink_widget_factory.h"

#include "analysis/analysis.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"
#include "mvme_context.h"

namespace analysis
{

QWidget *sink_widget_factory(const SinkPtr &sink, MVMEContext *context, QWidget *parent)
{
    QWidget *result = nullptr;

    if (auto h1dSink = std::dynamic_pointer_cast<Histo1DSink>(sink))
    {
        auto w = new Histo1DWidget(h1dSink->getHistos(), parent);
        result = w;

        w->setContext(context);

        w->setSink(h1dSink, [context] (const std::shared_ptr<Histo1DSink> &sink) {
            context->analysisOperatorEdited(sink);
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

        w->setContext(context);

        w->setSink(
            h2dSink,
            // addSinkCallback
            [context, eventId, userLevel] (const std::shared_ptr<Histo2DSink> &sink) {
                context->addAnalysisOperator(eventId, sink, userLevel);
            },
            // sinkModifiedCallback
            [context] (const std::shared_ptr<Histo2DSink> &sink) {
                context->analysisOperatorEdited(sink);
            },
            // makeUniqueOperatorNameFunction
            [context] (const QString &name) {
                return make_unique_operator_name(context->getAnalysis(), name);
        });
    }
    else
    {
        InvalidCodePath;
    }

    if (result)
    {
        context->addObjectWidget(result, sink.get(), sink->getId().toString());
    }

    return result;
}

}
