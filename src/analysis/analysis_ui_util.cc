#include "analysis_ui_util.h"

#include <QGuiApplication>
#include <QApplication>

#include "analysis.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"
#include "multiplot_widget.h"
#include "rate_monitor_widget.h"

#include "analysis_ui_p.h"
#include "expression_operator_dialog.h"
#include "listfilter_extractor_dialog.h"

namespace analysis::ui
{

QVector<QUuid> decode_id_list(QByteArray data)
{
    QDataStream stream(&data, QIODevice::ReadOnly);
    QVector<QByteArray> sourceIds;
    stream >> sourceIds;

    QVector<QUuid> result;
    result.reserve(sourceIds.size());

    for (const auto &idData: sourceIds)
    {
        result.push_back(QUuid(idData));
    }

    return result;
}

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
    if (widgetInfo.sink && !widgetInfo.histos.empty() && widgetInfo.histoAddress < widgetInfo.histos.size())
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

QWidget *open_new_gridview_widget(
    AnalysisServiceProvider *asp,
    const std::shared_ptr<PlotGridView> &gridView)
{
    auto widget = std::make_unique<MultiPlotWidget>(asp);
    widget->loadView(gridView);
    asp->getWidgetRegistry()->addObjectWidget(widget.get(), gridView.get(), gridView->getId().toString());
    return widget.release();
}

EventWidget *find_event_widget(const Analysis *analysis)
{
    auto widgets = QApplication::topLevelWidgets();

    for (auto widget: widgets)
    {
        if (auto ew = qobject_cast<EventWidget *>(widget))
            if (!analysis || ew->getAnalysis() == analysis)
                return ew;

        if (auto ew = widget->findChild<EventWidget *>())
            if (!analysis || ew->getAnalysis() == analysis)
                return ew;
    }

    return {};
}

ObjectEditorDialog *find_object_editor_dialog()
{
    auto widgets = QApplication::topLevelWidgets();

    for (auto widget: widgets)
    {
        if (auto oed = qobject_cast<ObjectEditorDialog *>(widget))
            return oed;
    }

    return {};
}

ObjectEditorDialog *operator_editor_factory(const OperatorPtr &op,
                                            s32 userLevel,
                                            ObjectEditorMode mode,
                                            const DirectoryPtr &destDir,
                                            EventWidget *eventWidget)
{
    ObjectEditorDialog *result = nullptr;

    if (auto expr = std::dynamic_pointer_cast<ExpressionOperator>(op))
    {
        result = new ExpressionOperatorDialog(expr, userLevel, mode, destDir, eventWidget);
        result->resize(1000, 800);
    }
    else if (auto exprCond = std::dynamic_pointer_cast<ExpressionCondition>(op))
    {
        result = new ExpressionConditionDialog(exprCond, userLevel, mode, destDir, eventWidget);
    }
    else
    {
        result = new AddEditOperatorDialog(op, userLevel, mode, destDir, eventWidget);
    }

    QObject::connect(result, &ObjectEditorDialog::applied,
                     eventWidget, &EventWidget::objectEditorDialogApplied);

    QObject::connect(result, &QDialog::accepted,
                     eventWidget, &EventWidget::objectEditorDialogAccepted);

    QObject::connect(result, &QDialog::rejected,
                     eventWidget, &EventWidget::objectEditorDialogRejected);

    return result;
}

ObjectEditorDialog *edit_operator(const OperatorPtr &op)
{
    // Check if an operator is being edited already.
    if (auto oed = find_object_editor_dialog())
    {
        // Raise the editor and abort.
        oed->show();
        oed->showNormal();
        oed->raise();
        return {};
    }

    if (auto eventWidget = find_event_widget(op))
    {
        auto dialog = operator_editor_factory(
            op, op->getUserLevel(), ObjectEditorMode::Edit, DirectoryPtr{}, eventWidget);

        if (dialog)
        {
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            eventWidget->clearAllTreeSelections();
            eventWidget->clearAllToDefaultNodeHighlights();
            return dialog;
        }
    }

    return {};
}

ObjectEditorDialog *datasource_editor_factory(const SourcePtr &src,
                                              ObjectEditorMode mode,
                                              ModuleConfig *moduleConfig,
                                              EventWidget *eventWidget)
{
    ObjectEditorDialog *result = nullptr;

    if (auto ex = std::dynamic_pointer_cast<Extractor>(src))
    {
        result = new AddEditExtractorDialog(ex, moduleConfig, mode, eventWidget);
    }
    else if (auto ex = std::dynamic_pointer_cast<ListFilterExtractor>(src))
    {
        auto serviceProvider = eventWidget->getServiceProvider();
        auto analysis = serviceProvider->getAnalysis();

        auto lfe_dialog = new ListFilterExtractorDialog(moduleConfig, analysis, serviceProvider, eventWidget);
        if (mode == ObjectEditorMode::New)
            lfe_dialog->newFilter();
        else
            lfe_dialog->editListFilterExtractor(ex);
        result = lfe_dialog;
    }
    else if (auto ex = std::dynamic_pointer_cast<MultiHitExtractor>(src))
    {
        result = new MultiHitExtractorDialog(ex, moduleConfig, mode, eventWidget);
    }
    else if (auto ex = std::dynamic_pointer_cast<DataSourceMdppSampleDecoder>(src))
    {
        result = new MdppSampleDecoderDialog(ex, moduleConfig, mode, eventWidget);
    }

    QObject::connect(result, &ObjectEditorDialog::applied,
                     eventWidget, &EventWidget::objectEditorDialogApplied);

    QObject::connect(result, &QDialog::accepted,
                     eventWidget, &EventWidget::objectEditorDialogAccepted);

    QObject::connect(result, &QDialog::rejected,
                     eventWidget, &EventWidget::objectEditorDialogRejected);

    return result;
}

void edit_datasource(const SourcePtr &src)
{
    if (auto oed = find_object_editor_dialog())
    {
        // Raise the editor and abort.
        oed->show();
        oed->showNormal();
        oed->raise();
        return;
    }

    auto eventWidget = find_event_widget(src);
    auto ana = src->getAnalysis();

    if (!eventWidget)
        return;

#ifndef NDEBUG //consistency check: analysis set on src and on service provider
    {
        auto ana2 = eventWidget->getServiceProvider()->getAnalysis();
        assert(ana.get() == ana2);
    }
#endif

    if (!ana)
        return;

    auto vmeConfig = eventWidget->getServiceProvider()->getVMEConfig();

    if (!vmeConfig)
        return;

    auto moduleConfig = vmeConfig->getModuleConfig(src->getModuleId());

    if (!moduleConfig)
        return;

    if (auto dialog = datasource_editor_factory(
        src, ObjectEditorMode::Edit, moduleConfig, eventWidget))
    {
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        eventWidget->clearAllTreeSelections();
        eventWidget->clearAllToDefaultNodeHighlights();
    }

    return;
}

}
