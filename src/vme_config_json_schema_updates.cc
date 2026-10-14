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

using namespace mesytec::mvme::vme_config;

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

// defaults to 1 (timestamp)
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

namespace mvme::vme_config::json_schema
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

} // end namespace json_schema

namespace
{

using namespace mesytec::mvme::vme_config::json_schema;

using VMEConfigConverter = std::function<QJsonObject (
    QJsonObject oldJson, Logger logger, const SchemaUpdateOptions & options)>;

/* Module script storage changed:
 * vme_scripts.readout              -> vmeReadout
 * vme_scripts.reset                -> vmeReset
 * vme_scripts.parameters           -> initScripts[0]
 * vme_scripts.readout_settings     -> initScripts[1]
 */
static QJsonObject v1_to_v2(QJsonObject json, Logger /*logger*/, const SchemaUpdateOptions & /*options*/)
{
    //qDebug() << "VME config conversion" << __PRETTY_FUNCTION__;

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
static QJsonObject v2_to_v3(QJsonObject json, Logger /*logger*/, const SchemaUpdateOptions & /*options*/)
{
    //qDebug() << "VME config conversion" << __PRETTY_FUNCTION__;

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

std::pair<QString, size_t>
apply_replacement_rules(
    const QVector<ReplacementRule> &rules,
    const QString &input,
    const QString &commentPrefix = DefaultReplacementCommentPrefix)
{
    using RO = ReplacementRule::Options;

    QString result = input;
    size_t replaceCount = 0u;

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

        replaceCount += result.count(re);
        result.replace(re, replacement);
    }

    return std::make_pair(result, replaceCount);
}

size_t apply_replacement_rules(
    const QVector<ReplacementRule> &rules,
    VMEScriptConfig *scriptConfig,
    const QString &commentPrefix = DefaultReplacementCommentPrefix)
{
    QString updated;
    size_t replaceCount = 0u;
    auto scriptContents = scriptConfig->getScriptContents();

    std::tie(updated, replaceCount) = apply_replacement_rules(rules, scriptContents, commentPrefix);

    scriptConfig->setScriptContents(updated);

    return replaceCount;
}

// For event level scripts event_daq_start, event_daq_stop,
// readout_cylce_start, readout_cycle_end.
static const QVector<ReplacementRule> EventRules =
{
    {
        // Replace the comment at the top of the script.
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
// - mdpp16 typename was changed to mdpp16_scp in the summer of 2019. I missed
//   the negative effect this had.
//   This conversion updates the type name. This makes it so that the
//   vme/analysis template system recogizes the module again and thus things
//   like creating analysis filters and multi-event-splitting will work again.
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
//   * mesy_readout_num_events and mesy_eoe_marker are guessed by looking at
//     the respective register writes of the first vme module in the event.
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

static QJsonObject v3_to_v4(QJsonObject json, Logger logger, const SchemaUpdateOptions &options)
{
    auto fix_mdpp16_module_typename = [logger] (QJsonObject json) -> QJsonObject
    {
        //qDebug() << __PRETTY_FUNCTION__ << "changing 'mdpp16' module type name to 'mdpp16_scp'";

        auto eventsArray = json["events"].toArray();

        for (int eventIndex = 0;
             eventIndex < eventsArray.size();
             ++eventIndex)
        {
            QJsonObject eventJson = eventsArray[eventIndex].toObject();

            auto eventName = eventJson["name"].toString();
            auto modulesArray = eventJson["modules"].toArray();

            for (int moduleIndex = 0;
                 moduleIndex < modulesArray.size();
                 ++moduleIndex)
            {
                QJsonObject moduleJson = modulesArray[moduleIndex].toObject();
                auto moduleName = moduleJson["name"].toString();

                // Case1: old mdpp16 type name
                if (moduleJson["type"].toString() == "mdpp16")
                {
                    moduleJson["type"] = QString("mdpp16_scp");

                    logger(QSL("Module '%1': updating typename from 'mdpp16' to 'mdpp16_scp'")
                           .arg(eventName + "/" + moduleName));
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

                    logger(QSL("Module '%1': setting typename to 'mdpp16_scp'")
                           .arg(eventName + "." + moduleName));
                }

                modulesArray[moduleIndex] = moduleJson;
            }

            eventJson["modules"] = modulesArray;
            eventsArray[eventIndex] = eventJson;
        }

        json["events"] = eventsArray;

        return json;
    };

#if 0
    // Add a set of default variables to each EventConfig.
    // Note: this version creates an EventConfig object from the JSON, modifies
    // the object and serializes it back to JSON. This apporach has the
    // drawback that the EventConfig class has to be able to deserialize
    // correctly from the old JSON data which in effect means that no
    // substantial changes can be made to the class.
    auto add_event_variables = [logger] (QJsonObject json)
    {
        qDebug() << __PRETTY_FUNCTION__ << "adding event variables";

        auto eventsArray = json["events"].toArray();

        for (int eventIndex = 0;
             eventIndex < eventsArray.size();
             ++eventIndex)
        {
            QJsonObject eventJson = eventsArray[eventIndex].toObject();
            auto eventName = eventJson["name"].toString();

            auto eventConfig = std::make_unique<EventConfig>();
            eventConfig->read(eventJson);

            // Try to get the events multicast address by looking at the daq_start script.
            u8 mcst = 0u;

            if (auto daqStart = eventConfig->vmeScripts["daq_start"])
                mcst = mvme::vme_config::json_schema::guess_event_mcst(daqStart->getScriptContents());

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

            logger(QSL("Adding standard variables to event '%1': %2")
                   .arg(eventName).arg(eventConfig->getVariables().symbolNames().join(", ")));

            eventJson = {};
            eventConfig->write(eventJson);

            eventsArray[eventIndex] = eventJson;
        }

        json["events"] = eventsArray;

        return json;
    };
#else
    // Add a set of default variables to each EventConfig.
    // Note: this version works directly on the serialzed JSON data instead of
    // creating an EventConfig object. This decouples the logic from the
    // current implementation of the EventConfig class and allows to make
    // structural changes as long as the resulting JSON can be deserialized
    // correctly. The drawback is that the code is slightly more complex.
    auto add_event_variables = [logger] (QJsonObject json)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "adding event variables";

        auto eventsArray = json["events"].toArray();

        for (int eventIndex = 0;
             eventIndex < eventsArray.size();
             ++eventIndex)
        {
            QJsonObject eventJson = eventsArray[eventIndex].toObject();
            auto eventName = eventJson["name"].toString();

            auto scriptsJson = eventJson["vme_scripts"].toObject();

            // Try to get the events multicast address by looking at the daq_start script.
            u8 mcst = mvme::vme_config::json_schema::guess_event_mcst(
                scriptsJson["daq_start"].toObject()["vme_script"].toString());

            // Set the proper irq value depending on triggerCondition and irqLevel.
            u8 irq = 0u;

            if (eventJson["triggerCondition"].toString() == QSL("Interrupt"))
                irq = eventJson["irqLevel"].toInt();

            // Create the variables for the event object.
            auto eventVariables = make_standard_event_variables(irq, mcst);

            // Look at the first module in the event and use its 'VME Interface
            // Settings' script to guess values for 'mesy_eoe_marker' and
            // 'mesy_reaodut_num_events'.
            auto modulesArray = eventJson["modules"].toArray();

            if (!modulesArray.isEmpty())
            {
                auto firstModuleJson = modulesArray[0].toObject();
                auto initScriptsJson = firstModuleJson["initScripts"].toArray();

                for (int scriptIndex = 0; scriptIndex < initScriptsJson.size(); ++scriptIndex)
                {
                    auto scriptJson = initScriptsJson[scriptIndex].toObject();

                    if (scriptJson["name"].toString() == QSL("VME Interface Settings"))
                    {
                        u32 eoe_marker = guess_module_mesy_eoe_marker(scriptJson["vme_script"].toString());
                        u32 num_events = guess_module_readout_num_events(scriptJson["vme_script"].toString());

                        eventVariables["mesy_eoe_marker"].value = QString::number(eoe_marker);
                        eventVariables["mesy_readout_num_events"].value = QString::number(num_events);
                    }
                }
            }

            logger(QSL("Adding standard variables to event '%1': %2")
                   .arg(eventName).arg(eventVariables.symbolNames().join(", ")));

            eventJson["variable_table"] = vme_script::to_json(eventVariables);
            eventsArray[eventIndex] = eventJson;
        }

        json["events"] = eventsArray;

        return json;
    };
#endif

    auto update_event_scripts = [logger] (QJsonObject json)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "updating vme event scripts";

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
                size_t replaceCount = apply_replacement_rules(EventRules, scriptConfig);

                if (replaceCount)
                {
                    logger(QSL("Updating event script '%1': %2 changes")
                           .arg(scriptConfig->getObjectPath())
                           .arg(replaceCount));
                }
            }

            eventJson = {};
            eventConfig->write(eventJson);
            eventsArray[eventIndex] = eventJson;
        }

        json["events"] = eventsArray;
        return json;
    };

