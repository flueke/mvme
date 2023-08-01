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
/* TODO
 * - fail on empty name or when failing to generate an identifier
 * - add version info, export date, etc to the template variables
 * - add info about the condition input to the templates
 *
 */
#include "exportsink_codegen.h"
#include "util/variablify.h"

#include <Mustache/mustache.hpp>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <functional>

#include "analysis.h"
#include "git_sha1.h"

namespace mu = kainjow::mustache;

namespace analysis
{

namespace TemplateRenderFlags
{
    using Flag = u8;
    static const Flag IfNotExists   = 1u << 0;
    static const Flag SetExecutable = 1u << 1;
}

static QString render_to_string(
    const QString &templateFilename,
    mu::data &templateData)
{
    QFile templateFile(templateFilename);

    if (!templateFile.open(QIODevice::ReadOnly))
    {
        auto msg = QSL("Could not open input template file %1: %2")
            .arg(templateFilename).arg(templateFile.errorString());

        qDebug() << __PRETTY_FUNCTION__ << msg;
        // Should not happen because the template files are stored in the
        // compiled in qt resources.
        assert(false);

        throw std::runtime_error(msg.toStdString());
    }

    mu::mustache tmpl(templateFile.readAll().toStdString());

    return QString::fromStdString(tmpl.render(templateData));
}

static void render_to_file(
    const QString &templateFilename,
    mu::data &templateData,
    const QString &outputFilename,
    TemplateRenderFlags::Flag flags,
    ExportSinkCodeGenerator::Logger logger)
{
    if ((flags & TemplateRenderFlags::IfNotExists)
        && QFileInfo(outputFilename).exists())
    {
        return;
    }

    auto rendered = render_to_string(templateFilename, templateData);

    if (logger)
    {
        logger(QSL("Generating file %1").arg(outputFilename));
    }

    QFile outFile(outputFilename);

    if (!outFile.open(QIODevice::WriteOnly))
    {
        auto msg = QSL("Could not open output file %1: %2")
            .arg(outputFilename).arg(outFile.errorString());

        throw std::runtime_error(msg.toStdString());
    }

    outFile.write(rendered.toLocal8Bit());

    if (flags & TemplateRenderFlags::SetExecutable)
    {
        auto perms = outFile.permissions();
        perms |= (QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeOther);

        if (!outFile.setPermissions(perms))
        {
            auto msg = QSL("Could not set execute permissions of output file %1: %2")
                .arg(outputFilename).arg(outFile.errorString());

            throw std::runtime_error(msg.toStdString());
        }
    }
}

struct VariableNames
{
    QString structName;
    QVector<QString> arrayNames;
};

using RenderFunction = std::function<void (const QString &templateFilename,
                                           mu::data &templateData,
                                           const QString &outputFilename,
                                           TemplateRenderFlags::Flag flags,
                                           ExportSinkCodeGenerator::Logger
                                           )>;

struct ExportSinkCodeGenerator::Private
{
    ExportSink *sink;

