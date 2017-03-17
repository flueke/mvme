#ifndef __ANALYSIS_UI_H__
#define __ANALYSIS_UI_H__

#include <memory>
#include <QWidget>

class MVMEContext;

namespace analysis
{

class AnalysisWidgetPrivate;
class OperatorInterface;

class AnalysisWidget: public QWidget
{
    Q_OBJECT
    public:
        AnalysisWidget(MVMEContext *ctx, QWidget *parent = 0);
        ~AnalysisWidget();

        void operatorAdded(const std::shared_ptr<OperatorInterface> &op);
        void operatorEdited(const std::shared_ptr<OperatorInterface> &op);

    private:
        AnalysisWidgetPrivate *m_d;
};

} // end namespace analysis

#endif /* __ANALYSIS_UI_H__ */