    auto update_module_scripts = [logger] (QJsonObject json)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "updating vme module scripts";

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
                auto scripts = moduleConfig->getInitScripts();
                scripts.push_back(moduleConfig->getResetScript());
                scripts.push_back(moduleConfig->getReadoutScript());

                for (auto script: scripts)
                {
                    size_t replaceCount = apply_replacement_rules(ModuleRules, script);

                    if (replaceCount)
                    {
                        logger(QSL("Updating module script '%1': %2 changes")
                               .arg(script->getObjectPath())
                               .arg(replaceCount));
                    }
                }
            }

            eventJson = {};
            eventConfig->write(eventJson);
            eventsArray[eventIndex] = eventJson;
        }

        json["events"] = eventsArray;
        return json;
    };

    //qDebug() << "VME config conversion" << __PRETTY_FUNCTION__;

    json = fix_mdpp16_module_typename(json);

    if (!options.skip_v4_VMEScriptVariableUpdate)
    {
        json = add_event_variables(json);

        json = update_event_scripts(json);

        json = update_module_scripts(json);
    }

    return json;
}

static QVector<VMEConfigConverter> VMEConfigConverters =
{
    nullptr,
    v1_to_v2,
    v2_to_v3,
    v3_to_v4,
};

} // end anon namespace

