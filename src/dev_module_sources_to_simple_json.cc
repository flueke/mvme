#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <iostream>
#include <spdlog/spdlog.h>
#include "analysis/analysis.h"
#include "analysis/analysis_serialization.h"
#include "git_sha1.h"
#include "template_system.h"

// Writes out vme module default analysis filter strings and meta information in json format.

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::warn);
    QCoreApplication app(argc, argv);

    auto logger = [](const QString &msg) { qDebug() << msg; };

    auto templates = vats::read_templates(logger);
    auto objectFactory = analysis::Analysis().getObjectFactory();

    QJsonArray modules_out;

    for (const auto &mm: templates.moduleMetas)
    {
        QDir moduleDir(vats::get_module_path(mm.typeName));
        QFile filtersFile(moduleDir.filePath("analysis/default_filters.analysis"));

        if (!filtersFile.open(QIODevice::ReadOnly))
        {
            spdlog::info("Could not open default filters file for module {}, skipping", mm.typeName.toStdString());
            continue;
        }

        auto doc = QJsonDocument::fromJson(filtersFile.readAll());
        auto json = doc.object()["AnalysisNG"].toObject();
        json = analysis::convert_to_current_version(json, nullptr);
        auto objectStore = analysis::deserialize_objects(json, objectFactory);

        spdlog::info("module={}", mm.typeName.toStdString());

        QJsonArray sources_out;

        for (const auto &src: objectStore.sources)
        {
            if (auto ex = qobject_cast<analysis::Extractor *>(src.get()))
            {
                if (auto filters = ex->getFilter().getSubFilters(); filters.size() == 1)
                {
                    auto first = filters.front();
                    if (first.matchWordIndex < 0)
                    {
                        spdlog::info("  name={}, filter={}", ex->objectName().toStdString(), to_string(first));
                        QJsonObject source_out;
                        auto name = ex->objectName();
                        if (name.startsWith(mm.typeName + "."))
                            name.remove(0, mm.typeName.size() + 1);
                        source_out["name"] = name;
                        source_out["filter"] = to_string(first).c_str();
                        sources_out.append(source_out);
                    }
                }
            }
        }

        if (sources_out.isEmpty())
        {
            spdlog::info("  no filters found");
            continue;
        }

        QJsonObject mm_out;
        mm_out["type_name"] = mm.typeName;
        mm_out["vendor_name"] = mm.vendorName;
        mm_out["display_name"] = mm.displayName;

        QJsonArray headerFilters_out;

        for (const auto &filterDef: mm.eventHeaderFilters)
        {
            QJsonObject jFilterDef;
            jFilterDef["filter"] = QString::fromLocal8Bit(filterDef.filterString);
            jFilterDef["description"] = filterDef.description;
            headerFilters_out.append(jFilterDef);
        }

        mm_out["header_filters"] = headerFilters_out;
        mm_out["vme_address"] = QString("0x%1").arg(mm.vmeAddress, 8, 16, QLatin1Char('0'));
        mm_out["data_sources"] = sources_out;
        modules_out.append(mm_out);
    }

    QJsonObject out;
    out["mvme_version"] = mvme_git_version();
    out["mvme_commit"] = mvme_git_hash();
    out["modules"] = modules_out;

    QJsonDocument doc(out);
    std::cout << doc.toJson().toStdString() << std::endl;

    return 0;
}
