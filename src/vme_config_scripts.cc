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
#include "vme_config_scripts.h"
#include "vme_script.h"

#include <cassert>

namespace mesytec
{
namespace mvme
{

vme_script::VMEScript parse(const VMEScriptConfig *scriptConfig)
{
    u32 baseAddress = get_base_address(scriptConfig);
    return parse_and_return_symbols(scriptConfig, baseAddress).first;
}

vme_script::VMEScript parse(const VMEScriptConfig *scriptConfig, u32 baseAddress)
{
    return parse_and_return_symbols(scriptConfig, baseAddress).first;
}

VMEScriptAndVars parse_and_return_symbols(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress)
{
    try
    {
        auto symtabs = collect_symbol_tables(scriptConfig);
        auto script = vme_script::parse(scriptConfig->getScriptContents(), symtabs, baseAddress);

        return std::make_pair(script, symtabs);
    }
    catch (vme_script::ParseError &e)
    {
        e.scriptName = scriptConfig->getVerboseTitle();
        throw;
    }
}

vme_script::SymbolTables collect_symbol_tables(const ConfigObject *co)
{
    assert(co);

    auto symbolsWithSources = collect_symbol_tables_with_source(co);

    return convert_to_symboltables(symbolsWithSources);
}

#if 0
vme_script::SymbolTables collect_symbol_tables(const VMEScriptConfig *scriptConfig)
{
    assert(scriptConfig);

    auto result = collect_symbol_tables(qobject_cast<const ConfigObject *>(scriptConfig));

    result.push_front({"local", {}});

    return result;
}
#endif

namespace
{

void collect_symbol_tables_with_source(
    const ConfigObject *co,
    QVector<SymbolTableWithSourceObject> &dest)
{
    assert(co);
    dest.push_back({co->getVariables(), co});

    if (auto parentObject = qobject_cast<ConfigObject *>(co->parent()))
        collect_symbol_tables_with_source(parentObject, dest);
}

} // end anon namespace

QVector<SymbolTableWithSourceObject> collect_symbol_tables_with_source(
    const ConfigObject *co)
{
    QVector<SymbolTableWithSourceObject> result;

    collect_symbol_tables_with_source(co, result);

    return result;
}

vme_script::SymbolTables LIBMVME_EXPORT convert_to_symboltables(
    const QVector<SymbolTableWithSourceObject> &input)
{
    vme_script::SymbolTables result;

    std::transform(
        std::begin(input), std::end(input),
        std::back_inserter(result),
        [] (const SymbolTableWithSourceObject &stso) { return stso.first; });

    return result;
}

//
// ScriptConfigRunner
//

struct ScriptConfigRunner::Private
{
    ScriptConfigRunner *q = nullptr;
    VMEController *ctrl = nullptr;

    // List of input VMEScriptConfigs to run. If the vme_script::VMEScript
    // member is set then a preparsed entry was added and the VMEScriptConfig
    // will not be parsed again.
    std::vector<std::pair<const VMEScriptConfig *, vme_script::VMEScript>> scriptConfigs;

    ScriptConfigRunner::Options options = {};

