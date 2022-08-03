#ifndef __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_
#define __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_

#include <map>
#include <QUuid>
#include "analysis_fwd.h"

class QGVScene;
class QGVNode;
class QGVEdge;
class QGVSubGraph;

namespace analysis
{

struct GraphContext
{
    QGVScene *scene;
    std::map<QUuid, QGVNode *> nodes;
    std::map<std::pair<QUuid, QUuid>, QGVEdge *> edges;
    std::map<QUuid, QGVSubGraph *> dirgraphs;

    void clear(); // clears the scene and the item maps
};

void create_graph(GraphContext &gctx, const AnalysisObjectPtr &obj);

}

#endif // __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_