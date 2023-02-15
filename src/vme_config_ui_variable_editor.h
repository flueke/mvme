/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_VME_CONFIG_UI_VARIABLE_EDITOR_H__
#define __MVME_VME_CONFIG_UI_VARIABLE_EDITOR_H__

#include <memory>
#include <QWidget>

#include "libmvme_export.h"
#include "vme_script_variables.h"

// User interface to create, read, update and delete vme_script SymbolTables
// stored in vme_config objects.
//
// In the final form the editor should be able to display variables from
// multiple ConfigObjects hierarchically: the topmost nodes each represent a
// SymbolTable. Their children display the tables variable names, values and
// the optional comment attached to each variable.
//
// Symtab0 (outermost symbol table, level0)
//   - var1 val1
//   - var2 val2
// Symtab1 (level1 symbol table)
//   - var3 val3
//   - var4 val4
// ...
//
// SymtabN (levelN symbol table, the most local one from the perspective of the script)
//   - varN1 valN1
//   - varN2 valN2


class LIBMVME_EXPORT VariableEditorWidget: public QWidget
{
    Q_OBJECT
    signals:
        void variabledAdded(const QString &varName, const vme_script::Variable &newVar);

        // Any edit to the variable (value or comment)
        void variableModified(const QString &varName, const vme_script::Variable &updatedVar);

        // The variables value was modified by the user
        void variableValueChanged(const QString &varName, const vme_script::Variable &updatedVar);

        void variableDeleted(const QString &varName);

    public:
        explicit VariableEditorWidget(QWidget *parent = nullptr);
        ~VariableEditorWidget() override;

        void setVariables(const vme_script::SymbolTable &symtab);
        vme_script::SymbolTable getVariables() const;
        void setVariableValue(const QString &varName, const QString &varValue);

        // If set to true variables starting with sys_ and mesy_ are hidden.
        void setHideInternalVariables(bool b);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* __MVME_VME_CONFIG_UI_VARIABLE_EDITOR_H__ */
