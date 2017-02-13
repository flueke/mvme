#ifndef __ANALYSIS_UI_H__
#define __ANALYSIS_UI_H__

#include <QWidget>

class MVMEContext;

namespace analysis
{

class AnalysisWidgetPrivate;

class AnalysisWidget: public QWidget
{
    Q_OBJECT
    public:
        AnalysisWidget(MVMEContext *ctx, QWidget *parent = 0);
        ~AnalysisWidget();

    private:
        AnalysisWidgetPrivate *m_d;
};

}

#endif /* __ANALYSIS_UI_H__ */
