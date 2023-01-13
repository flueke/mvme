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
#include "analysis/ui_lib.h"
#include <QDebug>

namespace analysis
{

namespace ui
{

void CheckStateNotifyingNode::setData(int column, int role, const QVariant &value)
{
    if (column == 0 && role == Qt::CheckStateRole)
    {
        if (auto observer = dynamic_cast<CheckStateObserver *>(treeWidget()))
        {
            auto prev = BasicTreeNode::data(column, role);

            //qDebug() << __PRETTY_FUNCTION__ << "CheckStateRole value =" << value
            //    << ", prev data =" << prev;

            BasicTreeNode::setData(column, role, value);

            if (prev.isValid() && value.isValid() && prev != value)
            {
                observer->checkStateChanged(this, prev);
            }
        }
    }
    else
    {
        BasicTreeNode::setData(column, role, value);
    }
}

} // end namespace ui

} // end namespace analyis
