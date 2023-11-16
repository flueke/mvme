#ifndef SRC_VME_CONFIG_ITEM_MODEL_H
#define SRC_VME_CONFIG_ITEM_MODEL_H

#include <QStandardItemModel>
#include <QMimeData>
#include <memory>
#include <QDebug>

class VmeConfigItemModel: public QStandardItemModel
{
    Q_OBJECT
    public:
        using QStandardItemModel::QStandardItemModel;
        ~VmeConfigItemModel() override;

        bool canDropMimeData(const QMimeData *data, Qt::DropAction action,
                             int row, int column, const QModelIndex &parent) const override
        {
            qDebug() << __PRETTY_FUNCTION__ << data << data->formats();
            Q_UNUSED(action); Q_UNUSED(row); Q_UNUSED(column); Q_UNUSED(parent);

            qDebug() << data->data("application/x-qstandarditemmodeldatalist");


            return data->hasUrls() || data->hasFormat("application/x-qstandarditemmodeldatalist");






            //return data->hasUrls() || data->hasFormat("application/x-qabstractitemmodeldatalist");
        }

        bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                          int row, int /*column*/, const QModelIndex &parent) override
        {
            qDebug() << __PRETTY_FUNCTION__;
            // Pretend to always move column 0 to circumvent QStandardItemModel
            // from adding columns.
            return QStandardItemModel::dropMimeData(data, action, row, 0, parent);
        }

};



#endif // SRC_VME_CONFIG_ITEM_MODEL_H
