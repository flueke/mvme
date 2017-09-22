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
#ifndef __DATA_FILTER_H__
#define __DATA_FILTER_H__

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QLineEdit>
#include <QVector>
#include "libmvme_export.h"
#include "typedefs.h"

/*
 * Identifying:
 * 0, 1             -> 0, 1
 * other characters -> don't care
 *
 * Data extraction:
 * Set continuous masks using 'A' for the address value and 'D' for the data
 * value.
 *
 * No checking is done if extraction masks are in fact continuous, e.g. "A0AA"
 * will be accepted.
 *
 * The filter and mask characters are turned into lowercase before being
 * processed.
 *
 */

class LIBMVME_EXPORT DataFilter
{
    public:
        DataFilter(const QByteArray &filter = QByteArray(), s32 wordIndex = -1);

        QByteArray getFilter() const { return m_filter; }

        inline u32 getMatchMask() const { return m_matchMask; }
        inline u32 getMatchValue() const { return m_matchValue; }

        inline bool matches(u32 value, s32 wordIndex = -1) const
        {
            return ((m_matchWordIndex < 0) || (m_matchWordIndex == wordIndex))
                && ((value & getMatchMask()) == getMatchValue());
        }

        u32 getExtractMask(char marker) const;
        u32 getExtractShift(char marker) const;
        u32 getExtractBits(char marker) const;
        u32 extractData(u32 value, char marker) const;
        s32 getWordIndex() const { return m_matchWordIndex; }

        bool operator==(const DataFilter &other) const;
        inline bool operator!=(const DataFilter &other) { return !(*this == other); }

        QString toString() const;

    private:
        void compile();

        QByteArray m_filter;
        mutable QHash<char, u32> m_extractCache;

        u32 m_matchMask  = 0;
        u32 m_matchValue = 0;
        s32 m_matchWordIndex = -1;
};

/* Combines multiple DataFilters to form a result value.
 *
 * The resulting extracted value is an unsigned 64 bit interger. This means the
 * total number of extraction characters ('D') is limited to 64.
 *
 * The subfilters fill the result value from low to high bits in order, i.e
 * filter[0] fills the lowest bits, filter[1] the ones after and so on.
 *
 */
class MultiWordDataFilter
{
    public:
        MultiWordDataFilter(const QVector<DataFilter> &filters = QVector<DataFilter>());

        void addSubFilter(const DataFilter &filter);

        // Returning a copy here would be dangerous because of threading and
        // m_filters being copied at the same time another thread is iterating.
        const QVector<DataFilter> &getSubFilters() const { return m_filters; }
        void setSubFilters(const QVector<DataFilter> &subfilters);
        int getSubFilterCount() const { return m_filters.size(); }

        inline void handleDataWord(u32 dataWord, s32 wordIndex = -1)
        {
            const s32 nFilters = m_filters.size();

            for (int i=0; i<nFilters; ++i)
            {
                DataFilter &filter(m_filters[i]);
                ResultPart &part(m_results[i]);

                if (!part.matched && filter.matches(dataWord, wordIndex))
                {
                    part.matched = true;
                    part.matchedWord = dataWord;

#if 0
                    qDebug() << __PRETTY_FUNCTION__ << "part" << i << "now matched";
#endif

                    break;
                }
            }
        }

        inline bool isComplete() const
        {
            for (const auto &part: m_results)
            {
                if (!part.matched)
                    return false;
            }

            return true;
        }

        inline void clearCompletion()
        {
            for (auto &part: m_results)
            {
                part.matched = false;
            }
        }

        inline u64 extractData(char marker) const
        {
            if (m_filters.isEmpty() || !isComplete())
                return 0;

            u64 result = 0;
            u32 shift = 0;

            const s32 nFilters = m_filters.size();

            for (int i=0; i<nFilters; ++i)
            {
#if 1
                result |= (static_cast<u64>(m_filters[i].extractData(m_results[i].matchedWord, marker)) << shift);
                shift += m_filters[i].getExtractBits(marker);
#else
                u64 filterValue = m_filters[i].extractData(m_results[i].matchedWord, marker);
                u64 filterValueShifted = filterValue << shift;
                u32 markerBits = m_filters[i].getExtractBits(marker);
                u32 newShift = shift + markerBits;
                u64 newResult = result | filterValueShifted;

                qDebug()
                    << "filterValue unshifted =" << filterValue
                    << "shift =" << shift
                    << "filterValue shifted =" << filterValueShifted
                    << "marker =" << marker
                    << "bits for marker =" << markerBits
                    << "newShift =" << newShift
                    << "currentResult =" << result
                    << "newResult =" << newResult
                    ;

                result = newResult;
                shift = newShift;
#endif
            }

            return result;
        }

        inline u64 getResultValue() const
        {
            return extractData('D');
        }

        inline u64 getResultAddress() const
        {
            return extractData('A');
        }

        inline u32 getDataBits() const
        {
            return getExtractBits('D');
        }

        inline u32 getAddressBits() const
        {
            return getExtractBits('A');
        }

        inline u32 getExtractBits(char marker) const
        {
            u32 result = 0;

            for (const auto &filter: m_filters)
            {
                result += filter.getExtractBits(marker);
            }

            return result;
        }

        /* Note: These operators do not use the result and completion status in
         * any way. Only the subfilters are compared!
         */
        inline bool operator==(const MultiWordDataFilter &other) const { return (m_filters == other.m_filters); }
        inline bool operator!=(const MultiWordDataFilter &other) { return !(*this == other); }

        QString toString() const;

    private:
        QVector<DataFilter> m_filters;

        struct ResultPart
        {
            bool matched = false;
            u32  matchedWord = 0;
        };

        QVector<ResultPart> m_results;
};

// Converts input to 8 bit, removes spaces, creates filter.
DataFilter makeFilterFromString(const QString &str, s32 wordIndex = -1);

// Removes spaces, creates filter.
DataFilter makeFilterFromBytes(const QByteArray &bytes, s32 wordIndex = -1);

// Create a QLineEdit setup for convenient filter editing and display.
QLineEdit *makeFilterEdit();

#endif /* __DATA_FILTER_H__ */
