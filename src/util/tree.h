#ifndef __TYPED_TREE_H__
#define __TYPED_TREE_H__

#include <QMap>
#include <QString>

namespace util
{
namespace tree
{

template<typename T>
class Node
{
    public:
        using data_type = T;
        using node_type = Node<T>;
        using child_map = QMap<QString, node_type>;
        using iterator  = typename child_map::iterator;
        using const_iterator = typename child_map::const_iterator;

        explicit Node(const T &data = {}, const node_type *parent = nullptr)
            : m_parent(parent)
            , m_data(data)
        {
        }

        const node_type *parent() const { return m_parent; }
        node_type *parent() { return m_parent; }

    private:
        data_type m_data;
        node_type *m_parent = nullptr;
        child_map m_children;
};

} // ns tree
} // ns util

#endif /* __TYPED_TREE_H__ */
