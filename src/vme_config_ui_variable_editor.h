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
        VariableEditorWidget(QWidget *parent = nullptr);
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
