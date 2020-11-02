#include <gtest/gtest.h>
#include <QJsonDocument>
#include "analysis/analysis_serialization.h"
#include "analysis/analysis.h"

using namespace analysis;

static const char *v3AnalysisJSON =
#include "analysis_v3_to_v4_migration.old.analysis"
;

TEST(Analysis_V3_to_V4_Migration, Migration)
{
    auto doc  = QJsonDocument::fromJson(v3AnalysisJSON);
    auto json = doc.object()["AnalysisNG"].toObject();
    ASSERT_EQ(json["MVMEAnalysisVersion"].toInt(), 3);
    json = convert_to_current_version(json, nullptr);
    ASSERT_EQ(json["MVMEAnalysisVersion"].toInt(), Analysis::getCurrentAnalysisVersion());
    auto objects = deserialize_objects(json, Analysis().getObjectFactory());

    const auto &dirs = objects.directories;

    auto dir_exists = [&dirs] (const auto &name) -> bool
    {
        auto it = std::find_if(dirs.begin(), dirs.end(),
                               [&name] (auto &dir) { return dir->objectName() == name; });
        return it != dirs.end();
    };

    ASSERT_TRUE(dir_exists("Raw Histos madc32"));
    ASSERT_TRUE(dir_exists("Raw Histos mdpp16_csi"));
    ASSERT_TRUE(dir_exists("Raw Histos mdpp32_padc"));
    ASSERT_TRUE(dir_exists("Raw Histos mqdc32"));
}
