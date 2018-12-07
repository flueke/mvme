#ifndef __MVME_ANALYSIS_OBJECT_INFO_WIDGET_H__
#define __MVME_ANALYSIS_OBJECT_INFO_WIDGET_H__

#include <QFrame>

#include "analysis_fwd.h"
#include "vme_config.h"

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
        void setAnalysisObject(const AnalysisObjectPtr &obj);
        void setVMEConfigObject(const ConfigObject *obj);
        void refresh();
        void clear();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // end namespace analysis

#endif /* __MVME_ANALYSIS_OBJECT_INFO_WIDGET_H__ */
