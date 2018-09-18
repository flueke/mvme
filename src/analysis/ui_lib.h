#ifndef __MVME_ANALYSIS_UI_LIB_H__
#define __MVME_ANALYSIS_UI_LIB_H__

#include <QTreeWidgetItem>
#include "typedefs.h"

namespace analysis
{

template<typename T>
T *get_pointer(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    return node ? reinterpret_cast<T *>(node->data(0, dataRole).value<void *>()) : nullptr;
}

inline QObject *get_qobject(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    return get_pointer<QObject>(node, dataRole);
}

template<typename T>
inline T *get_qobject_pointer(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    if (auto qobj = get_qobject(node, dataRole))
    {
        return qobject_cast<T *>(qobj);
    }

    return nullptr;
}

} // end namespace analysis

#endif /* __MVME_ANALYSIS_UI_LIB_H__ */
