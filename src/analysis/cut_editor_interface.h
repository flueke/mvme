#ifndef __MVME_ANALYSISCUT_EDITOR_INTERFACE_H__
#define __MVME_ANALYSISCUT_EDITOR_INTERFACE_H__

#include <QtPlugin>
#include "libmvme_export.h"

namespace analysis
{

class ConditionLink;

class LIBMVME_EXPORT CutEditorInterface
{
    public:
        virtual ~CutEditorInterface() {}

        virtual void editCut(const ConditionLink &cl) = 0;
};

}

#define CutEditorInterface_iid "com.mesytec.mvme.analysis.CutEditorInterface.1"
Q_DECLARE_INTERFACE(analysis::CutEditorInterface, CutEditorInterface_iid)

#endif /* __MVME_ANALYSISCUT_EDITOR_INTERFACE_H__ */
