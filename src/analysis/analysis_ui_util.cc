#include "analysis_ui_util.h"

#include <QGuiApplication>
#include "analysis.h"
#include "histo1d_widget.h"

namespace analysis::ui
{

QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkInterface *sink)
{
    return show_sink_widget(asp, std::dynamic_pointer_cast<SinkInterface>(sink->shared_from_this()));
}

QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkPtr sink)
{
    if (auto h1dSink = std::dynamic_pointer_cast<Histo1DSink>(sink))
    {
        Histo1DWidgetInfo widgetInfo{};
        widgetInfo.sink = h1dSink;
        widgetInfo.histos = h1dSink->getHistos();

        if (auto h1d = std::dynamic_pointer_cast<Histo1DSink>(sink))
        {
            if (!asp->getWidgetRegistry()->hasObjectWidget(sink.get())
                || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
            {
                return open_histo1dsink_widget(asp, widgetInfo);
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
    }

    return {};
}

QWidget *open_histo1dsink_widget(AnalysisServiceProvider *asp, const Histo1DWidgetInfo &widgetInfo)
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

}