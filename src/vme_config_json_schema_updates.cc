#include "vme_config_json_schema_updates.h"

#include <QDebug>
#include <QJsonArray>
#include <QRegularExpression>

#include "globals.h"
#include "vme_config.h"
#include "vme_config_json_schema_updates_p.h"
#include "vme_config_util.h"
#include "vme_config_version.h"
#include "vme_script_util.h"
#include "vme_script_variables.h"

namespace
{

u32 get_register_value(u32 address, const QString &vmeScript, u32 defaultValue = 0u)
{
    try
    {
        auto writes = vme_script::collect_writes(vmeScript);
        return writes.value(address, defaultValue);
    }
    catch (const vme_script::ParseError &e)
    {}

    return defaultValue;
}

// defaults to 1: timestamp
u32 guess_module_mesy_eoe_marker(const QString &vmeSettingsScript)
{
    return get_register_value(0x6038, vmeSettingsScript, 1u);
}

// defaults to 1
u32 guess_module_readout_num_events(const QString &vmeSettingsScript)
{
    return get_register_value(0x601A, vmeSettingsScript, 1u);
}

} // end anon namespace

namespace mvme
{
namespace vme_config_json
{

u8 guess_event_mcst(const QString &eventScript)
{
    static const QRegularExpression re(
        R"-(^\s*writeabs\s+a32\s+d16\s+(0x[0-9a-fA-F]{2})00603a\s+.*$)-",
        QRegularExpression::MultilineOption);

    auto match = re.match(eventScript);

    if (match.hasMatch())
    {
        u8 mcst = static_cast<u8>(match.captured(1).toUInt(nullptr, 0));
        return mcst;
    }

    return 0u;
}


} // end namespace vme_config_json
} // end namespace mvme

namespace
{

/* Module script storage changed:
 * vme_scripts.readout              -> vmeReadout
 * vme_scripts.reset                -> vmeReset
 * vme_scripts.parameters           -> initScripts[0]
 * vme_scripts.readout_settings     -> initScripts[1]
 */
static QJsonObject v1_to_v2(QJsonObject json)
{
    qDebug() << "VME config conversion" << __PRETTY_FUNCTION__;

    auto eventsArray = json["events"].toArray();

    for (int eventIndex = 0;
         eventIndex < eventsArray.size();
         ++eventIndex)
    {
        QJsonObject eventJson = eventsArray[eventIndex].toObject();
        auto modulesArray = eventJson["modules"].toArray();

        for (int moduleIndex = 0;
             moduleIndex < modulesArray.size();
             ++moduleIndex)
        {
            QJsonObject moduleJson = modulesArray[moduleIndex].toObject();

            moduleJson["vmeReadout"] = moduleJson["vme_scripts"].toObject()["readout"];
            moduleJson["vmeReset"]   = moduleJson["vme_scripts"].toObject()["reset"];

            QJsonArray initScriptsArray;
            initScriptsArray.append(moduleJson["vme_scripts"].toObject()["parameters"]);
            initScriptsArray.append(moduleJson["vme_scripts"].toObject()["readout_settings"]);
            moduleJson["initScripts"] = initScriptsArray;

            modulesArray[moduleIndex] = moduleJson;
        }

        eventJson["modules"] = modulesArray;
        eventsArray[eventIndex] = eventJson;
    }

    json["events"] = eventsArray;

    return json;
}

/* Instead of numeric TriggerCondition values string representations are now
 * stored. */
static QJsonObject v2_to_v3(QJsonObject json)
{
    qDebug() << "VME config conversion" << __PRETTY_FUNCTION__;

    auto eventsArray = json["events"].toArray();

    for (int eventIndex = 0;
         eventIndex < eventsArray.size();
         ++eventIndex)
    {
        QJsonObject eventJson = eventsArray[eventIndex].toObject();

        auto triggerCondition = static_cast<TriggerCondition>(eventJson["triggerCondition"].toInt());
        eventJson["triggerCondition"] = TriggerConditionNames.value(triggerCondition);

        eventsArray[eventIndex] = eventJson;
    }

    json["events"] = eventsArray;

    return json;
}

struct ReplacementRule
{
    struct Options
    {
        static const uint16_t KeepOriginalAsComment = 0;
        static const uint16_t ReplaceOnly = 1;
    };

