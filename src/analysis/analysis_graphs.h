#ifndef __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_
#define __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_

#include <map>
#include <QUuid>
#include "analysis_fwd.h"

class QGVScene;
class QGVNode;
class QGVEdge;
class QGVSubGraph;

namespace analysis::graph
{

struct GraphContext
{
    QGVScene *scene;
    std::map<QUuid, QGVNode *> nodes;
    std::map<std::pair<QUuid, QUuid>, QGVEdge *> edges;
    std::map<QUuid, QGVSubGraph *> dirgraphs;

    GraphContext(QGVScene *scene_): scene(scene_) {}
    void clear(); // clears the scene and the item maps
};

using Attributes = std::map<QString, QString>;

struct GraphObjectAttributes
{
    Attributes graphAttributes =
    {
        { "rankdir", "LR" },
        { "compound", "true" },
        { "fontname", "Bitstream Vera Sans" },
    };

    Attributes nodeAttributes =
    {
        { "fontname", "Bitstream Vera Sans" },
        { "style", "filled" },
        { "fillcolor", "#fffbcc" },
    };

    Attributes edgeAttributes =
    {
        { "fontname", "Bitstream Vera Sans" },
    };
};

void apply_graph_attributes(QGVScene *scene, const GraphObjectAttributes &goa);
void create_graph(GraphContext &gctx, const AnalysisObjectPtr &obj, const GraphObjectAttributes &goa = {});

}

#endif // __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_