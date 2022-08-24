#include "analysis_ui_util.h"

#include <QGuiApplication>
#include "analysis.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"

namespace analysis::ui
{

QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkInterface *sink, bool newWindow)
{
    return show_sink_widget(asp, std::dynamic_pointer_cast<SinkInterface>(sink->shared_from_this()), newWindow);
}

QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkPtr sink, bool newWindow)
{
    if (auto h1dSink = std::dynamic_pointer_cast<Histo1DSink>(sink))
    {
        Histo1DWidgetInfo widgetInfo{};
        widgetInfo.sink = h1dSink;
        widgetInfo.histos = h1dSink->getHistos();

        if (newWindow
            || !asp->getWidgetRegistry()->hasObjectWidget(sink.get())
            || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
        {
            return open_new_histo1dsink_widget(asp, widgetInfo);
        }
        else if (auto widget = qobject_cast<Histo1DWidget *>(
                    asp->getWidgetRegistry()->getObjectWidget(sink.get())))
        {
            if (widgetInfo.histoAddress >= 0)
                widget->selectHistogram(widgetInfo.histoAddress);
            show_and_activate(widget);
            return widget;
        }
    }
    else if (auto h2dSink = std::dynamic_pointer_cast<Histo2DSink>(sink))
    {
        if (newWindow
            || !asp->getWidgetRegistry()->hasObjectWidget(sink.get())
            || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
        {
            return open_new_histo2dsink_widget(asp, h2dSink);
        }
        else if (auto widget = qobject_cast<Histo2DWidget *>(
                    asp->getWidgetRegistry()->getObjectWidget(sink.get())))
        {
            show_and_activate(widget);
            return widget;
        }
    }
    else if (auto rms = std::dynamic_pointer_cast<RateMonitorSink>(sink))
    {
        #warning "implement me";
    }

    return {};
}

QWidget *show_sink_widget(AnalysisServiceProvider *asp, const Histo1DWidgetInfo &widgetInfo, bool newWindow = false);
{
}

QWidget *open_new_histo1dsink_widget(AnalysisServiceProvider *asp, const Histo1DWidgetInfo &widgetInfo)
{
    if (widgetInfo.sink && widgetInfo.histoAddress < widgetInfo.histos.size())
    {
        auto widget = new Histo1DWidget(widgetInfo.histos);
        widget->setServiceProvider(asp);

        if (widgetInfo.calib)
            widget->setCalibration(widgetInfo.calib);

        widget->setSink(widgetInfo.sink, [asp]
                        (const std::shared_ptr<Histo1DSink> &sink) {
                            asp->analysisOperatorEdited(sink);
                        });

        if (widgetInfo.histoAddress >= 0)
            widget->selectHistogram(widgetInfo.histoAddress);

        asp->getWidgetRegistry()->addObjectWidget(
            widget, widgetInfo.sink.get(),
            widgetInfo.sink->getId().toString());

        return widget;
    }

    return {};
}

QWidget *open_new_histo2dsink_widget(AnalysisServiceProvider *asp, const Histo2DSinkPtr &sink)
{
    auto widget = new Histo2DWidget(sink->m_histo);
    widget->setServiceProvider(asp);

    widget->setSink(
        sink,
        // addSinkCallback
        [asp, sink] (const std::shared_ptr<Histo2DSink> &newSink) {
            asp->addAnalysisOperator(sink->getEventId(), newSink, sink->getUserLevel());
        },
        // sinkModifiedCallback
        [asp] (const std::shared_ptr<Histo2DSink> &sink) {
            asp->analysisOperatorEdited(sink);
        },
        // makeUniqueOperatorNameFunction
        [asp] (const QString &name) {
            return make_unique_operator_name(asp->getAnalysis(), name);
        });
}

}