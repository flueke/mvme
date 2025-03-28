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
#include "template_system.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTextStream>
#include <limits>
#include <spdlog/spdlog.h>

#include "qt_util.h"
#include "vme_config_util.h"

namespace
{
    using namespace vats;

    void do_log(const QString &msg, TemplateLogger logger)
    {
        if (logger)
            logger(msg);
    };

    QString read_file(const QString &fileName, TemplateLogger logger)
    {
        QFile inFile(fileName);

        if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            do_log(QString("Error opening %1 for reading: %2")
                   .arg(fileName)
                   .arg(inFile.errorString()),
                   logger);

            return QString();
        }

        QTextStream inStream(&inFile);
        return inStream.readAll();
    }

    QJsonDocument read_json_file(const QString &fileName, TemplateLogger logger)
    {
        QString data = read_file(fileName, logger);

        QJsonParseError parseError;
        QJsonDocument doc(QJsonDocument::fromJson(data.toUtf8(), &parseError));

        if (parseError.error != QJsonParseError::NoError)
        {
            do_log(QString("JSON parse error in file %1: %2 at offset %3")
                   .arg(fileName)
                   .arg(parseError.errorString())
                   .arg(parseError.offset),
                   logger);
        }

        return doc;
    }

    VMETemplate read_vme_template(const QString &path, const QString &name, TemplateLogger logger, const QDir &baseDir)
    {
        VMETemplate result;
        result.contents = read_file(path, logger);
        result.name = name;
        result.sourceFileName = baseDir.relativeFilePath(path);
        return result;
    }

    VMEModuleTemplates read_module_templates(const QString &path, TemplateLogger logger, const QDir &baseDir)
    {
        QDir dir(path);
        VMEModuleTemplates result;
        result.readout = read_vme_template(dir.filePath("readout.vmescript"),
                                           QSL("Module Readout"), logger, baseDir);
        result.reset   = read_vme_template(dir.filePath("reset.vmescript"),
                                           QSL("Module Reset"), logger, baseDir);

        auto initEntries = dir.entryList({ QSL("init-*.vmescript") },
                                         QDir::Files | QDir::Readable, QDir::Name);

        for (const auto &fileName: initEntries)
        {
            VMETemplate vmeTemplate;
            QString entryFilePath = dir.filePath(fileName);
            vmeTemplate.contents = read_file(entryFilePath, logger);
            vmeTemplate.sourceFileName = baseDir.relativeFilePath(entryFilePath);

            QRegularExpression re;
            re.setPattern("^init-\\d\\d-(.*)\\.vmescript$");
            auto match = re.match(fileName);

            if (!match.hasMatch())
            {
                re.setPattern("^init-(.*)\\.vmescript$");
                match = re.match(fileName);
            }

            if (match.hasMatch())
            {
                vmeTemplate.name = match.captured(1);
            }
            else
            {
                vmeTemplate.name = QFileInfo(fileName).baseName();
            }

            result.init.push_back(vmeTemplate);
        }

        return result;
    }
}

