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

            if (prev.isValid() && prev != value)
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
