#ifndef __TYPED_TREE_H__
#define __TYPED_TREE_H__

#include <cassert>
#include <functional>
#include <QMap>
#include <QString>
#include <QTextStream>

namespace util
{
namespace tree
{

class PathIterator
{
    public:
        PathIterator(const QString &path)
            : path(path)
            , eos(path.size())
        {}

        QStringRef next()
        {
            if (partEnd >= eos)
                return {};

            partEnd = path.indexOf('.', partBegin);

            if (partEnd < 0)
                partEnd = eos;

            int partLen = partEnd - partBegin;
            auto result = path.midRef(partBegin, partLen);
            partBegin   = partEnd + 1;
            return result;
        }

    private:
        const QString &path;
        const int eos;
        int partBegin = 0;
        int partEnd   = 0;
};

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

        const node_type *getDirectChild(const QString &key) const
        {
            auto it = m_children.find(key);
            return it == m_children.end() ? nullptr : &it.value();
        }

        node_type *getDirectChild(const QString &key)
        {
            auto it = m_children.find(key);
            return it == m_children.end() ? nullptr : &it.value();
        }

        bool hasDirectChild(const QString &key)
        {
            return m_children.find(key) != m_children.end();
        }

        /** Add or replace the child node identified by the given \c path using
         * the supplied \c data.
         * Returns a pointer to the newly created node. */
        node_type *setDirectChildData(const QString &key, const data_type &data)
        {
            auto it = m_children.insert(key, node_type(data));
            it.value().m_parent = this;
            return &it.value();
        }

        /** Add a direct child using the given \c path and \c data.
         * Returns a pointer to the newly created node or nullptr if a child
         * for \c path exists. */
        node_type *addDirectChild(const QString &key, const data_type &data = {})
        {
            if (hasDirectChild(key))
                return nullptr;

            return setDirectChildData(key, data);
        }

        size_type childCount() const
        {
            return m_children.size();
        }

        node_type *addDirectChild(const QString &key, const node_type &node)
        {
            if (hasDirectChild(key))
                return nullptr;

            return &m_children.insert(key, node).value();
        }

        // node classification
        bool isRoot() const { return !m_parent; }
        bool isLeaf() const { return isEmpty(); }
        bool isEmpty() const { return m_children.isEmpty(); }

        // tree and branches
        node_type *createBranch(const QString &path, const data_type &data = {})
        {
            //return createBranch(path.split('.'), data); // FIXME: split()
            PathIterator iter(path);
            node_type *node = this;

            for (auto partRef = iter.next(); !partRef.isEmpty(); partRef = iter.next())
            {
                auto part = partRef.toString();
                node = node->hasDirectChild(part) ? node->getDirectChild(part) : node->addDirectChild(part);
                assert(node);
            }

            node->setData(data);

            return node;
        }

        const node_type *getChild(const QString &path) const
        {
            return traverse(path);
        }

        node_type *getChild(const QString &path)
        {
            return traverse(path);
        }

        bool hasChild(const QString &path) const
        {
            return traverse(path) != nullptr;
        }

    private:
        const node_type *traverse(const QString &path) const
        {
            return const_cast<node_type *>(this)->traverse(path);
        }

        node_type *traverse(const QString &path)
        {
            PathIterator iter(path);
            node_type *node = this;

            for (auto partRef = iter.next(); !partRef.isEmpty(); partRef = iter.next())
            {
                auto part = partRef.toString();

                if (!node || !node->hasDirectChild(part))
                    return nullptr;

                node = node->getDirectChild(part);
            }

            return node;
        }

        data_type m_data;
        node_type *m_parent = nullptr;
        child_map_type m_children;
};

template<typename T>
QTextStream &dump_tree(QTextStream &out, const Node<T> &node, size_t indent)
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

template<typename T>
QTextStream &dump_tree(QTextStream &out, const Node<T> &node)
{
    return dump_tree(out, node, 0);
}

} // ns tree
} // ns util

#endif /* __TYPED_TREE_H__ */
