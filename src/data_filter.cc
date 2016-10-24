#include "data_filter.h"
#include "util.h"

#include <stdexcept>

DataFilter::DataFilter(const QByteArray &filter)
    : m_filter(filter)
    , m_variables(256, 'X')
{
    if (filter.size() > 32)
        throw std::length_error("maximum filter size of 32 exceeded");

    m_variables['0'] = '0';
    m_variables['1'] = '1';
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
        char var = getVariable(c);

        if (var == '0' || var == '1' || var == 0 || var == 1)
            m_matchMask |= 1 << i;

        if (var == '1' || var == 1)
            m_matchValue |= 1 << i;
    }
}

void DataFilter::setVariable(char var, char value)
{
    m_variables[var] = value;
    compile();
}

#if 0
QHash<char, char> DataFilter::getVariables() const
{
    QHash<char, char> ret;

    for (int i=0; i<m_variables.size(); ++i)
    {
        char var = m_variables[i];

        if (var == '0'  || var == 0)
            ret[static_cast<char>(i)] = '0';
        else if (var == '1' || var == 1)
            ret[static_cast<char>(i)] = '1';
    }

    return ret;
}
#endif

u32 DataFilter::getExtractMask(char marker) const
{
    u32 result = 0;

    if (m_extractCache.contains(marker))
    {
        result = m_extractCache.value(marker);
    }
    else
    {
        for (int i=0; i<m_filter.size(); ++i)
        {
            char c = m_filter[m_filter.size() - i - 1];

            if (c == marker)
            {
                result |= 1 << i;
            }
        }
        m_extractCache[marker] = result;
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

u32 DataFilter::extractData(u32 value, char marker) const
{
    u32 result = (value & getExtractMask(marker)) >> getExtractShift(marker);

    return result;
}

bool DataFilter::operator==(const DataFilter &other) const
{
    return (m_filter == other.m_filter
            && m_variables == other.m_variables);
}
