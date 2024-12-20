#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <iostream>
#include <spdlog/spdlog.h>
#include "git_sha1.h"
#include "template_system.h"
#include "vme_config_util.h"
#include "mvlc/vmeconfig_to_crateconfig.h"

// Writes out vme module default initialization and readout commands in mvlc command format as json.
// mvlc base initialization for trigger io is added.

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::warn);
    QCoreApplication app(argc, argv);

    auto logger = [](const QString &msg) { qDebug() << msg; };

    auto templates = vats::read_templates(logger);

    QJsonArray modules_out;

    for (const auto &mm: templates.moduleMetas)
    {
        if (mm.vendorName != "mesytec")
            continue;

        ContainerObject parent;
        parent.setVariables(mesytec::mvme::vme_config::make_standard_event_variables(0, 0));

        auto moduleConfig_ = new ModuleConfig(&parent);
        auto &moduleConfig = *moduleConfig_;

        if (!mm.moduleJson.empty())
        {
            // New style template from a single json file.
            mesytec::mvme::vme_config::load_moduleconfig_from_modulejson(moduleConfig, mm.moduleJson);
        }
        else
        {
            // Old style template from multiple .vme files
            moduleConfig.getReadoutScript()->setObjectName(mm.templates.readout.name);
            moduleConfig.getReadoutScript()->setScriptContents(mm.templates.readout.contents);

            moduleConfig.getResetScript()->setObjectName(mm.templates.reset.name);
            moduleConfig.getResetScript()->setScriptContents(mm.templates.reset.contents);

            for (const auto &vmeTemplate: mm.templates.init)
            {
                moduleConfig.addInitScript(new VMEScriptConfig(
                    vmeTemplate.name, vmeTemplate.contents));
            }
        }

        moduleConfig.setObjectName(mm.typeName);
        moduleConfig.setVariables(mesytec::mvme::vme_config::variable_symboltable_from_module_meta(mm));

        try
        {

        mesytec::mvlc::StackCommandBuilder builder(mm.typeName.toStdString() + ".init");
        builder.addGroup("reset", mesytec::mvme::convert_script(moduleConfig.getResetScript()));

        for (const auto &initScript: moduleConfig.getInitScripts())
        {
            auto name = initScript->objectName().replace(" ", "_").toLower();;
            builder.addGroup(name.toLower().toStdString(), mesytec::mvme::convert_script(initScript));
        }

        QJsonObject init_out = QJsonDocument::fromJson(to_json(builder).c_str()).object();

        builder = mesytec::mvlc::StackCommandBuilder(mm.typeName.toStdString() + ".readout");
        builder.addGroup("readout", mesytec::mvme::convert_script(moduleConfig.getReadoutScript()));

        QJsonObject readout_out = QJsonDocument::fromJson(to_json(builder).c_str()).object();

        QJsonObject module_out;
        module_out["name"] = mm.typeName;
        module_out["init"] = init_out;
        module_out["readout"] = readout_out;
        modules_out.append(module_out);
        }
        catch (const vme_script::ParseError &e)
        {
            spdlog::error("Error parsing script: {}", e.toString().toStdString());
            return 1;
        }
    }

    VMEConfig vmeConfig;
    if (auto mvlcTriggerIO = vmeConfig.getGlobalObjectRoot().findChild<VMEScriptConfig *>(
            QSL("mvlc_trigger_io")))
    {
        mvlcTriggerIO->setScriptContents(vats::read_default_mvlc_trigger_io_script().contents);
    }

    auto crateConfig = mesytec::mvme::vmeconfig_to_crateconfig(&vmeConfig);
    QJsonObject crateconfig_out = QJsonDocument::fromJson(to_json(crateConfig).c_str()).object();


    QJsonObject out;
    out["mvme_version"] = mvme_git_version();
    out["modules"] = modules_out;
    out["crateconfig"] = crateconfig_out;

    QJsonDocument doc(out);
    std::cout << doc.toJson().toStdString() << std::endl;

    return 0;
}
