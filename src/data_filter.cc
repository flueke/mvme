/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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

// Source: http://stackoverflow.com/a/757266
static inline int trailing_zeroes(uint32_t v)
{
    int r;           // result goes here
    static const int MultiplyDeBruijnBitPosition[32] =
    {
        0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
    };
    r = MultiplyDeBruijnBitPosition[((uint32_t)((v & -v) * 0x077CB531U)) >> 27];
    return r;
}

// Source: http://stackoverflow.com/a/109025 (SWAR)
static inline u32 number_of_set_bits(u32 i)
{
     // Java: use >>> instead of >>
     // C or C++: use uint32_t
     i = i - ((i >> 1) & 0x55555555);
     i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
     return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

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

QLineEdit *makeFilterEdit()
{
    QFont font;
    font.setFamily(QSL("Monospace"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(9);

    QLineEdit *result = new QLineEdit;
    result->setFont(font);
    result->setInputMask("NNNN NNNN NNNN NNNN NNNN NNNN NNNN NNNN");

    QFontMetrics fm(font);
    s32 padding = 6;
    s32 width = fm.width(result->inputMask()) + padding;
    result->setMinimumWidth(width);

    return result;
}

//
// DataFilterExternalCache
//
static inline QByteArray remove_spaces(const QByteArray input)
{
    QByteArray result;

    for (auto c: input)
    {
        if (c != ' ')
            result.push_back(c);
    }

    return result;
}

DataFilterExternalCache::DataFilterExternalCache(const QByteArray &filterRaw, s32 wordIndex)
    : m_matchWordIndex(wordIndex)
{
    qDebug() << __PRETTY_FUNCTION__ << filterRaw << "begin";
    QByteArray filter = remove_spaces(filterRaw);

    if (filter.size() > FilterSize)
        throw std::length_error("maximum filter size of 32 exceeded");

    m_filter.fill('X');

    // FIXME: need to fix the ordering

    for (s32 i=0; i<filter.size(); ++i)
    {
        m_filter[i] = filter[i];
    }

    compile();
    qDebug() << __PRETTY_FUNCTION__ << filterRaw << "end";
}

void DataFilterExternalCache::compile()
{
    qDebug() << __PRETTY_FUNCTION__ << "begin";
    m_matchMask  = 0;
    m_matchValue = 0;

    for (s32 i=0; i<FilterSize; ++i)
    {
        char c = m_filter[i];

        if (c == '0' || c == '1' || c == 0 || c == 1)
            m_matchMask |= 1 << i;

        if (c == '1' || c == 1)
            m_matchValue |= 1 << i;

        qDebug() << __PRETTY_FUNCTION__ << "c" << c;
        qDebug() << __PRETTY_FUNCTION__ << "mask" << m_matchMask;
        qDebug() << __PRETTY_FUNCTION__ << "value" << m_matchValue;
    }
    qDebug() << __PRETTY_FUNCTION__ << "end";
}

QByteArray DataFilterExternalCache::getFilterString() const
{
    return QByteArray(m_filter.data(), m_filter.size());
}

DataFilterExternalCache::CacheEntry DataFilterExternalCache::makeCacheEntry(char marker) const
{
    marker = std::tolower(marker);

    CacheEntry result;

    bool markerSeen = false;
    bool gapSeen = false;

    for (s32 i=0; i<FilterSize; ++i)
    {
        char c = std::tolower(m_filter[m_filter.size() - i - 1]);

        if (c == marker)
        {
            if (markerSeen && gapSeen)
            {
                // Had marker and a gap, now on marker again -> need gather step
                result.needGather = true;
            }

            result.extractMask |= 1 << i;
            markerSeen = true;
        }
        else if (markerSeen)
        {
            gapSeen = true;
        }
    }

    result.extractShift = trailing_zeroes(result.extractMask);
    result.extractBits  = number_of_set_bits(result.extractMask);

    return result;
}
