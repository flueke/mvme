#ifndef MVME_SRC_REPLAY_UI_P_H_
#define MVME_SRC_REPLAY_UI_P_H_

#include <QDebug>
#include <QAbstractTableModel>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QMimeData>
#include <QProxyStyle>
#include <QSortFilterProxyModel>
#include <QStyleOption>
#include <QUrl>
#include <vector>

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

class QueueTableModel: public QAbstractTableModel
{
    Q_OBJECT
    public:
        struct QueueEntry
        {
            QFileInfo fileInfo;
        };

        QueueTableModel(QObject *parent = nullptr)
            : QAbstractTableModel(parent)
        {}

        ~QueueTableModel() override;

        int rowCount(const QModelIndex &parent = QModelIndex()) const override
        {
            return static_cast<int>(entries_.size());
        }

        int columnCount(const QModelIndex &parent = QModelIndex()) const override
        {
            return Headers.size();
        }

        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
        {
            if (index.row() >= static_cast<int>(entries_.size()))
                return {};

            if (index.column() == 0 && role == Qt::DisplayRole)
                return entries_.at(index.row()).fileInfo.fileName();

            return {};
        }

        QVariant headerData(int section, Qt::Orientation orientation,
                            int role = Qt::DisplayRole) const override
        {
            if (role == Qt::DisplayRole && orientation == Qt::Horizontal && 0 <= section && section < Headers.size())
                return Headers.at(section);
            return {};
        }

        Qt::ItemFlags flags(const QModelIndex &index) const override
        {
            auto result = QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;

            if (!index.isValid())
                result |= Qt::ItemIsDropEnabled;

            return result;
        }

        bool canDropMimeData(const QMimeData *data, Qt::DropAction action,
                             int row, int column, const QModelIndex &parent) const override
        {
            bool result = false;
            result = data->hasUrls() || data->hasFormat("application/x-qabstractitemmodeldatalist");
            qDebug() << __PRETTY_FUNCTION__ << data->formats() << "result =" << result;
            return true;
        }

        bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                          int row, int column, const QModelIndex &parent) override
        {
            qDebug() << __PRETTY_FUNCTION__ << data << action << "row =" << row << ", col =" << column << parent;

            if (data->hasUrls())
            {
                qDebug() << __PRETTY_FUNCTION__ << "url =" << data->urls();
                for (const auto &url: data->urls())
                {
                    if (!url.isLocalFile())
                        continue;
                    auto filename = url.fileName();
                    if (row < 0)
                    {
                        entries_.emplace_back(QueueEntry{ QFileInfo(filename) });
                    }
                    qDebug() << "would drop file" << filename;
                }
            }
            else if (data->hasFormat("application/x-qabstractitemmodeldatalist"))
            {
            }
            else
                return false;

            return true;

            bool result = QAbstractTableModel::dropMimeData(data, action, row, column, parent);
            qDebug() << __PRETTY_FUNCTION__ << "result would be" << result;
            qDebug() << __PRETTY_FUNCTION__ << data->formats();

            QByteArray encoded = data->data("application/x-qabstractitemmodeldatalist");
            QDataStream stream(&encoded, QIODevice::ReadOnly);

            while (!stream.atEnd())
            {
                int row, col;
                QMap<int, QVariant> roleDataMap;
                stream >> row >> col >> roleDataMap;

                qDebug() << __PRETTY_FUNCTION__ << row << col << roleDataMap;

                /* do something with the data */
            }

            return true;
        }

        void setQueueContents(const QStringList &paths)
        {
            beginResetModel();
            entries_.clear();
            entries_.reserve(paths.size());
            std::transform(std::begin(paths), std::end(paths), std::back_inserter(entries_),
                           [](const QString &path)
                           { return QueueEntry{ QFileInfo(path) }; });
            endResetModel();

        }

    private:
        static const QStringList Headers;
        std::vector<QueueEntry> entries_;
};

}

#endif // MVME_SRC_REPLAY_UI_P_H_
