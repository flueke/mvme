#ifndef __MNT_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_UI_UTIL_H_
#define __MNT_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_UI_UTIL_H_

#include "analysis_fwd.h"
#include "analysis_service_provider.h"

namespace analysis::ui
{

struct Histo1DWidgetInfo
{
    QVector<std::shared_ptr<Histo1D>> histos;
    s32 histoAddress;
    std::shared_ptr<CalibrationMinMax> calib;
    std::shared_ptr<Histo1DSink> sink;
};

QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkInterface *sink);
QWidget *show_sink_widget(AnalysisServiceProvider *asp, SinkPtr sink);

QWidget *open_histo1dsink_widget(AnalysisServiceProvider *asp, const Histo1DWidgetInfo &widgetInfo);

}

#endif // __MNT_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_UI_UTIL_H_