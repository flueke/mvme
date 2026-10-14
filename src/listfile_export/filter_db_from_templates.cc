#include "listfile_export/filter_db_from_templates.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <spdlog/spdlog.h>

#include "analysis/analysis.h"
#include "analysis/analysis_serialization.h"

namespace mesytec::mvme::listfile_export
{

FilterDb filter_db_from_templates(const vats::MVMETemplates &templates)
{
    FilterDb db;
    auto objectFactory = analysis::Analysis().getObjectFactory();

    for (const auto &mm: templates.moduleMetas)
    {
        QFile filtersFile(QDir(mm.templatePath).filePath("analysis/default_filters.analysis"));

        if (!filtersFile.open(QIODevice::ReadOnly))
        {
            spdlog::debug("no default filters for module type {}", mm.typeName.toStdString());
            continue;
        }

        auto json = QJsonDocument::fromJson(filtersFile.readAll()).object()["AnalysisNG"].toObject();
        json = analysis::convert_to_current_version(json, nullptr);
        auto objectStore = analysis::deserialize_objects(json, objectFactory);

        ModuleDef md;
        md.typeName = mm.typeName.toStdString();
        md.vendorName = mm.vendorName.toStdString();
        md.displayName = mm.displayName.toStdString();

        for (const auto &src: objectStore.sources)
        {
            auto ex = qobject_cast<analysis::Extractor *>(src.get());

            if (!ex)
                continue;

            auto filters = ex->getFilter().getSubFilters();

            if (filters.size() != 1 || filters.front().matchWordIndex >= 0)
                continue;

            auto name = ex->objectName();
            if (name.startsWith(mm.typeName + "."))
                name.remove(0, mm.typeName.size() + 1);

            md.dataSources.push_back({name.toStdString(), to_string(filters.front())});
        }

        if (!md.dataSources.empty())
            db.modules.emplace_back(std::move(md));
    }

    return db;
}

} // namespace mesytec::mvme::listfile_export
