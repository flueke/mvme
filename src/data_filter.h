#ifndef UUID_b5145aa0_ec26_4f1b_83cd_56b63757d828
#define UUID_b5145aa0_ec26_4f1b_83cd_56b63757d828

#include <QByteArray>
#include <QHash>
#include <QVector>
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

class DataFilter
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

/* Combines two DataFilters to form a combined result. The first filter is used
 * for the low-bits of the result, the second filter for the high bits. The
 * high bits will be left-shifted by the number of data bits in the first
 * filter.
 * For both filters the 'D' character is used to extract the data. Address
 * values are not used in any way with this filter. */
class DualWordDataFilter
{
    public:
        DualWordDataFilter(const DataFilter &lowFilter = DataFilter(), const DataFilter &highFilter = DataFilter());

        QVector<DataFilter> getFilters() const { return m_filters; }

        inline void handleDataWord(u32 dataWord, s32 wordIndex = -1)
        {
            for (int i=0; i<nFilters; ++i)
            {
                DataFilter &filter(m_filters[i]);

                if (filter.matches(dataWord, wordIndex))
                {
                    ResultPart part;
                    part.matched = true;
                    part.value   = filter.extractData(dataWord, 'D');
                    m_results[i] = part;
                }
            }
        }

        inline bool isComplete() const
        {
            return m_results[0].matched && m_results[1].matched;
        }

        inline void clearCompletion()
        {
            m_results[0].matched = false;
            m_results[1].matched = false;
        }

        inline u64 getResult() const
        {
            u64 result = m_results[0].value;
            result |= (m_results[1].value << m_filters[0].getExtractBits('D'));
            return result;
        }

        inline bool operator==(const DualWordDataFilter &other) const { return (m_filters == other.m_filters); }
        inline bool operator!=(const DualWordDataFilter &other) { return !(*this == other); }

        QString toString() const;

    private:
        static const int nFilters = 2;

        QVector<DataFilter> m_filters;

        struct ResultPart
        {
            bool matched = false;
            u32  value = 0;
        };

        QVector<ResultPart> m_results;
};

#endif
