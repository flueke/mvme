#ifndef __MVME_UTIL_QT_MODEL_VIEW_UTIL_H__
#define __MVME_UTIL_QT_MODEL_VIEW_UTIL_H__

#include <QHeaderView>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardItem>
#include <QTreeView>
#include <vector>

namespace mesytec::mvme
{

namespace
{
inline void collect_expanded_items(
    const QTreeView *view,
    const QStandardItem *root,
    std::vector<QStandardItem *> &dest)
{
    for (int row = 0; row < root->rowCount(); row++)
    {
        auto child = root->child(row);

        if (child && view->isExpanded(child->index()))
        {
            dest.push_back(child);
            collect_expanded_items(view, child, dest);
        }
    }
}
}

inline std::vector<QStandardItem *> expanded_items(const QTreeView *view, const QStandardItemModel *model)
{
    std::vector<QStandardItem *> dest;
    collect_expanded_items(view, model->invisibleRootItem(), dest);
    return dest;
}

// Returns a list of row indices which lead from the root item to the given
// item.
inline std::vector<int> row_indices(const QStandardItem *item)
{
    std::vector<int> result;

    for (; item; item = item->parent())
        result.push_back(item->row());

    std::reverse(std::begin(result), std::end(result));
    return result;
}

using TreeViewExpansionState = std::vector<std::vector<int>>;

inline TreeViewExpansionState make_expansion_state(const QTreeView *view, const QStandardItemModel *model)
{
    TreeViewExpansionState result;

    for (auto item: expanded_items(view, model))
        result.emplace_back(row_indices(item));

    return result;
}

inline void set_expansion_state(QTreeView *view, const TreeViewExpansionState &state)
{
    auto model = view->model();

    for (const auto &entry: state)
    {
        QModelIndex parent; // start out at the root (invalid index)

        for (int row: entry)
        {
            auto index = model->index(row, 0, parent);
            view->setExpanded(index, true);
            parent = index;
        }
    }
}

inline void expand_to_root(QTreeView *view, QModelIndex index)
{
    while (index.isValid())
    {
        view->setExpanded(index, true);
        index = index.parent();
    }
}

template<typename Predicate>
void find_items(QStandardItem *root, Predicate p, QVector<QStandardItem *> &result)
{
    if (p(root))
        result.push_back(root);

    for (int row = 0; row < root->rowCount(); ++row)
        find_items(root->child(row), p, result);
}

template<typename Predicate>
QVector<QStandardItem *> find_items(QStandardItem *root, Predicate p)
{
    QVector<QStandardItem *> result;
    find_items(root, p, result);
    return result;
}

inline void delete_item_rows(const QVector<QStandardItem *> &items)
{
    for (auto item : items)
    {
        if (item->parent() && item->row() >= 0)
        {
            auto row = item->parent()->takeRow(item->row());
            qDeleteAll(row);
        }
    }
}

class BaseItem: public QStandardItem
{
    public:
        explicit BaseItem(int type = QStandardItem::UserType)
            : type_(type) {}

        BaseItem(int type, const QString &text)
            : QStandardItem(text), type_(type) {}

        BaseItem(int type, const QIcon &icon, const QString &text)
            : QStandardItem(icon, text), type_(type) {}

        int type() const override { return type_; }

        QStandardItem *clone() const override
        {
            auto ret = new BaseItem(type_);
            *ret = *this;
            return ret;
        }

    private:
        int type_;
};

inline void enable_flags(QStandardItem *item, Qt::ItemFlags flags)
{
    item->setFlags(item->flags() | flags);
}

inline void disable_flags(QStandardItem *item, Qt::ItemFlags flags)
{
    item->setFlags(item->flags() & ~flags);
}

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
};

class ItemBuilder
{
    public:

        ItemBuilder(int type)
            : type_(type) {}

        ItemBuilder(int type, const QString &text)
            : type_(type), text_(text) {}

        ItemBuilder(int type, const QIcon &icon, const QString &text)
            : type_(type), icon_(icon), text_(text) {}

        ItemBuilder &text(const QString &text) { text_ = text; return *this; }
        ItemBuilder &qObject(QObject *obj) { obj_ = obj; return *this; }
        ItemBuilder &enableFlags(Qt::ItemFlags flag) { flagsEnable_ |= flag; return *this; }
        ItemBuilder &disableFlags(Qt::ItemFlags flag) { flagsDisable_ |= flag; return *this; }

        BaseItem *build()
        {
            auto result = new BaseItem(type_, icon_, text_);
            result->setEditable(false);
            result->setDropEnabled(false);
            result->setDragEnabled(false);

            if (obj_)
            {
                result->setData(QVariant::fromValue(reinterpret_cast<quintptr>(obj_)), DataRole_Pointer);

                if (auto p = obj_->property("display_name"); p.isValid() && text_.isEmpty())
                    result->setText(p.toString());

                if (auto p = obj_->property("icon"); p.isValid() && icon_.isNull())
                    result->setIcon(QIcon(p.toString()));
            }

            if (flagsEnable_)
                enable_flags(result, flagsEnable_);

            if (flagsDisable_)
                disable_flags(result, flagsDisable_);

            return result;
        }

    private:
        int type_;
        QIcon icon_;
        QString text_;
        QObject *obj_ = nullptr;
        Qt::ItemFlags flagsEnable_ = Qt::NoItemFlags;
        Qt::ItemFlags flagsDisable_ = Qt::NoItemFlags;
};

// Data for this mime type is QVector<QVariant>, where each variant contains a
// QObject * stored as quintptr.
inline QString qobject_pointers_mimetype()
{
    return "application/x-mvme-qobject-pointers";
}

// Get a QObject instance back out of a variant storing a pointer. Safe as long
// as nothing or a QObject * is stored in the variant.
template<typename T> T *qobject_from_pointer(const QVariant &pointer)
{
    if (auto obj = reinterpret_cast<QObject *>(pointer.value<quintptr>()))
    {
        if (auto config = qobject_cast<T *>(obj))
            return config;
    }

    return nullptr;
}

template<typename TargetType> TargetType *qobject_from_item(
    const QStandardItem *item, int dataRole = DataRole_Pointer)
{
    return qobject_from_pointer<TargetType>(item->data(dataRole));
}

template<typename TargetType>
QVector<TargetType *> object_pointers_from_mime_data(const QMimeData *mimeData)
{
    auto data = mimeData->data(qobject_pointers_mimetype());
    QVector<QVariant> pointers;
    QDataStream stream(&data, QIODevice::ReadOnly);
    stream >> pointers;
    QVector<TargetType *> result;

    for (const auto &pointer: pointers)
    {
        if (auto config = qobject_from_pointer<TargetType>(pointer))
            result.push_back(config);
    }

    return result;
}

QMimeData *mime_data_from_model_pointers(const QStandardItemModel *model, const QModelIndexList &indexes);

}

#endif /* __MVME_UTIL_QT_MODEL_VIEW_UTIL_H__ */
