#ifndef __MVME_EXPRTK_H__
#define __MVME_EXPRTK_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mvme_exprtk
{

struct ParseError
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
        union
        {
            double *scalar;
            std::string *string;
            std::vector<double> *vector;
            struct
            {
                double *array;
                size_t size;
            } array;
        };
    };

    std::map<std::string, Entry> entries;

    bool add_scalar(const std::string &name, double &value);
    bool add_string(const std::string &name, std::string &str);
    bool add_vector(const std::string &name, std::vector<double> &vec);
};

class Expression
{
    public:
        Expression();
        virtual ~Expression();

        void setScript(const QString &str);
        QString getScript() const;

        void compile();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // namespace mvme_exprtk

#endif /* __MVME_EXPRTK_H__ */
