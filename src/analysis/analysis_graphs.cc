#include "analysis_graphs.h"

#include <qgv.h>
#include "analysis.h"
#include "../graphviz_util.h"

namespace analysis::graph
{

using namespace mesytec::graphviz_util;

void GraphContext::clear()
{
    scene->clearGraphItems();
    nodes.clear();
    edges.clear();
    dirgraphs.clear();
}

QGVNode *object_graph_add_node(GraphContext &gctx, const AnalysisObjectPtr &obj)
{
    if (gctx.nodes.count(obj->getId()))
        return gctx.nodes[obj->getId()];

    QString label;

    if (auto exprCond = std::dynamic_pointer_cast<analysis::ExpressionCondition>(obj))
    {
        label = QSL("<<b>%1</b><br/>%2<br/><i>%3</i>>")
                    .arg(escape_dot_string_q(exprCond->getDisplayName()))
                    .arg(escape_dot_string_q(exprCond->objectName()))
                    .arg(escape_dot_string_q(exprCond->getExpression()));
    }
    else if (auto ps = std::dynamic_pointer_cast<analysis::PipeSourceInterface>(obj))
    {
        label = QSL("<<b>%1</b><br/>%2>")
                    .arg(escape_dot_string_q(ps->getDisplayName()))
                    .arg(escape_dot_string_q(ps->objectName()));
    }

    auto objNode = gctx.scene->addNode(label, obj->getId().toString());
    gctx.nodes[obj->getId()] = objNode;

    if (std::dynamic_pointer_cast<analysis::ConditionInterface>(obj))
        objNode->setAttribute("shape", "hexagon");

    if (std::dynamic_pointer_cast<analysis::SourceInterface>(obj))
        objNode->setAttribute("fillcolor", "lightgrey");

    return objNode;
}

QGVEdge *object_graph_add_edge(
    GraphContext &gctx,
    const analysis::AnalysisObjectPtr &srcObj,
    const analysis::AnalysisObjectPtr &dstObj)
{
    const auto key = std::make_pair(srcObj->getId(), dstObj->getId());

    if (gctx.edges.count(key))
        return gctx.edges[key];

    auto srcNode = gctx.nodes[srcObj->getId()];
    auto dstNode = gctx.nodes[dstObj->getId()];

    if (!srcNode || !dstNode)
        return {};

    auto edge = gctx.scene->addEdge(srcNode, dstNode);
    gctx.edges[key] = edge;

    return edge;
}

// Adds a node for the module the given src is attached to, if the node does not exist yet.
QGVNode *object_graph_add_module_for_source(GraphContext &gctx, const analysis::SourcePtr &src)
{
    assert(gctx.nodes[src->getId()]); // source node must exist already

    auto modId = src->getModuleId();

    if (!gctx.nodes.count(modId))
    {
        auto moduleName = src->getAnalysis()->getModuleProperty(modId, "moduleName").toString();
        auto label = QSL("<<b>Module</b><br/>%1>")
                         .arg(escape_dot_string_q(moduleName));
        auto node = gctx.scene->addNode(label, modId.toString());
        node->setAttribute("shape", "box");
        node->setAttribute("fillcolor", "lightgreen");
        gctx.nodes[modId] = node;
    }

    auto edgeKey = std::make_pair(modId, src->getId());

    if (!gctx.edges.count(edgeKey))
    {
        auto modNode = gctx.nodes[modId];
        auto srcNode = gctx.nodes[src->getId()];
        auto edge = gctx.scene->addEdge(modNode, srcNode);
        gctx.edges[edgeKey] = edge;
    }

    return gctx.nodes[modId];
}

void object_graph_recurse_to_source(GraphContext &gctx, const analysis::OperatorPtr &op)
{
    auto opNode = gctx.nodes[op->getId()];
    assert(opNode);

    // inputs
    for (auto si = 0; si < op->getNumberOfSlots(); ++si)
    {
        auto slot = op->getSlot(si);

        if (slot->isConnected())
        {
            auto inputObj = slot->getSource()->shared_from_this();
            object_graph_add_node(gctx, inputObj);
            object_graph_add_edge(gctx, inputObj, op);
            if (auto inputOp = std::dynamic_pointer_cast<analysis::OperatorInterface>(inputObj))
                object_graph_recurse_to_source(gctx, inputOp);
            else if (auto inputSrc = std::dynamic_pointer_cast<analysis::SourceInterface>(inputObj))
                object_graph_add_module_for_source(gctx, inputSrc);
        }
    }

    // conditions
    // XXX: leftoff here
}

#if 1
struct CreateGraphVisitor: public ObjectVisitor
{
    GraphContext &gctx;

    CreateGraphVisitor(GraphContext &gctx_): gctx(gctx_) {}

    void visit(SourceInterface *source) override
    {
        auto src = std::dynamic_pointer_cast<SourceInterface>(source->shared_from_this());
        object_graph_add_node(gctx, src);
        object_graph_add_module_for_source(gctx, src);
    }

    void visit(OperatorInterface *op_) override
    {
        auto op = std::dynamic_pointer_cast<OperatorInterface>(op_->shared_from_this());
        auto node = object_graph_add_node(gctx, op);
        node->setAttribute("fillcolor", "#fff580");
        object_graph_recurse_to_source(gctx, op);
    }

    void visit(SinkInterface *sink) override
    {
        visit(reinterpret_cast<OperatorInterface *>(sink));
    }

    void visit(ConditionInterface *cond_) override
    {
        auto cond = std::dynamic_pointer_cast<ConditionInterface>(cond_->shared_from_this());
        object_graph_add_node(gctx, cond);
    }

    void visit(Directory *dir_) override
    {
        auto dir = std::dynamic_pointer_cast<Directory>(dir_->shared_from_this());
    }
};
#endif

void apply_graph_attributes(QGVScene *scene, const GraphObjectAttributes &goa)
{
    for (auto & [key, value]: goa.graphAttributes)
        scene->setGraphAttribute(key, value);

    for (auto & [key, value]: goa.nodeAttributes)
        scene->setNodeAttribute(key, value);

    for (auto & [key, value]: goa.edgeAttributes)
        scene->setEdgeAttribute(key, value);
}

void create_graph(GraphContext &gctx, const AnalysisObjectPtr &obj, const GraphObjectAttributes &goa)
{
    gctx.scene->newGraph();
    apply_graph_attributes(gctx.scene, goa);
    CreateGraphVisitor v{gctx};
    obj->accept(v);
    gctx.scene->setRootNode(gctx.nodes[obj->getId()]);
    gctx.scene->applyLayout();
}
}