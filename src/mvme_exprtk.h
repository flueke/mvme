#ifndef __MVME_EXPRTK_H__
#define __MVME_EXPRTK_H__

#include <map>
#include <string>
#include <vector>

namespace mvme_exprtk
{

struct ExpressionError
{
    std::string mode;
    std::string diagnostic;
    std::string src_location;
    std::string error_line;
    size_t line = 0, column = 0;
};

struct SymbolTable
{
    struct Entry
    {
        enum Type
        {
            Scalar,
            String,
            Vector
        };

        Type type;
        double *scalar;
        std::string *string;
        std::vector<double> *vector;
    };

    std::map<std::string, Entry> entries;

    bool add_scalar(const std::string &name, double &value);
    bool add_string(const std::string &name, std::string &str);
    bool add_vector(const std::string &name, std::vector<double> &vec);
};

} // namespace mvme_exprtk

#endif /* __MVME_EXPRTK_H__ */
