#ifndef UUID_b5145aa0_ec26_4f1b_83cd_56b63757d828
#define UUID_b5145aa0_ec26_4f1b_83cd_56b63757d828

#include <QByteArray>
#include <QHash>
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
        DataFilter(const QByteArray &filter = QByteArray());

        QByteArray getFilter() const { return m_filter; }

        inline u32 getMatchMask() const { return m_matchMask; }
        inline u32 getMatchValue() const { return m_matchValue; }

        inline bool matches(u32 value) const
        {
            return ((value & getMatchMask()) == getMatchValue());
        }

        u32 getExtractMask(char marker) const;
        u32 getExtractShift(char marker) const;
        u32 getExtractBits(char marker) const;
        u32 extractData(u32 value, char marker) const;

        bool operator==(const DataFilter &other) const;
        inline bool operator!=(const DataFilter &other) { return !(*this == other); }

        QString toString() const;

    private:
        void compile();

        QByteArray m_filter;
        mutable QHash<char, u32> m_extractCache;

        u32 m_matchMask  = 0;
        u32 m_matchValue = 0;
};

#endif
