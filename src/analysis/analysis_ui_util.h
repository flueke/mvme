#ifndef __MNT_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_UI_UTIL_H_
#define __MNT_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_UI_UTIL_H_

#include "analysis_fwd.h"
#include "analysis_service_provider.h"
#include "object_editor_dialog.h"

namespace analysis::ui
{

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

// Returns the first ObjectEditorDialog found QApplication::topLevelWidgets()
ObjectEditorDialog *find_object_editor_dialog();

}

#endif // __MNT_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_UI_UTIL_H_