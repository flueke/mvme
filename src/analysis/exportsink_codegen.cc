#include "exportsink_codegen.h"

#include <Mustache/mustache.hpp>
#include <QFileInfo>
#include <QDir>
#include "analysis.h"

namespace mu = kainjow::mustache;

namespace analysis
{

struct ExportSinkCodeGenerator::Private
{
    ExportSink *sink;

    void generateFull();
    void generateSparse();
};

// Highly sophisticated variable name generation ;-)
QString variablify(QString str)
{
    str.replace(".", "_");
    str.replace("/", "_");
    return str;
}

void render_to_file(
    const QString &templateFilename,
    mu::data &templateData,
    const QString &outputFilename)
{
        QFile templateFile(templateFilename);

        if (!templateFile.open(QIODevice::ReadOnly))
            throw std::runtime_error("Could not open input template file.");

        mu::mustache tmpl(templateFile.readAll().toStdString());

        auto rendered = QString::fromStdString(tmpl.render(templateData));

        QFile outFile(outputFilename);

        if (!outFile.open(QIODevice::WriteOnly))
            throw std::runtime_error("Could not open output file.");
        outFile.write(rendered.toLocal8Bit());
}

void ExportSinkCodeGenerator::Private::generateFull()
{
    const auto dataInputs        = sink->getDataInputs();
    const QString headerFilename = sink->getOutputBasePath() + ".h";
    const QString implFilename   = sink->getOutputBasePath() + ".cpp";

    // write the c++ header file
    {
        mu::data data = mu::data::type::object;

        data.set("header_guard",    variablify(sink->objectName().toUpper()).toStdString());
        data.set("struct_name",     variablify(sink->objectName()).toStdString());
        data.set("array_count",     QString("%1u").arg(dataInputs.size()).toStdString());

        mu::data arrayMembers = mu::data::type::list;
        mu::data arrayLimits  = mu::data::type::list;

        for (auto slot: dataInputs)
        {
            auto pipe      = slot->inputPipe;
            auto arrayName = variablify(pipe->getSource()->objectName()).toStdString();
            auto arraySize = QString::number(pipe->getSize()).toStdString();

            mu::data arrayData = mu::data::type::object;
            arrayData.set("array_name", arrayName);
            arrayData.set("array_size", arraySize);
            arrayMembers.push_back(arrayData);

            mu::data limitsData = mu::data::type::object;
            limitsData.set("array_name", arrayName);
            limitsData.set("array_size", arraySize);
            arrayLimits.push_back(limitsData);
        }

        data.set("array_members", arrayMembers);
        data.set("array_limits", arrayLimits);

        render_to_file(":/resources/export_sink_full_cpp_header.mustache", data, headerFilename);
    }

    // write the c++ implementation file
    {
        mu::data data = mu::data::type::object;
        data.set("header_file", QFileInfo(headerFilename).fileName().toStdString());
        data.set("struct_name", variablify(sink->objectName()).toStdString());

        mu::data arrayLimits     = mu::data::type::list;
        mu::data arrayUnitLabels = mu::data::type::list;
        mu::data arrayInfo       = mu::data::type::list;

        size_t arrayIndex = 0;

        for (auto slot: dataInputs)
        {
            auto pipe      = slot->inputPipe;
            auto arrayName = variablify(pipe->getSource()->objectName()).toStdString();
            auto arraySize = QString::number(pipe->getSize()).toStdString();

            mu::data arrayLimitsData = mu::data::type::object;
            arrayLimitsData.set("array_name", variablify(pipe->getSource()->objectName()).toStdString());
            arrayLimitsData.set("array_size", QString::number(pipe->getSize()).toStdString());

            mu::data arrayInfoData = mu::data::type::object;
            arrayInfoData.set("index",      QString::number(arrayIndex).toStdString());
            arrayInfoData.set("size",       QString::number(pipe->getSize()).toStdString());
            arrayInfoData.set("var_name",   variablify(pipe->getSource()->objectName()).toStdString());
            arrayInfoData.set("name",       pipe->getSource()->objectName().toStdString());

            mu::data limitsList = mu::data::type::list;

            for (s32 pi = 0; pi < slot->inputPipe->getSize(); pi++)
            {
                auto param = slot->inputPipe->getParameter(pi);
                mu::data limitsData = mu::data::type::object;
                limitsData.set("lower_limit", QString::number(param->lowerLimit).toStdString());
                limitsData.set("upper_limit", QString::number(param->upperLimit).toStdString());
                limitsList.push_back(limitsData);
            }

            arrayLimitsData.set("limits", limitsList);
            arrayLimits.push_back(arrayLimitsData);

            arrayInfo.push_back(arrayInfoData);

            mu::data labelData = mu::data::type::object;
            labelData.set("unit_label", pipe->getParameters().unit.toStdString());
            arrayUnitLabels.push_back(labelData);

            arrayIndex++;
        }

        data.set("array_limits", arrayLimits);
        data.set("unit_labels",  arrayUnitLabels);
        data.set("array_info",   arrayInfo);

        render_to_file(":/resources/export_sink_full_cpp_impl.mustache", data, implFilename);
    }

    // write the c++ programs and a Makefile
    {
        mu::data data = mu::data::type::object;
        data.set("header_file", QFileInfo(headerFilename).fileName().toStdString());
        data.set("impl_file",   QFileInfo(implFilename).fileName().toStdString());
        data.set("struct_name", variablify(sink->objectName()).toStdString());
        // FIXME: add getter for this info to the sink (.gz is not handled here!)
        data.set("export_file", QFileInfo(sink->getOutputBasePath() + ".bin").fileName().toStdString());
        // FIXME: allow getting the directory of where the export files go from the sink instead of fiddling with the basepath here!
        QDir exportDir = QFileInfo(sink->getOutputBasePath()).dir();

        render_to_file(":/resources/export_sink_full_cpp_export_info.cpp.mustache", data, exportDir.filePath("export_info.cpp"));
        render_to_file(":/resources/export_sink_full_cpp_export_dump.cpp.mustache", data, exportDir.filePath("export_dump.cpp"));
        render_to_file(":/resources/export_sink_full_cpp_Makefile.mustache",        data, exportDir.filePath("Makefile"));
    }
}

void ExportSinkCodeGenerator::Private::generateSparse()
{
    assert(!"implement me!");
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
    switch (m_d->sink->getFormat())
    {
        case ExportSink::Format::Full:
            m_d->generateFull();
            break;

         case ExportSink::Format::Sparse:
            m_d->generateSparse();
            break;
    }
}

}