namespace vats
{

bool operator==(const VMETemplate &ta, const VMETemplate &tb)
{
    return (ta.contents == tb.contents
            && ta.name == tb.name
            && ta.sourceFileName == tb.sourceFileName
           );
}

bool operator==(const VMEModuleTemplates &mta, const VMEModuleTemplates &mtb)
{
    return (mta.reset == mtb.reset
            && mta.readout == mtb.readout
            && mta.init == mtb.init
           );
}

bool operator==(const VMEModuleEventHeaderFilter &a, const VMEModuleEventHeaderFilter &b)
{
    return (a.filterString == b.filterString
            && a.description == b.description
           );
}

bool operator==(const VMEModuleMeta &mma, const VMEModuleMeta &mmb)
{
    return (mma.typeId == mmb.typeId
            && mma.typeName == mmb.typeName
            && mma.displayName == mmb.displayName
            && mma.vendorName == mmb.vendorName
            && mma.templates == mmb.templates
            && mma.eventSizeFilters == mmb.eventSizeFilters
            && mma.vmeAddress == mmb.vmeAddress
            && mma.templatePath == mmb.templatePath
           );
}

bool operator!=(const VMETemplate &ta, const VMETemplate &tb)
{
    return !(ta == tb);
}

bool operator!=(const VMEModuleTemplates &mta, const VMEModuleTemplates &mtb)
{
    return !(mta == mtb);
}

bool operator!=(const VMEModuleMeta &mma, const VMEModuleMeta &mmb)
{
    return !(mma == mmb);
}

bool operator!=(const VMEModuleEventHeaderFilter &a, const VMEModuleEventHeaderFilter &b)
{
    return !(a == b);
}

QDebug operator<<(QDebug debug, const MVMETemplates &templates);

QString get_template_path()
{
    QString templatePath = QCoreApplication::applicationDirPath() + QSL("/../templates");
    return templatePath;
}

MVMETemplates read_templates(TemplateLogger logger)
{
    QString templatePath = get_template_path();
    do_log(QString("Loading templates from %1").arg(templatePath), logger);
    return read_templates_from_path(templatePath, logger);
}

VMEModuleMeta modulemeta_from_json(const QJsonObject &json)
{
    VMEModuleMeta mm;
    mm.typeId = json["typeId"].toInt(0);
    mm.typeName = json["typeName"].toString();
    mm.displayName = json["displayName"].toString();
    mm.vendorName = json["vendorName"].toString();
    if (json["eventHeaderFilters"].isArray())
    {
        // Handle new-style, list-of-filter-structures templates.
        for (const auto &filterDef: json["eventHeaderFilters"].toArray())
        {
            VMEModuleEventHeaderFilter filter;
            filter.filterString = filterDef.toObject()["filter"].toString().toLocal8Bit();
            filter.description = filterDef.toObject()["description"].toString();
            mm.eventSizeFilters.push_back(filter);
        }
    }
    else
    {
        // Handle old-style, single filter string templates.
        VMEModuleEventHeaderFilter filter;
        filter.filterString = json["eventHeaderFilter"].toString().toLocal8Bit();
        filter.description = "Default module event size extraction filter";
        mm.eventSizeFilters.push_back(filter);
    }
    mm.vmeAddress = json["vmeAddress"].toString().toUInt(nullptr, 0);
    mm.variables = json["variables"].toArray();
    return mm;
}

QJsonObject modulemeta_to_json(const VMEModuleMeta &mm)
{
    QJsonObject metaJ;
    metaJ["typeName"] = mm.typeName;
    metaJ["typeId"] = static_cast<int>(mm.typeId);
    metaJ["displayName"] = mm.displayName;
    metaJ["vendorName"] = mm.vendorName;
    QJsonArray filtersJ;
    for (const auto &filterDef: mm.eventSizeFilters)
    {
        QJsonObject filterJ;
        filterJ["filter"] = QString::fromLocal8Bit(filterDef.filterString);
        filterJ["description"] = filterDef.description;
        filtersJ.append(filterJ);
    }
    metaJ["eventHeaderFilters"] = filtersJ;
    metaJ["vmeAddress"] = QString("0x%1").arg(mm.vmeAddress, 8, 16, QLatin1Char('0'));
    metaJ["variables"] = mm.variables;

    return metaJ;
}

// TODO:
// - check for duplicate typeIds
// - check for duplicate typeNames
// - check for empty typeNames

// Handles both the old style (multiple .vme files) and new style (single
// .mvmemodule file) module templates.
MVMETemplates read_templates_from_path(const QString &path, TemplateLogger logger)
{
    const QDir baseDir(path);
    MVMETemplates result;

    result.eventTemplates.daqStart          = read_vme_template(
        baseDir.filePath(QSL("event/event_daq_start.vmescript")),
        QSL("DAQ Start"), logger, baseDir);

    result.eventTemplates.daqStop           = read_vme_template(
        baseDir.filePath(QSL("event/event_daq_stop.vmescript")),
        QSL("DAQ Stop"), logger, baseDir);

    result.eventTemplates.readoutCycleStart = read_vme_template(
        baseDir.filePath(QSL("event/readout_cycle_start.vmescript")),
        QSL("Cycle Start"), logger, baseDir);

    result.eventTemplates.readoutCycleEnd   = read_vme_template(
        baseDir.filePath(QSL("event/readout_cycle_end.vmescript")),
        QSL("Cycle End"), logger, baseDir);

    auto dirEntries = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);

    for (const auto &dirName: dirEntries)
    {
        QDir moduleDir(baseDir.filePath(dirName));

        if (moduleDir.exists(QSL("module_info.json")))
        {
            // Old style format with a module_info.json and separate .vme template files.
            auto moduleInfo = read_json_file(moduleDir.filePath(QSL("module_info.json")), logger);

            if (moduleInfo.isEmpty())
            {
                do_log(QString("Skipping %1: invalid module_info.json").arg(moduleDir.path()), logger);
                continue;
            }

            auto json = moduleInfo.object();

            s32 moduleType = json["typeId"].toInt();

            if (moduleType <= 0 || moduleType > std::numeric_limits<u8>::max())
            {
                do_log(QString("%1: module typeId out of range (valid range is [1, 255])")
                       .arg(moduleDir.filePath(QSL("moduleInfo.json"))),
                       logger);
                continue;
            }

            auto mm = modulemeta_from_json(json);
            mm.templates = read_module_templates(moduleDir.filePath(QSL("vme")), logger, baseDir);
            mm.templatePath = moduleDir.path();
            result.moduleMetas.push_back(mm);
        }
        else
        {
            // New style format where everything is stored in a single
            // .mvmemodule json file. Supports multiple .mvmemodule files in a
            // single directory.
            auto candidates = moduleDir.entryList({ "*.mvmemodule" }, QDir::Files, QDir::Name);

            for (const auto &c: candidates)
            {
                auto l = [] (const QString &msg) { spdlog::error(msg.toStdString()); };
                auto moduleJson = read_json_file(moduleDir.filePath(c), l).object();
                auto mm = modulemeta_from_json(moduleJson["ModuleMeta"].toObject());
                mm.templatePath = moduleDir.path();
                mm.templateFile = moduleDir.filePath(c);
                mm.moduleJson = moduleJson;
                result.moduleMetas.push_back(mm);
            }
        }
    }

