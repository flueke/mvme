#ifndef __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_
#define __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_

#include <map>
#include <QUuid>
#include "analysis_fwd.h"
#include "libmvme_export.h"

class QGVScene;
class QGVNode;
class QGVEdge;
class QGVSubGraph;

namespace analysis::graph
{

struct LIBMVME_EXPORT GraphContext
{
    QGVScene *scene; // must point to an existing object before use

    std::map<QUuid, QGVNode *> nodes;
    std::map<std::pair<QUuid, QUuid>, QGVEdge *> edges;
    std::map<QUuid, QGVSubGraph *> dirgraphs;
    QGVSubGraph *conditionsCluster = nullptr;

    explicit GraphContext(QGVScene *scene_): scene(scene_) {}
    void clear(); // clears the scene and the item maps
};

using Attributes = std::map<QString, QString>;

// global attributes set on the graph
struct LIBMVME_EXPORT GraphObjectAttributes
{
    Attributes graphAttributes =
    {
        { "rankdir", "LR" },
        { "compound", "true" },
        { "fontname", "Bitstream Vera Sans" },
    };

    Attributes nodeAttributes =
    {
        { "style", "filled" },
        { "fillcolor", "#fffbcc" },
        { "fontname", "Bitstream Vera Sans" },
    };

    Attributes edgeAttributes =
    {
        { "fontname", "Bitstream Vera Sans" },
    };
};

LIBMVME_EXPORT void apply_graph_attributes(QGVScene *scene, const GraphObjectAttributes &goa);
LIBMVME_EXPORT void create_graph(GraphContext &gctx, const AnalysisObjectPtr &rootObj, const GraphObjectAttributes &goa = {});
LIBMVME_EXPORT void new_graph(GraphContext &gctx, const GraphObjectAttributes &goa = {});

LIBMVME_EXPORT void show_dependency_graph(const AnalysisObjectPtr &obj);

}

#endif // __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_