#ifndef __MESYTEC_MVLC_STRING_UTIL_H__
#define __MESYTEC_MVLC_STRING_UTIL_H__

#include <algorithm>
#include <string>
#include <vector>

namespace mesytec
{
namespace mvlc
{
namespace util
{

inline std::string join(const std::vector<std::string> &parts, const std::string &sep = ", ")
{
    std::string result;

    auto it = parts.begin();

    while (it != parts.end())
    {
        result += *it++;

        if (it < parts.end())
            result += sep;
    }

    return result;
}

inline std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

} // end namespace util
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_STRING_UTIL_H__ */