    VariableNames generateVariableNames();
    mu::data makeGlobalTemplateData();
    void generate(RenderFunction render, Logger logger);
};

bool is_valid_identifier(const QString &str)
{
    QRegularExpression re("^[a-zA-Z_][a-zA-Z0-9_]*$");

    return re.match(str).hasMatch();
}

VariableNames ExportSinkCodeGenerator::Private::generateVariableNames()
{
    VariableNames result;
    QSet<QString> setOfNames;

    result.structName = variablify(sink->objectName());
    assert(is_valid_identifier(result.structName));

    for (auto slot: sink->getDataInputs())
    {
        assert(slot && slot->inputPipe && slot->inputPipe->getSource());

        auto inputSource = slot->inputPipe->getSource();
        QString arrayNameBase = variablify(inputSource->objectName());

        // If the pipe has (the possibility to have) multiple outputs, append the output name
        if (inputSource->hasVariableNumberOfOutputs() || inputSource->getNumberOfOutputs() > 1)
        {
            arrayNameBase += "_" + variablify(inputSource->getOutputName(
                    slot->inputPipe->sourceOutputIndex));
        }

        auto arrayName     = arrayNameBase;
        int suffix         = 1;

        while (setOfNames.contains(arrayName) || arrayName == result.structName)
        {
            arrayName = arrayNameBase + "_" + QString::number(suffix++);
        }

        assert(is_valid_identifier(arrayName));
        setOfNames.insert(arrayName);
        result.arrayNames.push_back(arrayName);
    }

    qDebug() << "structName =" << result.structName;

    for (auto arrayName: result.arrayNames)
    {
        qDebug() << "  arrayName =" << arrayName;
    }

    return result;
}

mu::data ExportSinkCodeGenerator::Private::makeGlobalTemplateData()
{
    /* Build a mustache data structure looking like this:
     * array_info_list =
     * [
     *     // One entry for each exported array
     *     {
     *       dimension, index, variable_name, analysis_name, unit,
     *       limits = [ { lower_limit, upper_limit } ],
     *     },
     * ]
     */

    auto varNames         = generateVariableNames();
    const auto dataInputs = sink->getDataInputs();

    assert(varNames.arrayNames.size() == dataInputs.size());

    mu::data array_info_list = mu::data::type::list;

    size_t arrayIndex = 0;

    for (auto slot: dataInputs)
    {
        mu::data limits_list = mu::data::type::list;

        for (s32 pi = 0; pi < slot->inputPipe->getSize(); pi++)
        {
            auto param = slot->inputPipe->getParameter(pi);
            mu::data limits_object = mu::data::type::object;

            limits_object["lower_limit"] = QString::number(param->lowerLimit).toStdString();
            limits_object["upper_limit"] = QString::number(param->upperLimit).toStdString();

            limits_list.push_back(limits_object);
        }

        auto pipe           = slot->inputPipe;
        auto variable_name  = varNames.arrayNames[arrayIndex];

        mu::data array_info = mu::data::type::object;

        array_info["index"]         = QString::number(arrayIndex).toStdString();
        array_info["dimension"]     = QString::number(pipe->getSize()).toStdString();
        array_info["variable_name"] = variable_name.toStdString();
        array_info["analysis_name"] = pipe->getSource()->objectName().toStdString();
        array_info["unit"]          = pipe->getParameters().unit.toStdString();
        array_info["limits"]        = mu::data{limits_list};

        array_info_list.push_back(array_info);

        arrayIndex++;
    }

    auto struct_name = varNames.structName;

    mu::data result = mu::data::type::object;

    result["struct_name"]           = struct_name.toStdString();
    result["array_count"]           = QString::number(dataInputs.size()).toStdString();
    result["array_info"]            = mu::data{array_info_list};
    result["mvme_version"]          = GIT_VERSION_SHORT;
    result["export_date"]           = QDateTime::currentDateTime().toString().toStdString();
    result["sparse?"]               = sink->getFormat() == ExportSink::Format::Sparse;
    result["full?"]                 = sink->getFormat() == ExportSink::Format::Full;

    return result;
}

void ExportSinkCodeGenerator::Private::generate(RenderFunction render,
                                                ExportSinkCodeGenerator::Logger logger)
{
    QString fmtString;

    switch (sink->getFormat())
    {
        case ExportSink::Format::Full:
            fmtString = QSL("full");
            break;

        case ExportSink::Format::Sparse:
            fmtString = QSL("sparse");
            break;

        case ExportSink::Format::CSV:
            assert(false);
            break;
    }

    const QString headerFilePath = sink->getOutputPrefixPath() + "/" + sink->getExportFileBasename() + ".h";
    const QString implFilePath   = sink->getOutputPrefixPath() + "/" + sink->getExportFileBasename() + ".cpp";
    const QString pyFilePath     = sink->getOutputPrefixPath() + "/" + sink->getExportFileBasename() + ".py";
    const QDir exportDir         = sink->getOutputPrefixPath();

    // generate c++ struct, utility programs and a CMakeLists.txt
    {
        mu::data data = makeGlobalTemplateData();

        data["header_guard"]       = variablify(sink->objectName().toUpper()).toStdString();
        data["export_header_file"] = QFileInfo(headerFilePath).fileName().toStdString();
        data["export_impl_file"]   = QFileInfo(implFilePath).fileName().toStdString();

        render(QSL(":/analysis/export_templates/cpp_%1_header.h.mustache").arg(fmtString),
                       data, headerFilePath, 0, logger);

        render(QSL(":/analysis/export_templates/cpp_%1_impl.cpp.mustache").arg(fmtString),
                       data, implFilePath, 0, logger);

        render(QSL(":/analysis/export_templates/cpp_%1_export_info.cpp.mustache").arg(fmtString),
                       data, exportDir.filePath("export_info.cpp"), 0, logger);

        render(QSL(":/analysis/export_templates/cpp_%1_export_dump.cpp.mustache").arg(fmtString),
                       data, exportDir.filePath("export_dump.cpp"), 0, logger);

        render(QSL(":/analysis/export_templates/CMakeLists.txt.mustache"),
                       data, exportDir.filePath("CMakeLists.txt"), 0, logger);

        render(QSL(":/analysis/export_templates/cpp_root_generate_histos.cpp.mustache"),
                       data, exportDir.filePath("root_generate_histos.cpp"), 0, logger);

        render(QSL(":/analysis/export_templates/cpp_%1_root_generate_tree.cpp.mustache").arg(fmtString),
                       data, exportDir.filePath("root_generate_tree.cpp"), 0, logger);

        // copy c++ libs
        if (sink->getCompressionLevel() != 0)
        {
            mu::data data = mu::data::type::object;

            render(QSL(":/external/zstr/src/zstr.hpp"),
                           data, exportDir.filePath("zstr.hpp"), 0, logger);

            render(QSL(":/external/zstr/src/strict_fstream.hpp"),
                           data, exportDir.filePath("strict_fstream.hpp"), 0, logger);
        }
    }

    // python
    {
        mu::data data = makeGlobalTemplateData();

        data["event_import"] = QFileInfo(pyFilePath).baseName().toStdString();

        render(QSL(":/analysis/export_templates/python_%1_event.py.mustache").arg(fmtString),
                       data, pyFilePath, 0, logger);

        render(QSL(":/analysis/export_templates/python_%1_export_dump.py.mustache").arg(fmtString),
               data, exportDir.filePath("export_dump.py"), TemplateRenderFlags::SetExecutable,
               logger);

        render(QSL(":/analysis/export_templates/pyroot_generate_histos.py.mustache"),
               data, exportDir.filePath("pyroot_generate_histos.py"), TemplateRenderFlags::SetExecutable,
               logger);
    }
}

ExportSinkCodeGenerator::ExportSinkCodeGenerator(ExportSink *sink)
    : m_d(std::make_unique<ExportSinkCodeGenerator::Private>())
{
    m_d->sink = sink;
}

ExportSinkCodeGenerator::~ExportSinkCodeGenerator()
{
}

void ExportSinkCodeGenerator::generateFiles(Logger logger)
{
    m_d->generate(render_to_file, logger);
}

QMap<QString, QString> ExportSinkCodeGenerator::generateMap() const
{
    QMap<QString, QString> result;

    auto renderer = [&result] (const QString &templateFilename,
                               mu::data &templateData,
                               const QString &outputFilename,
                               TemplateRenderFlags::Flag,
                               Logger)
    {
        result[outputFilename] = render_to_string(templateFilename, templateData);
    };

    m_d->generate(renderer, Logger());

    return result;
}

QStringList ExportSinkCodeGenerator::getOutputFilenames() const
{
    return generateMap().keys();
}

}
