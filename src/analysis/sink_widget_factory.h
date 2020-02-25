#ifndef __MVME_ANALYSIS_SINK_WIDGET_FACTORY_H__
#define __MVME_ANALYSIS_SINK_WIDGET_FACTORY_H__

#include "analysis/analysis_fwd.h"

class QWidget;
class MVMEContext;

namespace analysis
{

QWidget *sink_widget_factory(const SinkPtr &sink, MVMEContext *context, QWidget *parent = nullptr);

}

#endif /* __MVME_ANALYSIS_SINK_WIDGET_FACTORY_H__ */
