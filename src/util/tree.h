/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __TYPED_TREE_H__
#define __TYPED_TREE_H__

#include <cassert>
#include <functional>
#include <QMap>
#include <QString>
#include <QTextStream>
#include <QDebug>

namespace util
{
namespace tree
{

class PathIterator
{
    public:
        explicit PathIterator(const QString &path)
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

        // copy
#if 1
        Node(const node_type &other)
            : m_data(other.m_data)
            , m_children(other.m_children)
        {
            //qDebug() << __PRETTY_FUNCTION__ << this << &other;

            // use the same parent as the source node
            fixParent(other.m_parent);
        }

        Node &operator=(const node_type &other)
        {
            //qDebug() << __PRETTY_FUNCTION__ << this << &other;

            // copy data but keep the current parent
            m_data = other.m_data;
            m_children = other.m_children;
            fixParent(m_parent);
            return *this;
        }
#endif

        // move
        Node(node_type &&other)
            : m_data(std::move(other.m_data))
            , m_children(std::move(other.m_children))
        {
            qDebug() << __PRETTY_FUNCTION__ << this << &other;

            fixParent(nullptr); // XXX
        }

        Node &operator=(node_type &&other)
        {
            qDebug() << __PRETTY_FUNCTION__ << this << &other;

            // move data but keep the current parent
            m_data = std::move(other.m_data);
            m_children = std::move(other.m_children);
            fixParent(m_parent);
            return *this;
        }

        // data
        const data_type *data() const
        {
            return &m_data;
        }

        data_type *data()
        {
            return &m_data;
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

        size_type childCount() const
        {
            return m_children.size();
        }

        // node classification
        bool isRoot() const { return !m_parent; }
        bool isLeaf() const { return isEmpty(); }
        bool isEmpty() const { return m_children.isEmpty(); }

        // tree and branches
        node_type *putBranch(const QString &path, const data_type &leafData = {})
        {
            auto result = traverseCreate(path);
            result->setData(leafData);
            return result;
        }

        const node_type *child(const QString &path) const
        {
            return traverse(path);
        }

        node_type *child(const QString &path)
        {
            return traverse(path);
        }

        bool contains(const QString &path) const
        {
            return traverse(path) != nullptr;
        }

        bool operator==(const node_type &other) const
        {
            if (this == &other)
                return true;

            return (m_data == other.m_data
                    && m_parent == other.m_parent
                    && m_children == other.m_children);
        }

        QString path() const
        {
            QString result;

            if (m_parent)
            {
                auto pp = m_parent->path();
                if (!pp.isEmpty())
                    result += pp + ".";
                result += m_parent->m_children.key(*this);
            }
            return result;
        }

        void assertParentChildIntegrity() const
        {
#ifndef NDEBUG
            if (parent())
            {
                bool selfFound = false;

                for (const auto &parentChild: parent()->m_children)
                {
                    if (&parentChild == this)
                    {
                        selfFound = true;
                        break;
                    }
                }

                assert(selfFound);
            }

            for (const auto &child: m_children)
            {
                assert(child.parent() == this);
                child.assertParentChildIntegrity();
            }
#endif
        }

        iterator begin() { return m_children.begin(); }
        iterator end() { return m_children.end(); }
        const_iterator begin() const { return m_children.begin(); }
        const_iterator end() const { return m_children.end(); }

    private:
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

        node_type *traverseCreate(const QString &path)
        {
            PathIterator iter(path);
            node_type *node = this;

            for (auto partRef = iter.next(); !partRef.isEmpty(); partRef = iter.next())
            {
                auto part = partRef.toString();
                node = node->hasDirectChild(part) ? node->getDirectChild(part) : node->addDirectChild(part);
                assert(node);
            }

            return node;
        }

        void fixParent(node_type *parent)
        {
            m_parent = parent;

            for (auto &child: m_children)
            {
                child.fixParent(this);
            }
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
        do_indent() << it.key() << '\n';
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