    QString pattern;
    QString replacement;
    uint16_t options = Options::KeepOriginalAsComment;
};

static const QString DefaultReplacementCommentPrefix = QSL(
    "the next line was auto updated by mvme, previous version: ");

QString apply_replacement_rules(
    const QVector<ReplacementRule> &rules,
    const QString &input,
    const QString &commentPrefix = DefaultReplacementCommentPrefix)
{
    using RO = ReplacementRule::Options;

    QString result = input;

    for (const auto &rule: rules)
    {
        QRegularExpression re(rule.pattern, QRegularExpression::MultilineOption);

        QString replacement;

        if (rule.options & RO::ReplaceOnly)
        {
            replacement = rule.replacement;
        }
        else if (rule.options == RO::KeepOriginalAsComment)
        {
            replacement = "# " + commentPrefix + "\\1\n" + rule.replacement;
        }

        result.replace(re, replacement);
    }

    return result;
}

void apply_replacement_rules(
    const QVector<ReplacementRule> &rules,
    VMEScriptConfig *scriptConfig,
    const QString &commentPrefix = DefaultReplacementCommentPrefix)
{
    auto scriptContents = scriptConfig->getScriptContents();
    QString updated = apply_replacement_rules(rules, scriptContents, commentPrefix);
    scriptConfig->setScriptContents(updated);
}

// For event level scripts event_daq_start, event_daq_stop,
// readout_cylce_start, readout_cycle_end.
static const QVector<ReplacementRule> EventRules =
{
    {
        R"-(^# Start acquisition sequence using the default multicast address 0xbb\s*$)-",
        "# Run the start-acquisition-sequence for all modules via the events multicast address.",
        ReplacementRule::Options::ReplaceOnly,
    },
    {
        R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}00603a\s+0.*)$)-",
        "writeabs a32 d16 0x${mesy_mcst}00603a      0   # stop acq",
    },
    {
        R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}006090\s+3.*)$)-",
        "writeabs a32 d16 0x${mesy_mcst}006090      3   # reset CTRA and CTRB",
    },
    {
        R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}00603c\s+1.*)$)-",
        "writeabs a32 d16 0x${mesy_mcst}00603c      1   # FIFO reset",
    },
    {
        R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}00603a\s+1.*)$)-",
        "writeabs a32 d16 0x${mesy_mcst}00603a      1   # start acq",
    },
    {
        R"-(^(\s*writeabs\s+a32\s+d16\s+0x[0-9a-fA-F]{2}006034\s+1.*)$)-",
        "writeabs a32 d16 0x${mesy_mcst}006034      1   # readout reset",
    },
};

static const QVector<ReplacementRule> ModuleRules =
{
    // irq level
    // Note: irq 0 is _not_ replaced. The assumption is that the user enabled
    // the irq for a specific module only and disabled it for others.
    {
        R"-(^(\s*0x6010\s+[1-7]{1}.*)$)-",
        "0x6010 ${sys_irq}                                  # irq level",
    },

    // remove the irq vector line
    {
        R"-(^(\s*0x6012\s+0.*)$)-",
        "",
        ReplacementRule::Options::ReplaceOnly,
    },

    // fifo irq threshold
    {
        R"-(^(\s*0x601E\s+[0-9]+.*)$)-",
        "0x601E $(${mesy_readout_num_events} + 1)           # IRQ-FIFO threshold, events",
    },

    {
        R"-(^(\s*0x601A\s+[0-9]+.*)$)-",
        "0x601A ${mesy_readout_num_events}                  # multi event mode == 0x3 -> "
        "Berr is emitted when more or equal the",
    },

    // end of event marker
    {
        R"-(^(\s*0x6038\s+.*)$)-",
        "0x6038 ${mesy_eoe_marker}                          # End Of Event marking",
    },

    // set mcst
    {
        R"-(^(\s*0x6024\s+0x[0-9a-fA-F]{2}).*$)-",
        "0x6024 0x${mesy_mcst}                              # Set the 8 high-order bits of the MCST address",
    },
};

