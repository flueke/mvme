#ifndef __TYPED_TREE_H__
#define __TYPED_TREE_H__

#include <QMap>
#include <QString>
#include <exception>

namespace util
{
namespace tree
{

template<typename T>
class Node
{
    public:
        using data_type      = T;
        using node_type      = Node<T>;
        using child_map_type = QMap<QString, node_type>;
        using size_type      = typename child_map_type::size_type;

        using iterator       = typename child_map_type::iterator;
        using const_iterator = typename child_map_type::const_iterator;

        struct Exception: public std::runtime_error {};
        struct ChildNotFound: public Exception {};

        // node construction
        explicit Node(const T &data = {}, const node_type *parent = nullptr)
            : m_parent(parent)
            , m_data(data)
        {
        }

        // parent
        const node_type *parent() const { return m_parent; }
        node_type *parent() { return m_parent; }

        // direct children
        void addChild(const QString &path, const data_type &data)
        {
            auto it = m_children.insert(path, node_type(data));
            it.value().parent = this;
        }

        const node_type &getChild(const QString &path) const
        {
            auto it = m_children.find(path);

            if (it != m_children.end())
                return it.value();

            throw ChildNotFound();
        }

        node_type &getChild(const QString &path)
        {
            auto it = m_children.find(path);

            if (it != m_children.end())
                return *it;

            throw ChildNotFound();
        }

        size_type childCount() const
        {
            return m_children.size();
        }

        // data
        const data_type &data() const
        {
            return m_data;
        }

        data_type &data()
        {
            return m_data;
        }

    private:
        data_type m_data;
        node_type *m_parent = nullptr;
        child_map_type m_children;
};

} // ns tree
} // ns util

#endif /* __TYPED_TREE_H__ */