    return result;
}

QString get_module_path(const QString &moduleTypeName)
{
    auto templates = read_templates();

    for (const auto &mm: templates.moduleMetas)
    {
        if (mm.typeName == moduleTypeName)
            return mm.templatePath;
    }

    return QString();
}

static QTextStream &do_indent(QTextStream &out, int indent)
{
    for (int i=0; i<indent; ++i)
        out << " ";

    return out;
}

static QTextStream &print(QTextStream &out, const VMETemplate &vmeTemplate, int indent = 0)
{
    do_indent(out, indent) << "name=" << vmeTemplate.name << endl;
    do_indent(out, indent) << "source=" << vmeTemplate.sourceFileName << endl;
    do_indent(out, indent) << "size=" << vmeTemplate.contents.size() << endl;

    return out;
}

static QTextStream &print(QTextStream &out, const VMEModuleMeta &module, int indent = 0)
{
    do_indent(out, indent) << "typeId=" << static_cast<u32>(module.typeId) << endl;
    do_indent(out, indent) << "typeName=" << module.typeName << endl;
    do_indent(out, indent) << "displayName=" << module.displayName << endl;

    for (const auto &filterDef: module.eventSizeFilters)
    {
        do_indent(out, indent) << "eventHeaderFilters=" << filterDef.filterString
            << ", description=" << filterDef.description << endl;
    }

    do_indent(out, indent) << "vmeBaseAddress="
        << (QString("0x%1").arg(module.vmeAddress, 8, 16, QLatin1Char('0'))) << endl;

    do_indent(out, indent) << "templates:" << endl;

    do_indent(out, indent+2) << "reset:" << endl;
    print(out, module.templates.reset, indent+4);

    do_indent(out, indent+2) << "readout:" << endl;
    print(out, module.templates.readout, indent+4);

    do_indent(out, indent+2) << "init (" << module.templates.init.size() << " templates):" << endl;

    int idx = 0;
    for (const auto &vmeTemplate: module.templates.init)
    {
        do_indent(out, indent+4) << idx << endl;
        print(out, vmeTemplate, indent+6);

        ++idx;
    }

    if (module.variables.size())
    {
        do_indent(out, indent) << "variables:" << endl;

        for (int i=0; i<module.variables.size(); ++i)
        {
            auto varEntry = module.variables.at(i).toObject();

            do_indent(out, indent+2)
                << "name=\"" << varEntry["name"].toString() << "\""
                << ", value=\"" << varEntry["value"].toString() << "\""
                << ", comment=\"" << varEntry["comment"].toString() << "\"" << endl;
        }
    }

    return out;
}

QTextStream &operator<<(QTextStream &out, const MVMETemplates &templates)
{
    // Module table
    {
        QVector<VMEModuleMeta> modules(templates.moduleMetas);
        std::sort(modules.begin(), modules.end(), [](const VMEModuleMeta &a, const VMEModuleMeta &b) {
            return a.typeId < b.typeId;
        });

        out << ">>>>> Known Modules <<<<<" << endl;
        const int fw = 25;
        left(out) << qSetFieldWidth(fw)
            << "typeId" << "typeName" << "displayName"
            << qSetFieldWidth(0) << endl;

        for (const auto &mm: modules)
        {
            out << qSetFieldWidth(fw)
                << static_cast<u32>(mm.typeId)
                << mm.typeName
                << mm.displayName
                << qSetFieldWidth(0)
                << endl;
        }

        out << "<<<<< Known Modules >>>>>" << endl;
        out.reset();
    }

    out << endl;

    // Detailed information about loaded templates
    {
        out << ">>>>> VME Templates <<<<<" << endl;
        out << "Event:" << endl;

        do_indent(out, 2) << "daqStart" << endl;
        print(out, templates.eventTemplates.daqStart, 4);

        do_indent(out, 2) << "daqStop" << endl;
        print(out, templates.eventTemplates.daqStop, 4);

        do_indent(out, 2) << "readoutCycleStart" << endl;
        print(out, templates.eventTemplates.readoutCycleStart, 4);

        do_indent(out, 2) << "readoutCycleEnd" << endl;
        print(out, templates.eventTemplates.readoutCycleEnd, 4);

        out << endl << "Modules:" << endl;

        for (const auto &module: templates.moduleMetas)
        {
            do_indent(out, 2) << "Begin Module" << endl;
            print(out, module, 4);
            do_indent(out, 2) << "End Module" << endl << endl;
        }
        out << "<<<<< VME Templates >>>>>" << endl;
    }

    return out;
}

