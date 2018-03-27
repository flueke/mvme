/* TODO
 * - collect variable names
 * - generate valid and unique identifiers, prefill with something like "array_<idx>".
 * - fail on empty name or when failing to generate an identifier
 * - add version info, export date, etc to the template variables
 * - add info about the condition input to the templates
 *
 */
#include "exportsink_codegen.h"

#include <Mustache/mustache.hpp>
#include <QFileInfo>
#include <QDir>

#include "analysis.h"
#include "git_sha1.h"

namespace mu = kainjow::mustache;

namespace analysis
{

// Highly sophisticated variable name generation ;-)
QString variablify(QString str)
{
    str.replace(".", "_");
    str.replace("/", "_");
    return str;
}

static void render_to_file(
    const QString &templateFilename,
    mu::data &templateData,
    const QString &outputFilename)
{
        QFile templateFile(templateFilename);

        if (!templateFile.open(QIODevice::ReadOnly))
        {
            auto msg = QSL("Could not open input template file %1: %2")
                .arg(templateFilename).arg(templateFile.errorString());
            throw std::runtime_error(msg.toStdString());
        }

        mu::mustache tmpl(templateFile.readAll().toStdString());

        auto rendered = QString::fromStdString(tmpl.render(templateData));

        QFile outFile(outputFilename);

        if (!outFile.open(QIODevice::WriteOnly))
            throw std::runtime_error("Could not open output file.");

        outFile.write(rendered.toLocal8Bit());
}

struct ExportSinkCodeGenerator::Private
{
    ExportSink *sink;
    RunInfo runInfo;

    mu::data makeGlobalTemplateData();
    void generate();
};

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

    const auto dataInputs = sink->getDataInputs();

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
        auto variable_name  = variablify(pipe->getSource()->objectName()).toStdString();

        mu::data array_info = mu::data::type::object;

        array_info["index"]         = QString::number(arrayIndex).toStdString();
        array_info["dimension"]     = QString::number(pipe->getSize()).toStdString();
        array_info["variable_name"] = variable_name;
        array_info["analysis_name"] = pipe->getSource()->objectName().toStdString();
        array_info["unit"]          = pipe->getParameters().unit.toStdString();
        array_info["limits"]        = mu::data{limits_list};

        array_info_list.push_back(array_info);

        arrayIndex++;
    }

    auto struct_name = variablify(sink->objectName()).toStdString();

    mu::data result = mu::data::type::object;

    result["struct_name"]           = struct_name;
    result["array_count"]           = QString::number(dataInputs.size()).toStdString();
    result["array_info"]            = mu::data{array_info_list};
    result["mvme_version"]          = GIT_VERSION;
    result["export_date"]           = QDateTime::currentDateTime().toString().toStdString();
    result["run_id"]                = runInfo.runId.toStdString();
    result["export_data_filepath"]  = sink->getDataFilePath().toStdString();
    result["export_data_filename"]  = sink->getDataFileName().toStdString();
    result["export_data_basename"]  = sink->getExportFileBasename().toStdString();
    result["export_data_extension"] = sink->getDataFileExtension().toStdString();

    return result;
}

void ExportSinkCodeGenerator::Private::generate()
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
    }

    const auto dataInputs        = sink->getDataInputs();
    const QString headerFilePath = sink->getOutputBasePath() + ".h";
    const QString implFilePath   = sink->getOutputBasePath() + ".cpp";
    const QString pyFilePath     = sink->getOutputBasePath() + ".py";

    // write the c++ export header file
    {
        mu::data data = makeGlobalTemplateData();

        data["header_guard"] = variablify(sink->objectName().toUpper()).toStdString();

        render_to_file(QSL(":/analysis/export_templates/%1_header.h.mustache").arg(fmtString),
                       data, headerFilePath);
    }

    // write the c++ export implementation file
    {
        mu::data data = makeGlobalTemplateData();

        data["export_header_file"] = QFileInfo(headerFilePath).fileName().toStdString();

        render_to_file(QSL(":/analysis/export_templates/%1_impl.cpp.mustache").arg(fmtString),
                       data, implFilePath);
    }

    // write utility/demo programs and a CMakeLists.txt
    {
        mu::data data = makeGlobalTemplateData();

        data["export_header_file"]   = QFileInfo(headerFilePath).fileName().toStdString();
        data["export_impl_file"]     = QFileInfo(implFilePath).fileName().toStdString();

        QDir exportDir = sink->getExportDirectory();

        render_to_file(QSL(":/analysis/export_templates/%1_export_info.cpp.mustache").arg(fmtString),
                       data, exportDir.filePath("export_info.cpp"));

        render_to_file(QSL(":/analysis/export_templates/%1_export_dump.cpp.mustache").arg(fmtString),
                       data, exportDir.filePath("export_dump.cpp"));

        render_to_file(":/analysis/export_templates/CMakeLists.txt.mustache",
                       data, exportDir.filePath("CMakeLists.txt"));

        render_to_file(QSL(":/analysis/export_templates/generate_root_histos.cpp.mustache"),
                       data, exportDir.filePath("export_generate_root_histos.cpp"));

        /* Copy c++ libs. */
        if (sink->getCompressionLevel() != 0)
        {
            render_to_file(QSL(":/3rdparty/zstr/src/zstr.hpp"),
                           data, exportDir.filePath("zstr.hpp"));

            render_to_file(QSL(":/3rdparty/zstr/src/strict_fstream.hpp"),
                           data, exportDir.filePath("strict_fstream.hpp"));
        }

        render_to_file(QSL(":/analysis/export_templates/%1_event.py.mustache").arg(fmtString),
                       data, pyFilePath);
    }
}

ExportSinkCodeGenerator::ExportSinkCodeGenerator(ExportSink *sink, const RunInfo &runInfo)
    : m_d(std::make_unique<ExportSinkCodeGenerator::Private>())
{
    m_d->sink    = sink;
    m_d->runInfo = runInfo;
}

ExportSinkCodeGenerator::~ExportSinkCodeGenerator()
{
}

void ExportSinkCodeGenerator::generate()
{
    m_d->generate();
}

}
