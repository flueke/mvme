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
#include <QRegularExpression>

#include "analysis.h"
#include "git_sha1.h"

namespace mu = kainjow::mustache;

namespace analysis
{

namespace TemplateRenderFlags
{
    using Flag = u8;
    static const Flag IfNotExists = 1u << 0;
}

static void render_to_file(
    const QString &templateFilename,
    mu::data &templateData,
    const QString &outputFilename,
    TemplateRenderFlags::Flag flags = 0)
{
    if ((flags & TemplateRenderFlags::IfNotExists)
        && QFileInfo(outputFilename).exists())
    {
        return;
    }

    QFile templateFile(templateFilename);

    if (!templateFile.open(QIODevice::ReadOnly))
    {
        auto msg = QSL("Could not open input template file %1: %2")
            .arg(templateFilename).arg(templateFile.errorString());

        qDebug() << msg;
        assert(false);

        throw std::runtime_error(msg.toStdString());
    }

    mu::mustache tmpl(templateFile.readAll().toStdString());

    auto rendered = QString::fromStdString(tmpl.render(templateData));

    QFile outFile(outputFilename);

    if (!outFile.open(QIODevice::WriteOnly))
    {
        auto msg = QSL("Could not open output file %1: %2")
            .arg(outputFilename).arg(outFile.errorString());

        throw std::runtime_error(msg.toStdString());
    }

    outFile.write(rendered.toLocal8Bit());
}

struct VariableNames
{
    QString structName;
    QVector<QString> arrayNames;
};

struct ExportSinkCodeGenerator::Private
{
    ExportSink *sink;

    VariableNames generateVariableNames();
    mu::data makeGlobalTemplateData();
    void generate();
};

// Highly sophisticated variable name generation ;-)
static QString variablify(QString str)
{
    QRegularExpression ReIsValidFirstChar("[a-zA-Z_]");
    QRegularExpression ReIsValidChar("[a-zA-Z0-9_]");

    for (int i = 0; i < str.size(); i++)
    {
        QRegularExpressionMatch match;

        if (i == 0)
        {
            match = ReIsValidFirstChar.match(str, i, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption);
        }
        else
        {
            match = ReIsValidChar.match(str, i, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption);
        }

        if (!match.hasMatch())
        {
            //qDebug() << "re did not match on" << str.mid(i);
            //qDebug() << "replacing " << str[i] << " with _ in " << str;
            str[i] = '_';
        }
        else
        {
            //qDebug() << "re matched: " << match.captured(0);
        }
    }

    return str;
}

static bool is_valid_identifier(const QString &str)
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
        auto arrayNameBase = variablify(slot->inputPipe->getSource()->objectName());
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
    result["mvme_version"]          = GIT_VERSION;
    result["export_date"]           = QDateTime::currentDateTime().toString().toStdString();
    result["sparse?"]               = sink->getFormat() == ExportSink::Format::Sparse;
    result["full?"]                 = sink->getFormat() == ExportSink::Format::Full;

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

        render_to_file(QSL(":/analysis/export_templates/cpp_%1_header.h.mustache").arg(fmtString),
                       data, headerFilePath);

        render_to_file(QSL(":/analysis/export_templates/cpp_%1_impl.cpp.mustache").arg(fmtString),
                       data, implFilePath);

        render_to_file(QSL(":/analysis/export_templates/cpp_%1_export_info.cpp.mustache").arg(fmtString),
                       data, exportDir.filePath("export_info.cpp"));

        render_to_file(QSL(":/analysis/export_templates/cpp_%1_export_dump.cpp.mustache").arg(fmtString),
                       data, exportDir.filePath("export_dump.cpp"));

        render_to_file(":/analysis/export_templates/CMakeLists.txt.mustache",
                       data, exportDir.filePath("CMakeLists.txt"));

        render_to_file(QSL(":/analysis/export_templates/cpp_generate_root_histos.cpp.mustache"),
                       data, exportDir.filePath("export_generate_root_histos.cpp"));

        // copy c++ libs
        if (sink->getCompressionLevel() != 0)
        {
            mu::data data = mu::data::type::object;

            render_to_file(QSL(":/3rdparty/zstr/src/zstr.hpp"),
                           data, exportDir.filePath("zstr.hpp"));

            render_to_file(QSL(":/3rdparty/zstr/src/strict_fstream.hpp"),
                           data, exportDir.filePath("strict_fstream.hpp"));
        }
    }

    // python
    {
        mu::data data = makeGlobalTemplateData();

        data["event_import"] = QFileInfo(pyFilePath).baseName().toStdString();

        render_to_file(QSL(":/analysis/export_templates/python_%1_event.py.mustache").arg(fmtString),
                       data, pyFilePath);

        render_to_file(QSL(":/analysis/export_templates/python_%1_export_dump.py.mustache").arg(fmtString),
                       data, exportDir.filePath("export_dump.py"));
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

void ExportSinkCodeGenerator::generate()
{
    m_d->generate();
}

}
