#ifndef __MVME_ANALYSIS_UI_LIB_H__
#define __MVME_ANALYSIS_UI_LIB_H__

#include "analysis/analysis_fwd.h"
#include "treewidget_utils.h"
#include "typedefs.h"

#include <QTreeWidgetItem>

namespace analysis
{

namespace ui
{

static const int Global_DataRole_AnalysisObject = Qt::UserRole + 1000;

struct CheckStateObserver
{
    virtual void checkStateChanged(QTreeWidgetItem *node, const QVariant &prev) = 0;
};

class CheckStateNotifyingNode: public BasicTreeNode
{
    public:
        using BasicTreeNode::BasicTreeNode;

        virtual void setData(int column, int role, const QVariant &value) override;
};

template<typename T>
T *get_pointer(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    return node ? reinterpret_cast<T *>(node->data(0, dataRole).value<void *>()) : nullptr;
}

inline QObject *get_qobject(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    return get_pointer<QObject>(node, dataRole);
}

} // end namespace ui
} // end namespace analysis

#endif /* __MVME_ANALYSIS_UI_LIB_H__ */
