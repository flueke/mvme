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
#ifndef __MVME_UTIL_QT_CONTAINER_H__
#define __MVME_UTIL_QT_CONTAINER_H__

#include <QStringList>
#include <QVector>

template<typename C>
QStringList to_qstrlist(
    const C &container)
{
    static_assert(std::is_same<typename C::value_type, QString>::value, "");

    QStringList result;
    result.reserve(container.size());
    std::copy(container.begin(), container.end(), std::back_inserter(result));
    return result;
}

template<typename C>
QStringList to_qstrlist_from_std(
    const C &container)
{
    static_assert(std::is_same<typename C::value_type, std::string>::value, "");

    QStringList result;
    result.reserve(container.size());

    std::transform(
        container.begin(), container.end(), std::back_inserter(result),
        [] (const std::string &str) { return QString::fromStdString(str); });

    return result;
}

template<typename Container>
QVector<typename Container::value_type> to_qvector(const Container &c)
{
    QVector<typename Container::value_type> result;

    for (const auto &value: c)
        result.push_back(value);

    return result;
}

template<typename Iter>
QVector<typename std::iterator_traits<Iter>::value_type> to_qvector(
    Iter begin_, const Iter &end_)
{
    QVector<typename std::iterator_traits<Iter>::value_type> result;

    while (begin_ != end_)
    {
        result.push_back(*begin_++);
    }

    return result;
}


#endif /* __MVME_UTIL_QT_CONTAINER_H__ */
