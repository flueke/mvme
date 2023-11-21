#include "util/qt_model_view_util.h"

namespace mesytec::mvme
{

QMimeData *mime_data_from_model_pointers(const QStandardItemModel *model, const QModelIndexList &indexes)
{
    QVector<QVariant> pointers;

    for (const auto &index: indexes)
    {
        if (auto item = model->itemFromIndex(index))
        {
            if (item->data(DataRole_Pointer).isValid())
            {
                pointers.push_back(item->data(DataRole_Pointer));
            }
        }
    }

    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream << pointers;

    auto result = new QMimeData;
    result->setData(qobject_pointers_mimetype(), buffer);
    return result;
}

}