// Changes between format versions 3 and 4.
// - mdpp16 typename was changed to mdpp16_scp in the summer of 2019. This
//   conversion updates the type name. This makes it so that the vme/analysis
//   template system recogizes the module again and thus things like creating
//   analysis filters and multi-event-splitting will work again.
//   In the case a config was saved with the unknown module type the module
//   type in the JSON data became an empty string. To fix this if the module
//   type is empty and the module name contains the string "mdpp16" the module
//   type is also set to 'mdpp16_scp'
//
// - The variable system was introduced and the vme templates have been updated
//   to make use of the standard variables.
//   Without any changes an existing setup will continue to work as before.
//   Problems arise when adding a new VME module to an existing VME event.
//   Things will break because the new module templates will reference
//   variables that should have been set at event scope but do not exist in the
//   older config version.
//   To fix this a set of standard variables is added to each EventConfig in
//   the setup:
//   * sys_irq is taken from the events TriggerCondition and irqLevel.
//   * mesy_mcst is guessed by taking a look at the 'daq_start' script,
//     specifically a write to module register 0x603a (start/stop acq). The
//     guessed value or a default value of 0xbb is set.
//   * mesy_readout_num_events is set to 1
//   * mesy_eoe_marker is set to 1 (timestamp mode)
//
// - Existing vme scripts do not reference any of the variables yet.
//   This means changes to e.g. an events irq value or to any of the mesy_*
//   variables will have an effect on newly created events and modules but not
//   on existing ones.
//   To remedy this issue event and module scripts are updated to reference the
//   current set of variables.
//   The update is performed by matching expected lines from the old template
//   files against regular expressions. If a match is found the old line is
//   kept as a comment and the updated version is inserted below.
//   All the write commands that have been in the module and event templates
//   for a while will be updated. Non-standard writes which do not match the
//   regular expression won't be touched and the containing setup will have to
//   be updated manually by the user.

