#ifndef __ANALYSIS_INFO_WIDGET_H__
#define __ANALYSIS_INFO_WIDGET_H__

#include "mvme_context.h"
#include "libmvme_export.h"

struct AnalysisInfoWidgetPrivate;

class LIBMVME_EXPORT AnalysisInfoWidget: public QWidget
{
    Q_OBJECT
    public:
        AnalysisInfoWidget(MVMEContext *context, QWidget *parent = 0);
        ~AnalysisInfoWidget();

    private:
        void update();

        AnalysisInfoWidgetPrivate *m_d;
};

#endif /* __ANALYSIS_INFO_WIDGET_H__ */
