#include "mvme_exprtk.h"

namespace mvme_exprtk
{

bool SymbolTable::add_scalar(const std::string &name, double &value)
{
    if (entries.count(name))
        return false;

    entries[name] = Entry{ Entry::Scalar, &value, nullptr, nullptr };
    return true;
}

bool SymbolTable::add_string(const std::string &name, std::string &str)
{
    if (entries.count(name))
        return false;

    entries[name] = Entry{ Entry::String, nullptr, &str, nullptr };
    return true;
}

bool SymbolTable::add_vector(const std::string &name, std::vector<double> &vec)
{
    if (entries.count(name))
        return false;

    entries[name] = Entry{ Entry::Vector, nullptr, nullptr, &vec };
    return true;
}

} // namespace mvme_exprtk