VMEModuleMeta get_module_meta_by_typename(const MVMETemplates &templates,
                                          const QString &moduleTypeName)
{
    for (auto mm: templates.moduleMetas)
    {
        if (mm.typeName == moduleTypeName)
            return mm;
    }
    return {};
}

VMEModuleMeta get_module_meta_by_typeId(const MVMETemplates &templates, u8 typeId)
{
    for (auto mm: templates.moduleMetas)
    {
        if (mm.typeId == typeId)
            return mm;
    }
    return {};
}

QVector<AuxiliaryVMEScriptInfo> LIBMVME_EXPORT read_auxiliary_scripts(
    TemplateLogger logger)
{
    QString templatePath = get_template_path() + QSL("/auxiliary_scripts");
    do_log(QString("Reading auxiliary scripts from %1").arg(templatePath), logger);
    return read_auxiliary_scripts(templatePath, logger);
}

QVector<AuxiliaryVMEScriptInfo> LIBMVME_EXPORT read_auxiliary_scripts(
    const QString &path, TemplateLogger logger)
{
    QVector<AuxiliaryVMEScriptInfo> result;

    QDir auxDir(path);

    auto jsonFiles = auxDir.entryList({ QSL("*.json") }, QDir::Files | QDir::Readable);

    for (auto jsonFile: jsonFiles)
    {
        auto json = read_json_file(auxDir.filePath(jsonFile), logger).object();

        if (!json.contains("auxiliary_vme_scripts")
            || !json["auxiliary_vme_scripts"].isArray())
            continue;

        auto scriptInfoArray = json["auxiliary_vme_scripts"].toArray();

        for (const auto &scriptInfoRef: scriptInfoArray)
        {
            auto scriptInfo = scriptInfoRef.toObject();

            if (!scriptInfo.contains("fileName"))
                continue;

            QString scriptContents = read_file(auxDir.filePath(
                    scriptInfo["fileName"].toString()), logger);

            if (scriptContents.isEmpty())
                continue;

            AuxiliaryVMEScriptInfo auxInfo = {};
            auxInfo.info=scriptInfo;
            auxInfo.contents=scriptContents;
            auxInfo.auxInfoFileName=jsonFile;

            result.push_back(auxInfo);
        }
    }

    std::sort(std::begin(result), std::end(result), auxinfo_default_compare);

    return result;
}

bool auxinfo_default_compare(
    const AuxiliaryVMEScriptInfo &a, const AuxiliaryVMEScriptInfo &b)
{
    if (a.vendorName() == b.vendorName())
    {
        if (a.moduleName() == b.moduleName())
            return a.scriptName() < b.scriptName();

        return a.moduleName() < b.moduleName();
    }

    if (a.vendorName() == QSL("mesytec"))
        return true;

    if (b.vendorName() == QSL("mesytec"))
        return false;

    return a.vendorName() < b.vendorName();
}

QVector<GenericVMEScriptInfo> read_vme_scripts_from_directory(
    const QString &path)
{
    QVector<GenericVMEScriptInfo> result;

    QDir sourceDir(path);

    auto filenames = sourceDir.entryList({ QSL("*.vmescript") }, QDir::Files | QDir::Readable);

    for (auto filename: filenames)
    {
        auto fullPath = sourceDir.filePath(filename);

        result.push_back({ read_file(fullPath, {}), QFileInfo(fullPath) });
    }

    return result;
}

QVector<GenericVMEScriptInfo> read_mvlc_trigger_io_scripts()
{
    QString templatePath = get_template_path() + QSL("/mvlc_trigger_io");
    return read_vme_scripts_from_directory(templatePath);
}

GenericVMEScriptInfo read_default_mvlc_trigger_io_script()
{
    for (const auto &scriptInfo: read_mvlc_trigger_io_scripts())
    {
        if (scriptInfo.fileInfo.baseName() == QSL("mvlc_trigger_io-Default_Trigger_IO"))
            return scriptInfo;
    }

    return {};
}


} // namespace vats
