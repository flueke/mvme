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
#include "data_filter.h"
#include "qt_util.h"
#include "util/bits.h"

#include <cassert>
#include <cctype>
#include <cmath>
#include <stdexcept>

#define DATA_FILTER_DEBUG 0

//
// MultiWordDataFilter
//
MultiWordDataFilter::MultiWordDataFilter(const QVector<DataFilter> &filters)
    : m_filters(filters)
    , m_results(filters.size())
{
}

void MultiWordDataFilter::addSubFilter(const DataFilter &filter)
{
    m_filters.push_back(filter);
    m_results.resize(m_filters.size());
    clearCompletion();
}

void MultiWordDataFilter::setSubFilters(const QVector<DataFilter> &subFilters)
{
    m_filters = subFilters;
    m_results.resize(m_filters.size());
    clearCompletion();
}

QString MultiWordDataFilter::toString() const
{
    return QString("MultiWordDataFilter(filterCount=%1)")
        .arg(m_filters.size());
}

DataFilter makeFilterFromString(const QString &str, s32 wordIndex)
{
    return makeFilterFromBytes(str.toLocal8Bit(), wordIndex);
}

DataFilter makeFilterFromBytes(const QByteArray &filterDataRaw, s32 wordIndex)
{
    std::string filterData;

    for (auto c: filterDataRaw)
    {
        if (c != ' ')
            filterData.push_back(c);
    }

    return make_filter(filterData, wordIndex);
}

QString generate_pretty_filter_string(u8 bits, char c)
{
    QString buffer;
    buffer.resize(bits + std::ceil(bits / 4.0 - 1.0), ' ');

    int outidx = buffer.size() - 1;
    u8 bits_written = 0;

    while (bits_written < bits && outidx >= 0)
    {
        if (bits_written && bits_written % 4 == 0)
        {
            buffer[outidx--] = ' ';
        }

        assert(outidx >= 0);
        buffer[outidx--] = c;
        bits_written++;
    }

    return buffer;
}

QString generate_pretty_filter_string(u8 dataBits, u8 totalBits, char c)
{
    u8 bits = std::max(dataBits, totalBits);

    QString buffer;
    buffer.resize(bits + (bits / 4 - 1), ' ');

    int outidx = buffer.size() - 1;
    u8 bits_written = 0;

    while (bits_written < bits && outidx >= 0)
    {
        if (bits_written && bits_written % 4 == 0)
        {
            buffer[outidx--] = ' ';
        }

        if (bits_written < dataBits)
        {
            buffer[outidx--] = c;
        }
        else
        {
            buffer[outidx--] = ' ';
        }

        bits_written++;
    }

    return buffer;
}

