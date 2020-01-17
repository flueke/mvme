#include "vme_config_scripts.h"

#include <cassert>

namespace mesytec
{
namespace mvme
{

vme_script::VMEScript parse(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress)
{
    return parse_and_return_symbols(scriptConfig, baseAddress).first;
}

VMEScriptAndVars parse_and_return_symbols(
    const VMEScriptConfig *scriptConfig,
    u32 baseAddress)
{
    auto symtabs = build_symbol_tables(scriptConfig);
    auto script = vme_script::parse(scriptConfig->getScriptContents(), symtabs, baseAddress);

    return std::make_pair(script, symtabs);
}

namespace
{

void build_symbol_tables(const ConfigObject *co, vme_script::SymbolTables &symtabs)
{
    assert(co);

    vme_script::SymbolTable symtab;

    if (auto event = qobject_cast<const EventConfig *>(co))
    {
        symtab.name = QSL("Event '%1'").arg(event->objectName());

        int irq = 0;

        if (event->triggerCondition == TriggerCondition::Interrupt)
            irq = event->irqLevel;

        symtab["irq"] = QString::number(irq);
        symtab["mcst"] = QString::number(event->getMulticastByte(), 16);

        symtabs.push_back(symtab);
    }

    if (auto module = qobject_cast<const ModuleConfig *>(co))
    {
        symtab.name = QSL("Module '%1'").arg(module->objectName());

        // Set the "irq" variable to 0 if the parent event is triggered by an
        // irq and this module should not raise the irq.
        // This will override the "irq" variable set by the event except for
        // the module that should raise the irq.
        if (auto event = module->getEventConfig())
        {
            if (event->triggerCondition == TriggerCondition::Interrupt
                && !module->raisesIRQ())
            {
                symtab["irq"] = QSL("0");
            }
        }

        symtabs.push_back(symtab);
    }

    if (auto child = qobject_cast<const ConfigObject *>(co->parent()))
        build_symbol_tables(child, symtabs);
}

}

vme_script::SymbolTables build_symbol_tables(const VMEScriptConfig *scriptConfig)
{
    assert(scriptConfig);

    vme_script::SymbolTables result = { {"local", {}} };

    if (auto co = qobject_cast<const ConfigObject *>(scriptConfig->parent()))
        build_symbol_tables(co, result);

    return result;
}

} // end namespace mvme
} // end namespace mesytec
