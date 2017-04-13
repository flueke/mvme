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

        void updateAddRemoveUserLevelButtons();

    private:
        friend class AnalysisWidgetPrivate;
        AnalysisWidgetPrivate *m_d;

        void eventConfigModified();
};

} // end namespace analysis

#endif /* __ANALYSIS_UI_H__ */
