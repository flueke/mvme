#include "analysis_graphs.h"

#include <qgv.h>
#include "analysis.h"

namespace analysis
{

void GraphContext::clear()
{
    scene->clearGraphItems();
    nodes.clear();
    edges.clear();
    dirgraphs.clear();
}

class CreateGraphVisitor: public ObjectVisitor
{
    public:
        void visit(SourceInterface *source) override;
        void visit(OperatorInterface *op) override;
        void visit(SinkInterface *sink) override;
        void visit(ConditionInterface *cond) override;
        void visit(Directory *dir) override;
};

void CreateGraphVisitor::visit(SourceInterface *source)
{
}

void CreateGraphVisitor::visit(OperatorInterface *op)
{
}

void CreateGraphVisitor::visit(SinkInterface *sink)
{
    visit(reinterpret_cast<OperatorInterface *>(sink));
}

void CreateGraphVisitor::visit(ConditionInterface *cond)
{
}

void CreateGraphVisitor::visit(Directory *dir)
{
}

void create_graph(GraphContext &gctx, const AnalysisObjectPtr &obj)
{
}

}