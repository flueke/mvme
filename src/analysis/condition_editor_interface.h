#ifndef __MVME_ANALYSIS_CONDITION_EDITOR_INTERFACE_H__
#define __MVME_ANALYSIS_CONDITION_EDITOR_INTERFACE_H__

#include <QtPlugin>
#include "analysis/condition_link.h"
#include "libmvme_export.h"

namespace analysis
{

class LIBMVME_EXPORT ConditionEditorInterface
{
    public:
        virtual ~ConditionEditorInterface() {}

        /* Sets the condition to be edited.
         * Must return false if the condition can't be edited in this editor
         * instance, true otherwise.
         * In case the condition can't be edited getCondition() may return an
         * invalid ConditionLink instead of the one passed here. */
        virtual bool setEditCondition(const ConditionLink &cl) = 0;

        /* Returns the condition and subindex currently being edited.
         *
         * Note that the returned subindex may be different than the one
         * originally passed in when editCondition() was called. This is
         * because the editor can offer to edit all subindexes in one
         * graphical widget, e.g. Histo1DListWidget.
         */
        virtual ConditionLink getEditCondition() const = 0;

        /* Starts editing the condition that was previously set via
         * setEditCondition. */
        virtual void beginEditCondition() = 0;
};

}

#define ConditionEditorInterface_iid "com.mesytec.mvme.analysis.ConditionEditorInterface.1"
Q_DECLARE_INTERFACE(analysis::ConditionEditorInterface, ConditionEditorInterface_iid)

#endif /* __MVME_ANALYSIS_CONDITION_EDITOR_INTERFACE_H__ */
