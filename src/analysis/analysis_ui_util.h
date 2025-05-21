#ifndef __MNT_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_UI_UTIL_H_
#define __MNT_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_UI_UTIL_H_

#include "analysis_fwd.h"
#include "analysis_service_provider.h"
#include "libmvme_export.h"
#include "object_editor_dialog.h"
#include "ui_eventwidget.h"

namespace analysis::ui
{

// MIME types and structures for drag and drop operations

// Easy to serialize reference to an analysis object or a subobject.
struct LIBMVME_EXPORT AnalysisObjectRef
{
    // Unique id of the object. Required.
    QUuid id;

    // Optional element index if referencing a subobject, e.g. a specific
    // histogram in a Histo1DSink. Set to -1 if not applicable.
    int index = -1;

    explicit AnalysisObjectRef(const QUuid &id = {}, int index = -1)
        : id(id), index(index) {}

    bool isSubobjectRef() const
    {
        return index >= 0;
    }
};

// SourceInterface objects only
static const QString DataSourceObjectRefMimeType = QSL("application/x-mvme-analysis-datasource-objectref-list");

// Non datasource operators and directories
static const QString OperatorObjectRefMimeType = QSL("application/x-mvme-analysis-operator-objectref-list");

// Sink-type operators and directories
static const QString SinkObjectRefMimeType = QSL("application/x-mvme-analysis-sink-objectref-list");

// Generic, untyped analysis objects
static const QString ObjectRefMimeType = QSL("application/x-mvme-analysis-object-objectref-list");

// Decode data for any of the above object ref mime types.
QVector<AnalysisObjectRef> decode_object_ref_list(QByteArray data);
// Encode data for any of the above object ref mime types.
QByteArray encode_object_ref_list(const QVector<AnalysisObjectRef> &objectRefs);

struct Histo1DWidgetInfo
{
    QVector<std::shared_ptr<Histo1D>> histos;
    s32 histoAddress;
    std::shared_ptr<CalibrationMinMax> calib;
    std::shared_ptr<Histo1DSink> sink;
};

QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkInterface *sink, bool newWindow = false);
QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkPtr sink, bool newWindow = false);
QWidget *show_sink_widget(AnalysisServiceProvider *asp, const Histo1DWidgetInfo &widgetInfo, bool newWindow = false);

QWidget *open_new_histo1dsink_widget(AnalysisServiceProvider *asp, const Histo1DWidgetInfo &widgetInfo);
QWidget *open_new_histo2dsink_widget(AnalysisServiceProvider *asp, const Histo2DSinkPtr &sink);
QWidget *open_new_ratemonitor_widget(AnalysisServiceProvider *asp, const std::shared_ptr<RateMonitorSink> &sink);
QWidget *open_new_gridview_widget(AnalysisServiceProvider *asp, const std::shared_ptr<PlotGridView> &gridView);

EventWidget *find_event_widget(const Analysis *analysis = nullptr);

inline EventWidget *find_event_widget(const std::shared_ptr<Analysis> &analysis)
{
    return find_event_widget(analysis.get());
}

inline EventWidget *find_event_widget(const AnalysisObjectPtr &obj)
{
    if (!obj)
        return nullptr;

    return find_event_widget(obj->getAnalysis());
}

inline EventWidget *find_event_widget(const AnalysisObject *obj)
{
    return find_event_widget(obj->getAnalysis());
}

ObjectEditorDialog *operator_editor_factory(
    const OperatorPtr &op, s32 userLevel, ObjectEditorMode mode,
    const DirectoryPtr &destDir, EventWidget *eventWidget);

// Returns the first ObjectEditorDialog found in QApplication::topLevelWidgets()
ObjectEditorDialog *find_object_editor_dialog();

ObjectEditorDialog *edit_operator(const OperatorPtr &op);

ObjectEditorDialog *datasource_editor_factory(const SourcePtr &src,
                                              ObjectEditorMode mode,
                                              ModuleConfig *moduleConfig,
                                              EventWidget *eventWidget);

void edit_datasource(const SourcePtr &src);
}

Q_DECLARE_METATYPE(analysis::ui::AnalysisObjectRef);

QDataStream &operator<<(QDataStream &out, const analysis::ui::AnalysisObjectRef &ref);
QDataStream &operator>>(QDataStream &in, analysis::ui::AnalysisObjectRef &ref);

QDebug operator<<(QDebug dbg, const analysis::ui::AnalysisObjectRef &ref);

#endif // __MNT_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_UI_UTIL_H_
