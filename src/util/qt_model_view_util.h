#ifndef __MVME_UTIL_QT_MODEL_VIEW_UTIL_H__
#define __MVME_UTIL_QT_MODEL_VIEW_UTIL_H__

#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardItem>
#include <QTreeView>
#include <vector>

namespace mesytec
{
namespace mvme
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

}
}

#endif /* __MVME_UTIL_QT_MODEL_VIEW_UTIL_H__ */
