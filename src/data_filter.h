/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_DATA_FILTER_H__
#define __MVME_DATA_FILTER_H__

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QVector>
#include "libmvme_export.h"
#include "typedefs.h"
#include "analysis/a2/a2_data_filter.h"

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

using namespace a2::data_filter;

/* Combines multiple DataFilters to form a result value.
 *
 * The resulting extracted value is an unsigned 64 bit interger. This means the
 * total number of extraction characters ('D') is limited to 64.
 *
 * The subfilters fill the result value from low to high bits in order, i.e
 * filter[0] fills the lowest bits, filter[1] the ones after and so on.
 *
 */
class LIBMVME_EXPORT MultiWordDataFilter
{
    public:
        explicit MultiWordDataFilter(const QVector<DataFilter> &filters = QVector<DataFilter>());

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

                if (!part.matched && matches(filter, dataWord, wordIndex))
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
                result |= (static_cast<u64>(extract(m_filters[i], m_results[i].matchedWord, marker)) << shift);
                shift += get_extract_bits(m_filters[i], marker);
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
                result += get_extract_bits(filter, marker);
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
LIBMVME_EXPORT DataFilter makeFilterFromString(const QString &str, s32 wordIndex = -1);

// Removes spaces, creates filter.
LIBMVME_EXPORT DataFilter makeFilterFromBytes(const QByteArray &bytes, s32 wordIndex = -1);

// Groups of 4 chars separated by a space, no padding on the left.
LIBMVME_EXPORT QString generate_pretty_filter_string(u8 bits = 32, char c = 'N');

// Groups of 4 chars separated by a space, padded on the left with spaces if dataBits < totalBits.
LIBMVME_EXPORT QString generate_pretty_filter_string(u8 dataBits, u8 totalBits, char c = 'N');

#endif /* __MVME_DATA_FILTER_H__ */
