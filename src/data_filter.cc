/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include <cctype>
#include <stdexcept>

#define DATA_FILTER_DEBUG 0

//
// DataFilter
//
DataFilter::DataFilter(const QByteArray &filter, s32 wordIndex)
    : m_filter(filter)
    , m_matchWordIndex(wordIndex)
{
    if (filter.size() > 32)
        throw std::length_error("maximum filter size of 32 exceeded");

    compile();
}

void DataFilter::compile()
{
    m_extractCache.clear();
    m_matchMask  = 0;
    m_matchValue = 0;

    for (int i=0; i<m_filter.size(); ++i)
    {
        char c = m_filter[m_filter.size() - i - 1];

        if (c == '0' || c == '1' || c == 0 || c == 1)
            m_matchMask |= 1 << i;

        if (c == '1' || c == 1)
            m_matchValue |= 1 << i;
    }
}

u32 DataFilter::getExtractMask(char marker) const
{
    u32 result = 0;

    marker = std::tolower(marker);

    if (m_extractCache.contains(marker))
    {
        result = m_extractCache.value(marker).mask;
    }
    else
    {
        bool markerSeen = false;
        bool gapSeen = false;
        bool needGather = false;

        for (int i=0; i<m_filter.size(); ++i)
        {
            char c = std::tolower(m_filter[m_filter.size() - i - 1]);

            if (c == marker)
            {
                if (markerSeen && gapSeen)
                {
                    // Had marker and a gap, now on marker again -> need gather step
                    needGather = true;
                }

                result |= 1 << i;
                markerSeen = true;
            }
            else if (markerSeen)
            {
                gapSeen = true;
            }
        }

        m_extractCache[marker] = { result, needGather };
    }

    return result;
}

u32 DataFilter::getExtractShift(char marker) const
{
    return trailing_zeroes(getExtractMask(marker));
}

u32 DataFilter::getExtractBits(char marker) const
{
    return number_of_set_bits(getExtractMask(marker));
}

bool DataFilter::needGather(char marker) const
{
    marker = std::tolower(marker);
    getExtractMask(marker); // force cache update
    return m_extractCache.value(marker).needGather;
}

//#include <x86intrin.h>

u32 DataFilter::extractData(u32 value, char marker) const
{
    marker = std::tolower(marker);
    u32 mask   = getExtractMask(marker);
    u32 shift  = getExtractShift(marker);
    u32 result = (value & mask) >> shift;
    bool needGather = m_extractCache.value(marker).needGather;

#if DATA_FILTER_DEBUG
    qDebug("%s: marker=%c, mask=0x%08X, shift=%u, result before gather =0x%08X, needGather=%d",
           __PRETTY_FUNCTION__, marker, mask, shift, result, needGather);
#endif

    if (needGather)
    {
        result = bit_gather(result, mask >> shift);

        //result = _pext_u32(result, mask >> shift);

#if DATA_FILTER_DEBUG
        qDebug("%s: marker=%c, mask=0x%08X, shift=%u, result after gather =0x%08X",
           __PRETTY_FUNCTION__, marker, mask, shift, result);
#endif
    }

    return result;
}

bool DataFilter::operator==(const DataFilter &other) const
{
    return (m_filter == other.m_filter)
        && (m_matchWordIndex == other.m_matchWordIndex);
}

QString DataFilter::toString() const
{
    return QString("DataFilter(f=%1,i=%2)")
        .arg(QString::fromLocal8Bit(getFilter()))
        .arg(m_matchWordIndex);
}

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
    QByteArray filterData;

    for (auto c: filterDataRaw)
    {
        if (c != ' ')
            filterData.push_back(c);
    }

    return DataFilter(filterData, wordIndex);
}

QString generate_pretty_filter_string(u8 bits, char c)
{
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

        buffer[outidx--] = c;
        bits_written++;
    }

    return buffer;
}

QLineEdit *makeFilterEdit(u8 bits)
{
    QFont font;
    font.setFamily(QSL("Monospace"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(9);

    QLineEdit *result = new QLineEdit;
    result->setFont(font);
    result->setInputMask(generate_pretty_filter_string(bits, 'N'));
    result->setAlignment(Qt::AlignRight);

    QFontMetrics fm(font);
    s32 padding = 6;
    s32 width = fm.width(result->inputMask()) + padding;
    result->setMinimumWidth(width);

    return result;
}
