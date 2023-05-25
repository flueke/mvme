#ifndef MVME_SRC_REPLAY_UI_P_H_
#define MVME_SRC_REPLAY_UI_P_H_

#include <QDebug>
#include <QAbstractTableModel>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QMimeData>
#include <QProxyStyle>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStyleOption>
#include <QUrl>
#include <QVector>

#include "util/qt_str.h"

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

// Source: https://stackoverflow.com/a/9611137
class FullWidthDropIndicatorStyle: public QProxyStyle
{
    public:
        using QProxyStyle::QProxyStyle;

        void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                           QPainter *painter, const QWidget *widget) const override
        {
            if (element == QStyle::PE_IndicatorItemViewItemDrop && !option->rect.isNull())
            {
                QStyleOption opt(*option);
                opt.rect.setLeft(0);
                if (widget)
                    opt.rect.setRight(widget->width());
                QProxyStyle::drawPrimitive(element, &opt, painter, widget);
                return;
            }

            QProxyStyle::drawPrimitive(element, option, painter, widget);
        }
};

class QueueTableItem: public QStandardItem
{
    public:
        using QStandardItem::QStandardItem;
        QueueTableItem(const QString &text = {})
            : QStandardItem(text)
        {
            setEditable(false);
            setDropEnabled(false);
        }

        QStandardItem *clone() const override
        {
            auto ret = new QueueTableItem;
            *ret = *this;
            return ret;
        }
};

class QueueTableModel: public QStandardItemModel
{
    Q_OBJECT
    public:
        QueueTableModel(QObject *parent = nullptr)
            : QStandardItemModel(parent)
        {
            setColumnCount(4);
            setHeaderData(0, Qt::Horizontal, QSL("Name"));
            setHeaderData(1, Qt::Horizontal, QSL("Date Modified"));
            setHeaderData(2, Qt::Horizontal, QSL("Info0"));
            setHeaderData(3, Qt::Horizontal, QSL("Info1"));
            setItemPrototype(new QueueTableItem);
        }

        ~QueueTableModel() override;

        bool canDropMimeData(const QMimeData *data, Qt::DropAction action,
                             int row, int column, const QModelIndex &parent) const override
        {
            Q_UNUSED(action); Q_UNUSED(row); Q_UNUSED(column); Q_UNUSED(parent);
            return data->hasUrls() || data->hasFormat("application/x-qabstractitemmodeldatalist");
        }

        bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                          int row, int /*column*/, const QModelIndex &parent) override
        {
            if (!data->hasUrls())
            {
                // Pretend to always move column 0 to prevent QStandardItemModel
                // from adding columns.
                return QStandardItemModel::dropMimeData(data, action, row, 0, parent);
            }

            auto urls = data->urls();

            if (row >= 0)
                std::reverse(std::begin(urls), std::end(urls));

            for (const auto &url: urls)
            {
                if (!url.isLocalFile() || !(QFileInfo(url.fileName()).suffix() == QSL("zip")))
                    continue;

                auto rowItems = makeRow(url);

                if (row >= 0)
                    insertRow(row, rowItems);
                else
                    appendRow(rowItems);
            }

            return true;
        }

        void setQueueContents(const QStringList &paths)
        {
            beginResetModel();
            for (const auto &path: paths)
            {
                auto rowItems = makeRow(path);
                invisibleRootItem()->appendRow(rowItems);
            }
            endResetModel();
        }

        void clearQueueContents()
        {
            removeRows(0, rowCount());
        }

        QVector<QUrl> getQueueContents() const
        {
            const auto rows = rowCount();

            QVector<QUrl> result;
            result.reserve(rows);

            for (int row = 0; row < rows; ++row)
            {
                auto item0 = item(row, 0);
                result.push_back(item0->data().toUrl());
            }

            return result;
        }

    private:
        static const QStringList Headers;

        // Note: this is dangerous. Add the returned row to the model asap,
        // manually delete the items or leak memory.
        QList<QStandardItem *> makeRow(const QUrl &url = {})
        {
            auto item0 = new QueueTableItem;
            item0->setData(url.fileName(), Qt::DisplayRole);
            item0->setData(url);

            QList<QStandardItem *> rowItems = { item0 };

            for (auto i=Headers.size()-1; i>0; --i)
                rowItems.append(new QueueTableItem);

           return rowItems;
        }
};

}

#endif // MVME_SRC_REPLAY_UI_P_H_
