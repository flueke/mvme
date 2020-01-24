#include "vme_config_util.h"

#include <QSet>
#include "vme.h"

namespace
{
    u8 get_next_mcst(const VMEConfig *vmeConfig)
    {
        QVector<unsigned> mcsts;
        for (auto event: vmeConfig->getEventConfigs())
        {
            auto vars = event->getVariables();
            mcsts.push_back(vars["mesy_mcst"].value.toUInt(nullptr, 16));
        }

        std::sort(mcsts.begin(), mcsts.end());

        u8 result = 0;

        if (!mcsts.isEmpty())
            result = mcsts.back() + 1;

        if (result == 0)
            result = 0xbb;

        return result;
    }
} // end anon namespace

u8 get_next_free_irq(const VMEConfig *vmeConfig)
{
    QSet<u8> irqsInUse;

    for (auto event: vmeConfig->getEventConfigs())
    {
        if (event->triggerCondition == TriggerCondition::Interrupt)
            irqsInUse.insert(event->irqLevel);
    }

    for (u8 irq=vme::MinIRQ; irq <= vme::MaxIRQ; ++irq)
        if (!irqsInUse.contains(irq))
            return irq;

    return 0u;
}

vme_script::SymbolTable make_default_event_variables(u8 irq, u8 mcst)
{
    vme_script::SymbolTable vars;

    vars["sys_irq"] = vme_script::Variable(
        QString::number(irq), {}, "IRQ value set for the VME Controller for this event.");

    vars["mesy_mcst"] = vme_script::Variable(
        QString::number(mcst, 16), {}, "The most significant byte of the 32-bit multicast address to be used by this event.");

    vars["mesy_readout_num_events"] = vme_script::Variable(
        "1", {}, "Number of events to read out in each cycle.");

    vars["mesy_eoe_marker"] = vme_script::Variable(
        "1", {}, "EndOfEvent marker for mesytec modules (0: eventcounter, 1: timestamp, 3: extended_ts).");

    return vars;
}

std::unique_ptr<EventConfig> make_new_event_config(const VMEConfig *vmeConfig)
{
    auto eventConfig = std::make_unique<EventConfig>();

    u8 irq = get_next_free_irq(vmeConfig);
    u8 mcst = get_next_mcst(vmeConfig);

    // If there's no free irq reuse the first valid one.
    if (irq == 0) irq = vme::MinIRQ;;

    eventConfig->setObjectName(QString("event%1").arg(vmeConfig->getEventConfigs().size()));
    eventConfig->triggerCondition = TriggerCondition::Interrupt;
    eventConfig->irqLevel = irq;

    auto vars = make_default_event_variables(irq, mcst);
    eventConfig->setVariables(vars);

    return eventConfig;
}