    vme_script::ResultList run_impl();
};

ScriptConfigRunner::ScriptConfigRunner(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
}

ScriptConfigRunner::~ScriptConfigRunner()
{ }

void ScriptConfigRunner::setVMEController(VMEController *ctrl)
{
    d->ctrl = ctrl;
}

void ScriptConfigRunner::setScriptConfig(const VMEScriptConfig *scriptConf)
{
    d->scriptConfigs = { { scriptConf, {} } };
}

void ScriptConfigRunner::addScriptConfig(const VMEScriptConfig *scriptConf)
{
    d->scriptConfigs.push_back({ scriptConf, {} });
}

void ScriptConfigRunner::setScriptConfigs(const QVector<const VMEScriptConfig *> &scriptConfigs)
{
    d->scriptConfigs.clear();
    addScriptConfigs(scriptConfigs);
}

void ScriptConfigRunner::addScriptConfigs(const QVector<const VMEScriptConfig *> &scriptConfigs)
{
    for (auto sc: scriptConfigs)
        addScriptConfig(sc);
}

void ScriptConfigRunner::setPreparsedScriptConfig(const VMEScriptConfig *scriptConf, const vme_script::VMEScript &parsed)
{
    d->scriptConfigs = { { scriptConf, parsed } };
}

void ScriptConfigRunner::addPreparsedScriptConfig(const VMEScriptConfig *scriptConf, const vme_script::VMEScript &parsed)
{
    d->scriptConfigs.push_back({ scriptConf, parsed });
}

void ScriptConfigRunner::setOptions(const ScriptConfigRunner::Options &opts)
{
    d->options = opts;
}

ScriptConfigRunner::Options ScriptConfigRunner::getOptions() const
{
    return d->options;
}

vme_script::ResultList ScriptConfigRunner::run()
{
    emit started();
    auto results = d->run_impl();
    emit finished();
    return results;
}

vme_script::ResultList ScriptConfigRunner::Private::run_impl()
{
    auto log_msg = [this] (const QString &msg) { emit q->logMessage(msg); };
    auto log_err = [this] (const QString &msg) { emit q->logError(msg); };

    assert(ctrl);

    if (!ctrl)
    {
        log_err("ScriptConfigRunner: no VME controller set");
        return {};
    }

    // Parse the script configs

    std::vector<std::pair<const VMEScriptConfig *, vme_script::VMEScript>> parsed;
    int cmdCount = 0;

    for (const auto &pair: scriptConfigs)
    {
        auto scriptConf = pair.first;
        auto script = pair.second;

        // Preparsed entry - just copy it over
        if (!script.empty())
        {
            cmdCount += script.size();
            parsed.push_back(pair);
        }
        else
        {
            // Parse the script
            try
            {
                auto script = parse(scriptConf);
                cmdCount += script.size();
                parsed.emplace_back(std::make_pair(scriptConf, script));
            }
            catch (const vme_script::ParseError &e)
            {
                log_msg(QSL("Parsing script \"%1\"").arg(scriptConf->getVerboseTitle()));
                log_err(QSL("  Parse error: ") + e.toString());
                return {};
            }
        }
    }

    emit q->progressChanged(0, cmdCount);

    // Run the scripts

    vme_script::ResultList allResults;
    int cmdIndex = 0;

    for (const auto &pair: parsed)
    {
        auto scriptConf = pair.first;
        auto script = pair.second;

        log_msg(QSL("Running script \"%1\"").arg(scriptConf->getVerboseTitle()));

        vme_script::ResultList scriptResults;
        bool errorSeen = false;

        for (const auto &cmd: script)
        {
            if (cmd.type == vme_script::CommandType::Invalid)
                continue;

            if (!cmd.warning.isEmpty())
            {
                log_err(QSL("  Warning: %1 on line %2 (cmd=%3)")
                        .arg(cmd.warning) .arg(cmd.lineNumber) .arg(to_string(cmd.type)));
            }

            auto result = vme_script::run_command(ctrl, cmd, log_msg);

            if (!options.AggregateResults)
            {
                if (result.error.isError())
                    log_err("  " + format_result(result));
                else
                    log_msg("  " + format_result(result));
            }

            errorSeen = result.error.isError();
            scriptResults.push_back(result);
            ++cmdIndex;

            emit q->progressChanged(cmdIndex, cmdCount);

            if (!options.ContinueOnVMEError && errorSeen)
                break;
        }

        if (options.AggregateResults)
        {
            // Find first error
            auto it = std::find_if(
                scriptResults.begin(), scriptResults.end(),
                [] (const auto &r) { return r.error.isError(); });

            if (it == scriptResults.end())
                log_msg(QSL("  Executed %1 commands, no errors").arg(scriptResults.size()));
            else
                log_err(QSL("  Error: %1").arg(it->error.toString()));
        }

        allResults.append(scriptResults);

        if (!options.ContinueOnVMEError && errorSeen)
            break;
    }

    return allResults;
}

} // end namespace mvme
} // end namespace mesytec
