#ifndef MVME_SRC_REPLAY_UI_P_H_
#define MVME_SRC_REPLAY_UI_P_H_

#include <QDebug>
#include <QAbstractTableModel>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>

namespace mesytec::mvme
{

class BrowseFilterModel: public QSortFilterProxyModel
{
    Q_OBJECT
    public:
        BrowseFilterModel(QObject *parent = nullptr)
            : QSortFilterProxyModel(parent)
        {}

        ~BrowseFilterModel() override;

        // accept all directories and filenames matching the filterRegExp
        bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
        {
            auto index0 = sourceModel()->index(sourceRow, 0, sourceParent);

            if (auto fsModel = qobject_cast<const QFileSystemModel *>(sourceModel()))
            {
                return (fsModel->fileInfo(index0).isDir()
                    || fsModel->fileName(index0).contains(filterRegExp()));
            }

            return true;
        }

        // directories first in the name column, default comparison for other columns
        bool lessThan(const QModelIndex &ia, const QModelIndex &ib) const override
        {
            bool result = false;

            if (ia.column() != 0 || ib.column() != 0)
            {
                result = QSortFilterProxyModel::lessThan(ia, ib);
            }
            else if (auto fsModel = qobject_cast<const QFileSystemModel *>(sourceModel()))
            {
                auto fileInfo_a(fsModel->fileInfo(ia));
                auto fileInfo_b(fsModel->fileInfo(ib));

                if (fileInfo_a.isDir() && !fileInfo_b.isDir())
                    result = true;
                else if (!fileInfo_a.isDir() && fileInfo_b.isDir())
                    result = false;
                else
                    result = fileInfo_a.filePath() < fileInfo_b.filePath();
            }

            //qDebug() << ia.internalId() << ib.internalId() << "result =" << result;

            return result;
        }
};

class BrowseByRunTreeModel: public QAbstractItemModel
{
    Q_OBJECT
    public:
        BrowseByRunTreeModel(QObject *parent = nullptr)
            : QAbstractItemModel(parent)
        {}

        ~BrowseByRunTreeModel() override;
};

class QueueTableModel: public QAbstractTableModel
{
    Q_OBJECT
    public:
        QueueTableModel(QObject *parent = nullptr)
            : QAbstractTableModel(parent)
        {}

        ~QueueTableModel() override;
};

}

#endif // MVME_SRC_REPLAY_UI_P_H_
