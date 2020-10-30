#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <iostream>

#include <template_system.h>
#include <vme_config.h>
#include <analysis/analysis.h>
#include <analysis/analysis_util.h>

using std::cerr;
using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (app.arguments().size() != 2
        || app.arguments().contains("-h")
        || app.arguments().contains("--help"))
    {
        cout << "This tool creates and saves a default analysis for each of the" << endl
            << "non-user VME module templates shipped with mvme." << endl
            << "The resulting analysis files are placed in the given output directory." << endl;
        cout << endl;
        cout << "Usage: " << argv[0] << " <outputDirectory>" << endl;
        return 1;
    }

    auto outputDirName = app.arguments().at(1);

    if (QDir(outputDirName).exists())
    {
        cerr << "Error: output directory "
            << outputDirName.toStdString() << " exists" << endl;
        return 1;
    }

    if (!QDir().mkdir(outputDirName))
    {
        cerr << "Could not create output directory " << outputDirName.toStdString() << endl;
        return 1;
    }

    if (!QDir::setCurrent(outputDirName))
    {
        cerr << "Could not change working directory to " << outputDirName.toStdString() << endl;
        return 1;
    }

    auto mvmeTemplates = vats::read_templates();

    for (const auto &mm: mvmeTemplates.moduleMetas)
    {
        if (mm.typeName.startsWith("user"))
            continue;

        auto module = std::make_unique<ModuleConfig>();
        module->setObjectName(mm.typeName);
        module->setModuleMeta(mm);

        auto analysis = std::make_unique<analysis::Analysis>();
        analysis::add_default_filters(analysis.get(), module.get());

        // FIXME: factor this out into analysis_util
        QJsonObject analysisJson;
        {
            QJsonObject dest;
            analysis->write(dest);
            analysisJson["AnalysisNG"] = dest;
        }

        QFile outFile(module->objectName() + ".analysis");
        if (!outFile.open(QIODevice::WriteOnly))
        {
            cerr << "Error opening output file " << outFile.fileName().toStdString()
                << " for writing: " << outFile.errorString().toStdString() << endl;
            return 1;
        }

        auto doc = analysis::analysis_to_json_doc(*analysis);

        if (outFile.write(doc.toJson()) < 0)
        {
            cerr << "Error writing analysis JSON to " << outFile.fileName().toStdString()
                << ": " << outFile.errorString().toStdString() << endl;
            return 1;
        }
    }

    return 0;
}
