#include "data_filter.h"
#include "util.h"

#include <cctype>
#include <stdexcept>

DataFilter::DataFilter(const QByteArray &filter)
    : m_filter(filter)
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
        result = m_extractCache.value(marker);
    }
    else
    {
        for (int i=0; i<m_filter.size(); ++i)
        {
            char c = std::tolower(m_filter[m_filter.size() - i - 1]);

            if (c == marker)
                result |= 1 << i;
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
    return (m_filter == other.m_filter);
}

QString DataFilter::toString() const
{
    return QString("DataFilter(f=%1)")
        .arg(QString::fromLocal8Bit(getFilter()));
}
