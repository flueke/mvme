/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
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
