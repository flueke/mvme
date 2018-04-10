#ifndef __MVME_EXPRTK_H__
#define __MVME_EXPRTK_H__

#include <map>
#include <memory>
#include <QString>
#include <string>
#include <vector>

#include "analysis/a2/a2_exprtk.h"

namespace mvme_exprtk
{

using ParserError = a2::a2_exprtk::ParserError;

class SymbolTable: public a2::a2_exprtk::SymbolTable
{
    bool add_scalar(const QString &name, double &value);
    bool add_string(const QString &name, std::string &str);
    bool add_vector(const QString &name, std::vector<double> &vec);
};

class Expression
{
    public:
        Expression();
        virtual ~Expression();

        void setExpressionString(const QString &str);
        QString getExpressionString() const;

        void registerSymbolTable(const SymbolTable &symtab);

        void compile();
        double eval();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // namespace mvme_exprtk

#endif /* __MVME_EXPRTK_H__ */