namespace mesytec::mvme::vme_config::json_schema
{

void set_vmeconfig_version(QJsonObject &json, int version)
{
    json["properties"] = QJsonObject({{"version", version}});
}

int get_vmeconfig_version(const QJsonObject &json)
{
    return json["properties"].toObject()["version"].toInt(1);
};

QJsonObject convert_vmeconfig_to_current_version(
    QJsonObject json, Logger logger, const SchemaUpdateOptions &options)
{
    //qDebug() << "<<<<<<<<<<<<< begin vme config json conversion <<<<<<<<<<<<<<<<<";

    if (!logger)
        logger = [] (const QString &) {};

    int version = get_vmeconfig_version(json);

    // Edge case: old configs from a time (~2017)  where only the VMUSB
    // controller was supported did not have a separate key for storing VME
    // controller type and settings. When reading these configs now they default
    // to MVLC_ETH as the controller which in turns means the format must be
    // interpreted as an mvlclst file. This breaks because the format is
    // actually the initial mvmelst format.
    // Fix: set vme_controller.type = VMUSB in the json data.
    if (!json.contains("vme_controller"))
    {
        QJsonObject vmeControllerJson;
        vmeControllerJson["type"] = "VMUSB";
        json["vme_controller"] = vmeControllerJson;
    }

    while ((version = get_vmeconfig_version(json)) < GetCurrentVMEConfigVersion())
    {
        auto converter = VMEConfigConverters.value(version);

        if (!converter)
            break;

        json = converter(json, logger, options);
        set_vmeconfig_version(json, version + 1);

        //qDebug() << __PRETTY_FUNCTION__ << "converted VMEConfig from version"
        //    << version << "to version" << version+1;
    }

    //qDebug() << ">>>>>>>>>>>>> end vme config json conversion >>>>>>>>>>>>>>>>>";

    return json;
}

} // end namespace json_schema