static QJsonObject v3_to_v4(QJsonObject json)
{
    auto fix_mdpp16_module_typename = [] (QJsonObject json) -> QJsonObject
    {
        qDebug() << __PRETTY_FUNCTION__ << "changing 'mdpp16' module type name to 'mdpp16_scp'";

        auto eventsArray = json["events"].toArray();

        for (int eventIndex = 0;
             eventIndex < eventsArray.size();
             ++eventIndex)
        {
            QJsonObject eventJson = eventsArray[eventIndex].toObject();
            auto modulesArray = eventJson["modules"].toArray();

            for (int moduleIndex = 0;
                 moduleIndex < modulesArray.size();
                 ++moduleIndex)
            {
                QJsonObject moduleJson = modulesArray[moduleIndex].toObject();

                // Case1: old mdpp16 type name
                if (moduleJson["type"].toString() == "mdpp16")
                {
                    moduleJson["type"] = QString("mdpp16_scp");
                }
                // Case2: type name is empty. This happened when loading a
                // setup before this conversion was introduced and resaving it.
                // mvme wasn't able to find module meta information, thus
                // ModuleConfig.m_meta was empty and when writing the config
                // back out the typename was set to an empty string.
                else if (moduleJson["type"].toString().isEmpty()
                         && moduleJson["name"].toString().contains(
                             "mdpp16", Qt::CaseInsensitive))
                {
                    moduleJson["type"] = QString("mdpp16_scp");
                }

                modulesArray[moduleIndex] = moduleJson;
            }

            eventJson["modules"] = modulesArray;
            eventsArray[eventIndex] = eventJson;
        }

        json["events"] = eventsArray;

        return json;
    };

    auto add_event_variables = [] (QJsonObject json)
    {
        qDebug() << __PRETTY_FUNCTION__ << "adding default event variables";

        auto eventsArray = json["events"].toArray();

        for (int eventIndex = 0;
             eventIndex < eventsArray.size();
             ++eventIndex)
        {
            QJsonObject eventJson = eventsArray[eventIndex].toObject();

            auto eventConfig = std::make_unique<EventConfig>();
            eventConfig->read(eventJson);

            // Try to get the events multicast address by looking at the daq_start script.
            u8 mcst = 0u;

            if (auto daqStart = eventConfig->vmeScripts["daq_start"])
                mcst = mvme::vme_config_json::guess_event_mcst(daqStart->getScriptContents());

            // Set the proper irq value depending on triggerCondition and irqLevel.
            u8 irq = (eventConfig->triggerCondition == TriggerCondition::Interrupt
                      ? eventConfig->irqLevel
                      : 0u);

            eventConfig->setVariables(make_standard_event_variables(irq, mcst));

            // Look at the first module in the event and use its 'VME Interface
            // Settings' script to guess values for 'mesy_eoe_marker' and
            // 'mesy_reaodut_num_events'.
            if (!eventConfig->getModuleConfigs().isEmpty())
            {
                auto firstModule = eventConfig->getModuleConfigs().first();
                auto vmeSettings = firstModule->findChildByName<VMEScriptConfig *>(
                    QSL("VME Interface Settings"), false);

                if (vmeSettings)
                {
                    u32 eoe_marker = guess_module_mesy_eoe_marker(vmeSettings->getScriptContents());
                    u32 num_events = guess_module_readout_num_events(vmeSettings->getScriptContents());

                    eventConfig->setVariableValue("mesy_eoe_marker", QString::number(eoe_marker));
                    eventConfig->setVariableValue("mesy_readout_num_events", QString::number(num_events));
                }
            }

            eventJson = {};
            eventConfig->write(eventJson);

            eventsArray[eventIndex] = eventJson;
        }

        json["events"] = eventsArray;

        return json;
    };

    auto update_event_scripts = [] (QJsonObject json)
    {
        qDebug() << __PRETTY_FUNCTION__ << "updating vme event scripts";

        auto eventsArray = json["events"].toArray();

        for (int eventIndex = 0;
             eventIndex < eventsArray.size();
             ++eventIndex)
        {
            QJsonObject eventJson = eventsArray[eventIndex].toObject();

            auto eventConfig = std::make_unique<EventConfig>();
            eventConfig->read(eventJson);

            for (auto scriptConfig: eventConfig->vmeScripts.values())
            {
                apply_replacement_rules(EventRules, scriptConfig);
            }

            eventJson = {};
            eventConfig->write(eventJson);
            eventsArray[eventIndex] = eventJson;
        }

        json["events"] = eventsArray;
        return json;
    };

    auto update_module_scripts = [] (QJsonObject json)
    {
        qDebug() << __PRETTY_FUNCTION__ << "updating vme module scripts";

        auto eventsArray = json["events"].toArray();

        for (int eventIndex = 0;
             eventIndex < eventsArray.size();
             ++eventIndex)
        {
            QJsonObject eventJson = eventsArray[eventIndex].toObject();

            auto eventConfig = std::make_unique<EventConfig>();
            eventConfig->read(eventJson);

            for (auto moduleConfig: eventConfig->getModuleConfigs())
            {
                apply_replacement_rules(ModuleRules, moduleConfig->getResetScript());
                apply_replacement_rules(ModuleRules, moduleConfig->getReadoutScript());

                for (auto initScript: moduleConfig->getInitScripts())
                    apply_replacement_rules(ModuleRules, initScript);
            }

            eventJson = {};
            eventConfig->write(eventJson);
            eventsArray[eventIndex] = eventJson;
        }

        json["events"] = eventsArray;
        return json;
    };

    qDebug() << "VME config conversion" << __PRETTY_FUNCTION__;

    json = fix_mdpp16_module_typename(json);

    json = add_event_variables(json);

    json = update_event_scripts(json);

    json = update_module_scripts(json);

    return json;
}

using VMEConfigConverter = std::function<QJsonObject (QJsonObject)>;

static QVector<VMEConfigConverter> VMEConfigConverters =
{
    nullptr,
    v1_to_v2,
    v2_to_v3,
    v3_to_v4
};

} // end anon namespace

namespace mvme
{
namespace vme_config_json
{

int get_vmeconfig_version(const QJsonObject &json)
{
    return json["properties"].toObject()["version"].toInt(1);
};

QJsonObject convert_vmeconfig_to_current_version(QJsonObject json)
{
    qDebug() << "<<<<<<<<<<<<< begin vme config json conversion <<<<<<<<<<<<<<<<<";
    int version;

    while ((version = get_vmeconfig_version(json)) < GetCurrentVMEConfigVersion())
    {
        auto converter = VMEConfigConverters.value(version);

        if (!converter)
            break;

        json = converter(json);
        json["properties"] = QJsonObject({{"version", version+1}});

        qDebug() << __PRETTY_FUNCTION__ << "converted VMEConfig from version"
            << version << "to version" << version+1;
    }

    qDebug() << ">>>>>>>>>>>>> end vme config json conversion >>>>>>>>>>>>>>>>>";

    return json;
}

} // end namespace vme_config_json
} // end namespace mvme
