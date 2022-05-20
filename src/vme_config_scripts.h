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
#ifndef __VME_CONFIG_SCRIPTS_H__
#define __VME_CONFIG_SCRIPTS_H__

#include "vme_config.h"
#include "vme_script.h"
#include "vme_script_exec.h"
#include "libmvme_export.h"

#include <utility>

namespace mesytec
{
namespace mvme
{

//
// Bridges between / combines vme_config and vme_script
//

// Returns the base VME address to be used when parsing the script config.
// Returns the parent ModuleConfigs address or 0 if the scriptConfig does not
// have a parent module.
inline u32 get_base_address(const VMEScriptConfig *scriptConfig)
{
    assert(scriptConfig);
    auto moduleConfig = qobject_cast<ModuleConfig *>(scriptConfig->parent());
    return moduleConfig ? moduleConfig->getBaseAddress() : 0u;
}

// Parses the given scriptConfig. Gets the base address from the scriptConfig
// using get_base_address().
vme_script::VMEScript LIBMVME_EXPORT parse(const VMEScriptConfig *scriptConfig);

// Parses the given scriptConfig using the supplied baseAddress.
vme_script::VMEScript LIBMVME_EXPORT parse(const VMEScriptConfig *scriptConfig, u32 baseAddress);

using VMEScriptAndVars = std::pair<vme_script::VMEScript, vme_script::SymbolTables>;

VMEScriptAndVars LIBMVME_EXPORT parse_and_return_symbols(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress = 0);

// Collects the symbol tables from the given ConfigObject and all parent
// ConfigObjects. The first table in the return value is the one belonging to
// the given ConfigObject (the most local one).
vme_script::SymbolTables LIBMVME_EXPORT collect_symbol_tables(const ConfigObject *co);

// Same as collect_symbol_tables for a ConfigObject except that a script-local
// symbol table is prepended to the result.
// TODO: do we need the script-local symboltable at all? Is the 'set' command any good?
//vme_script::SymbolTables LIBMVME_EXPORT collect_symbol_tables(const VMEScriptConfig *scriptConfig);

// SymbolTable and the ConfigObject from which the table was created.
using SymbolTableWithSourceObject = std::pair<vme_script::SymbolTable, const ConfigObject *>;

// Collects the symbol tables from the given ConfigObject and all parent
// ConfigObjects. The first table in the return value is the one belonging to
// the given ConfigObject (the most local one).
QVector<SymbolTableWithSourceObject> LIBMVME_EXPORT collect_symbol_tables_with_source(
    const ConfigObject *co);

vme_script::SymbolTables LIBMVME_EXPORT convert_to_symboltables(
    const QVector<SymbolTableWithSourceObject> &input);

class LIBMVME_EXPORT ScriptConfigRunner: public QObject
{
    Q_OBJECT

    signals:
        void started();
        void finished();

        void progressChanged(int cur, int max);
        void logMessage(const QString &msg);
        void logError(const QString &msg);

    public:
        struct Options
        {
            bool ContinueOnVMEError: 1;
            bool AggregateResults: 1;
        };

        ScriptConfigRunner(QObject *parent = nullptr);
        ~ScriptConfigRunner() override;

        void setVMEController(VMEController *ctrl);

        // Note: setXXX replaces existing scripts, addXXX adds them instead.

        void setScriptConfig(const VMEScriptConfig *scriptConf);
        void addScriptConfig(const VMEScriptConfig *scriptConf);

        void setScriptConfigs(const QVector<const VMEScriptConfig *> &scriptConfigs);
        void addScriptConfigs(const QVector<const VMEScriptConfig *> &scriptConfigs);

        // Allows setting preparsed entries. Useful for example when
        // editing a script and the edited script text should be executed
        // instead of the text stored in the config. If 'parsed' is empty this
        // method is equal to setScriptConfig().
        void setPreparsedScriptConfig(const VMEScriptConfig *scriptConf, const vme_script::VMEScript &parsed);

        // Allows adding preparsed entries. Useful for example when
        // editing a script and the edited script text should be executed
        // instead of the text stored in the config. If 'parsed' is empty this
        // method is equal to addScriptConfig().
        void addPreparsedScriptConfig(const VMEScriptConfig *scriptConf, const vme_script::VMEScript &parsed);

        void setOptions(const Options &opts);
        Options getOptions() const;

    public slots:
        vme_script::ResultList run();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mvme
} // end namespace mesytec


#endif /* __VME_CONFIG_SCRIPTS_H__ */
