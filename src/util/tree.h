#ifndef __TYPED_TREE_H__
#define __TYPED_TREE_H__

#include <cassert>
#include <exception>
#include <QMap>
#include <QString>
#include <QTextStream>

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

        struct Exception: public std::runtime_error
        {
            Exception(const char *what): std::runtime_error(what) {}
        };

        struct ChildNotFound: public Exception
        {
            ChildNotFound(): Exception("child not found") {}
        };

        struct ChildExists: public Exception
        {
            ChildExists(): Exception("child exists") {}
        };

        // node construction
        explicit Node(const T &data = {}, node_type *parent = nullptr)
            : m_data(data)
            , m_parent(parent)
        {
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

        void setData(const data_type &data)
        {
            m_data = data;
        }

        // direct parent
        const node_type *parent() const { return m_parent; }
        node_type *parent() { return m_parent; }

        // direct children
        const child_map_type &children() const { return m_children; }
        child_map_type &children() { return m_children; }

        /** Add or replace the child node identified by the given \c path using
         * the supplied \c data.
         * Returns a reference to the newly created node. */
        node_type &setChildData(const QString &key, const data_type &data)
        {
            auto it = m_children.insert(key, node_type(data));
            it.value().m_parent = this;
            return it.value();
        }

        /** Add a direct child using the given \c path and \c data.
         * Throws ChildExists if a child node is present for \c path.
         * Returns a reference to the newly created node. */
        node_type &addChild(const QString &key, const data_type &data = {})
        {
            if (hasChild(key))
                throw ChildExists();

            return setChildData(key, data);
        }

        const node_type &getChild(const QString &key) const
        {
            auto it = m_children.find(key);

            if (it != m_children.end())
                return it.value();

            throw ChildNotFound();
        }

        node_type &getChild(const QString &key)
        {
            auto it = m_children.find(key);

            if (it != m_children.end())
                return *it;

            throw ChildNotFound();
        }

        bool hasChild(const QString &key)
        {
            return m_children.find(key) != m_children.end();
        }

        size_type childCount() const
        {
            return m_children.size();
        }

        // node classification
        bool isRoot() const { return !m_parent; }
        bool isLeaf() const { return isEmpty(); }
        bool isEmpty() const { return m_children.isEmpty(); }

        // tree and branches
        node_type &createBranch(const QString &path, const data_type &data = {})
        {
            return createBranch(path.split('.'), data);
        }

    private:
        node_type &createBranch(const QStringList &pathParts, const data_type &data = {})
        {
            node_type *node = this;

            for (const auto &part: pathParts)
            {
                node = node->hasChild(part) ? &node->getChild(part) : &node->addChild(part);
                assert(node);
            }

            node->setData(data);

            return *node;
        }

        data_type m_data;
        node_type *m_parent = nullptr;
        child_map_type m_children;
};

template<typename T>
QTextStream &dump_tree(QTextStream &out, const Node<T> &node, size_t indent = 0)
{
    auto do_indent = [&]() -> QTextStream &
    {
        for (size_t i=0; i<indent; i++)
            out << "  ";
        return out;
    };

    auto &children(node.children());

    for (auto it = children.begin(); it != children.end(); it++)
    {
        do_indent() << it.key() << endl;
        dump_tree(out, it.value(), indent + 1);
    }

    return out;
}

} // ns tree
} // ns util

#endif /* __TYPED_TREE_H__ */
