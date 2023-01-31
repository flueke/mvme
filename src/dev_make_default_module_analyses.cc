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
            << "The resulting files are placed in a directory tree with the same" << endl
            << "structure as the mvme templates. The directories and files are created" << endl
            << "inside the given output directory." << endl;
        cout << endl;
        cout << "Usage: " << argv[0] << " <outputDirectory>" << endl;
        return 1;
    }

    auto outBaseDirName = app.arguments().at(1);

    if (QDir(outBaseDirName).exists())
    {
        cerr << "Error: output directory "
            << outBaseDirName.toStdString() << " exists" << endl;
        return 1;
    }

    if (!QDir().mkdir(outBaseDirName))
    {
        cerr << "Could not create output directory " << outBaseDirName.toStdString() << endl;
        return 1;
    }

    if (!QDir::setCurrent(outBaseDirName))
    {
        cerr << "Could not change working directory to " << outBaseDirName.toStdString() << endl;
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

        auto path = QFileInfo(mm.templatePath).fileName() + "/analysis";

        if (!QDir().mkpath(path))
        {
            cerr << "Error creating module path " << path.toStdString() << endl;
            return 1;
        }

        QFile outFile(path + "/default_filters.analysis");
        if (!outFile.open(QIODevice::WriteOnly))
        {
            cerr << "Error opening output file " << outFile.fileName().toStdString()
                << " for writing: " << outFile.errorString().toStdString() << endl;
            return 1;
        }

        auto doc = analysis::serialize_analysis_to_json_document(*analysis);

        if (outFile.write(doc.toJson()) < 0)
        {
            cerr << "Error writing analysis JSON to " << outFile.fileName().toStdString()
                << ": " << outFile.errorString().toStdString() << endl;
            return 1;
        }
    }

    return 0;
}
