#include "analysis_ui_util.h"

#include <QGuiApplication>
#include "analysis.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"
#include "rate_monitor_widget.h"

namespace analysis::ui
{

QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkInterface *sink, bool newWindow)
{
    return show_sink_widget(asp, std::dynamic_pointer_cast<SinkInterface>(sink->shared_from_this()), newWindow);
}

QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkPtr sink, bool newWindow)
{
    bool createWidget = (newWindow
            || !asp->getWidgetRegistry()->hasObjectWidget(sink.get())
            || QGuiApplication::keyboardModifiers() & Qt::ControlModifier);

    QWidget *existingWidget = asp->getWidgetRegistry()->getObjectWidget(sink.get());

    if (!createWidget)
    {
        show_and_activate(existingWidget);
        return existingWidget;
    }

    if (auto h1dSink = std::dynamic_pointer_cast<Histo1DSink>(sink))
    {
        Histo1DWidgetInfo widgetInfo{};
        widgetInfo.sink = h1dSink;
        widgetInfo.histos = h1dSink->getHistos();
        return show_sink_widget(asp, widgetInfo, newWindow);
    }
    else if (auto h2dSink = std::dynamic_pointer_cast<Histo2DSink>(sink))
    {
        return open_new_histo2dsink_widget(asp, h2dSink);
    }
    else if (auto rms = std::dynamic_pointer_cast<RateMonitorSink>(sink))
    {
        return open_new_ratemonitor_widget(asp, rms);
    }

    return {};
}

QWidget *show_sink_widget(AnalysisServiceProvider *asp, const Histo1DWidgetInfo &widgetInfo, bool newWindow)
{
    if (newWindow
        || !asp->getWidgetRegistry()->hasObjectWidget(widgetInfo.sink.get())
        || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
    {
        return open_new_histo1dsink_widget(asp, widgetInfo);
    }
    else if (auto widget = qobject_cast<Histo1DWidget *>(
                asp->getWidgetRegistry()->getObjectWidget(widgetInfo.sink.get())))
    {
        if (widgetInfo.histoAddress >= 0)
            widget->selectHistogram(widgetInfo.histoAddress);
        show_and_activate(widget);
        return widget;
    }

    return {};
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
                            asp->setAnalysisOperatorEdited(sink);
                        });

        if (widgetInfo.histoAddress >= 0)
            widget->selectHistogram(widgetInfo.histoAddress);

        asp->getWidgetRegistry()->addObjectWidget(widget, widgetInfo.sink.get(), widgetInfo.sink->getId().toString());

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
            asp->setAnalysisOperatorEdited(sink);
        },
        // makeUniqueOperatorNameFunction
        [asp] (const QString &name) {
            return make_unique_operator_name(asp->getAnalysis(), name);
        });

    asp->getWidgetRegistry()->addObjectWidget(widget, sink.get(), sink->getId().toString());

    return widget;
}

QWidget *open_new_ratemonitor_widget(AnalysisServiceProvider *asp, const std::shared_ptr<RateMonitorSink> &rms)
{
    auto widget = new RateMonitorWidget(rms,
        // sinkModifiedCallback
        [asp] (const std::shared_ptr<RateMonitorSink> &sink) { asp->setAnalysisOperatorEdited(sink); });

    widget->setPlotExportDirectory(asp->getWorkspacePath(QSL("PlotsDirectory")));

    // Note: using a QueuedConnection here is a hack to
    // make the UI refresh _after_ the analysis has been
    // rebuilt.
    // The call sequence is
    //   sinkModifiedCallback
    //   -> serviceProvider->analysisOperatorEdited
    //     -> analysis->setOperatorEdited
    //     -> emit operatorEdited
    //   -> analysis->beginRun.
    // So with a direct connection the Widgets sinkModified
    // is called before the analysis has been rebuilt in
    // beginRun.
    QObject::connect(
        asp->getAnalysis(), &Analysis::operatorEdited,
        widget, [rms, widget] (const OperatorPtr &op)
        {
            if (op == rms)
                widget->sinkModified();
        }, Qt::QueuedConnection);

    asp->getWidgetRegistry()->addObjectWidget(widget, rms.get(), rms->getId().toString());

    return widget;
}

}