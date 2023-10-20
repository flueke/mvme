#include "util/version_compare.h"
#include <algorithm>
#include <sstream>

namespace mesytec::mvme::util
{

// Source: https://stackoverflow.com/a/2941508

std::vector<unsigned> parse_version(const std::string &s)
{
    std::istringstream iss(s);
    std::vector<unsigned> result;
    unsigned v;

    while (iss >> v)
    {
        result.push_back(v);
        iss.get(); // skip separators (period, dash, any other single character)
    }

    return result;
}

bool version_less_than(const std::string &a, const std::string &b)
{
    auto va = parse_version(a);
    auto vb = parse_version(b);
    return std::lexicographical_compare(std::begin(va), std::end(va), std::begin(vb), std::end(vb));
}

}
