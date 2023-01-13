/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
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
