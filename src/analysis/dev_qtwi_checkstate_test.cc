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

int main(int argc, char *argv[])
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
