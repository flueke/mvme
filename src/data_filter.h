#ifndef UUID_b5145aa0_ec26_4f1b_83cd_56b63757d828
#define UUID_b5145aa0_ec26_4f1b_83cd_56b63757d828

#include <QByteArray>
#include <QHash>
#include "typedefs.h"

/*
 * identifying
 * 0, 1     -> 0, 1
 * X        -> 0 or 1 (dontcare)
 * Variables:
 *   O = overflow
 *   U = underflow
 *   P = pileup
 * set to one of 0, 1, X
 * Variables are just for user convenience to make it easy to read and edit
 * filters.
 *
 * By default any variables other than '0' and '1' are set to 'X' (dontcare).
 *
 * Note: Any variable values other than '0', 0, '1' and 1 are treated as
 * "dontcare" (not just 'X').
 *
 * Data extraction:
 * Set continuous masks using 'A' for the address value and 'D' for the data
 * value.
 *
 * Note: The variable names mentioned above are not enforced (any character can
 * be used and set to any value (should be one of [0, 1, X]), but the actual
 * data filtering implementation will use 'A' and 'D'.
 *
 * No checking is done if extraction masks are in fact continuous, e.g. "A0AA"
 * will be accepted.
 *
 *
 */

class DataFilter
{
    public:
        DataFilter(const QByteArray &filter);

        QByteArray getFilter() const { return m_filter; }

        inline u32 getMatchMask() const { return m_matchMask; }
        inline u32 getMatchValue() const { return m_matchValue; }

        inline bool matches(u32 value)
        {
            return ((value & getMatchMask()) == getMatchValue());
        }

        void setVariable(char var, char value);
        inline char getVariable(char var) const { return m_variables[var]; }
        //QHash<char, char> getVariables() const;
        QByteArray getVariables() const { return m_variables; }

        u32 getExtractMask(char marker) const;
        u32 getExtractShift(char marker) const;
        u32 getExtractBits(char marker) const;
        u32 extractData(u32 value, char marker) const;

        bool operator==(const DataFilter &other) const;
        inline bool operator!=(const DataFilter &other) { return !(*this == other); }

    private:
        void compile();

        QByteArray m_filter;
        QByteArray m_variables;
        mutable QHash<char, u32> m_extractCache;

        u32 m_matchMask  = 0;
        u32 m_matchValue = 0;
};

#endif
