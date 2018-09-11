#ifndef __MVME_ANALYSIS_OBJECT_INFO_WIDGET_H__
#define __MVME_ANALYSIS_OBJECT_INFO_WIDGET_H__

#include <QFrame>

#include "analysis_fwd.h"

class MVMEContext;

namespace analysis
{

class ObjectInfoWidget: public QFrame
{
    Q_OBJECT
    public:
        ObjectInfoWidget(MVMEContext *ctx, QWidget *parent = nullptr);
        ~ObjectInfoWidget();

    public slots:
        void setObject(const AnalysisObjectPtr &obj);
        void refresh();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // end namespace analysis

#endif /* __MVME_ANALYSIS_OBJECT_INFO_WIDGET_H__ */
