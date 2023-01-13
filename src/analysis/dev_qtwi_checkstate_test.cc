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
#include <memory>
#include <QDebug>
#include <QTreeWidgetItem>

struct CheckStateObserver
{
    virtual void checkStateChanged(QTreeWidgetItem *node, const QVariant &prev) = 0;
};

class Node: public QTreeWidgetItem
{
    public:
        using QTreeWidgetItem::QTreeWidgetItem;

        void setObserver(CheckStateObserver *observer)
        {
            m_observer = observer;
        }

        virtual void setData(int column, int role, const QVariant &value) override
        {
            if (column == 0 && role == Qt::CheckStateRole)
            {
                auto observer = m_observer;
                //if (auto observer = dynamic_cast<CheckStateObserver *>(treeWidget()))
                if (observer)
                {
                    auto prev = QTreeWidgetItem::data(column, role);

                    qDebug() << __PRETTY_FUNCTION__ << "CheckStateRole value =" << value
                        << ", prev data =" << prev;

                    QTreeWidgetItem::setData(column, role, value);

                    if (prev.isValid() && prev != value)
                    {
                        observer->checkStateChanged(this, prev);
                    }
                }
            }
            else
            {
                QTreeWidgetItem::setData(column, role, value);
            }
        }

    private:
        CheckStateObserver *m_observer = nullptr;
};

struct TestObserver: public CheckStateObserver
{
    virtual void checkStateChanged(QTreeWidgetItem *node, const QVariant &prev) override
    {
        qDebug() << __PRETTY_FUNCTION__ << this << "CheckState changed on node" << node
            << ", prev =" << prev
            << ", new =" << node->data(0, Qt::CheckStateRole);
    }
};

int main()
{
    TestObserver observer;
    auto node = std::make_unique<Node>();
    node->setObserver(&observer);

    node->setData(0, Qt::CheckStateRole, Qt::PartiallyChecked);
    node->setData(0, Qt::CheckStateRole, Qt::Checked);
    node->setData(0, Qt::CheckStateRole, QVariant());
    node->setData(0, Qt::CheckStateRole, Qt::Checked);

    return 0;
}
