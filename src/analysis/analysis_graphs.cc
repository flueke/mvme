#include "analysis_graphs.h"

#include <qgv.h>
#include <QApplication>
#include <QMouseEvent>
#include <QTimer>
#include <QToolBar>
#include <QUndoStack>
#include <QUndoCommand>
#include <QGraphicsSceneMouseEvent>
#include "analysis.h"
#include "analysis_ui_util.h"
#include "../analysis_service_provider.h"
#include "../graphviz_util.h"
#include "../qt_util.h"

using namespace analysis::ui;

namespace analysis::graph
{

using namespace mesytec::graphviz_util;

void GraphContext::clear()
{
    scene->clearGraphItems();
    nodes.clear();
    nodesToId.clear();
    edges.clear();
    dirgraphs.clear();
    conditionsCluster = nullptr;
}

// The template parameter Parent must provide an addNode() method like QGVScene or QGVSubGraph.
template<typename Parent>
QGVNode *object_graph_add_node(GraphContext &gctx, Parent *parent, const AnalysisObjectPtr &obj)
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

    auto objNode = parent->addNode(label, obj->getId().toString());
    gctx.nodes[obj->getId()] = objNode;
    gctx.nodesToId[objNode] = obj->getId();

    if (std::dynamic_pointer_cast<analysis::ConditionInterface>(obj))
    {
        objNode->setAttribute("shape", "hexagon");
        objNode->setAttribute("fillcolor", "lightblue");
    }

    if (std::dynamic_pointer_cast<analysis::SourceInterface>(obj))
        objNode->setAttribute("fillcolor", "lightgrey");

    return objNode;
}

QGVNode *object_graph_add_node(GraphContext &gctx, const AnalysisObjectPtr &obj)
{
    return object_graph_add_node(gctx, gctx.scene, obj);
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
    edge->setFlag(QGraphicsItem::ItemIsSelectable, false);
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
        gctx.nodesToId[node] = modId;
    }

    auto edgeKey = std::make_pair(modId, src->getId());

    if (!gctx.edges.count(edgeKey))
    {
        auto modNode = gctx.nodes[modId];
        auto srcNode = gctx.nodes[src->getId()];
        auto edge = gctx.scene->addEdge(modNode, srcNode);
        edge->setFlag(QGraphicsItem::ItemIsSelectable, false);
        gctx.edges[edgeKey] = edge;
    }

    return gctx.nodes[modId];
}

void object_graph_recurse_to_source(GraphContext &gctx, const analysis::OperatorPtr &op)
{
    auto opNode = gctx.nodes[op->getId()];
    assert(opNode);

    const auto slotCount = op->getNumberOfSlots();

    // inputs
    for (auto si = 0; si < slotCount; ++si)
    {
        auto slot = op->getSlot(si);

        if (slot->isConnected())
        {
            auto inputObj = slot->getSource()->shared_from_this();
            object_graph_add_node(gctx, inputObj);
            auto e = object_graph_add_edge(gctx, inputObj, op);

            if (slotCount > 1 && !slot->name.isEmpty())
            {
                e->setAttribute("headlabel", slot->name + "\n ");
            }

            if (auto inputOp = std::dynamic_pointer_cast<analysis::OperatorInterface>(inputObj))
                object_graph_recurse_to_source(gctx, inputOp);
            else if (auto inputSrc = std::dynamic_pointer_cast<analysis::SourceInterface>(inputObj))
                object_graph_add_module_for_source(gctx, inputSrc);
        }
    }

    // conditions:
    // if there are active conditions:
    //   create the condition cluster if it does not exist yet
    //   create each condition node in the cluster if it does not exist yet
    //   add edges from conditions to this operator
    auto condSet = op->getActiveConditions();

    if (!condSet.isEmpty())
    {
        if (!gctx.conditionsCluster)
        {
            gctx.conditionsCluster = gctx.scene->addSubGraph("conditions");
            gctx.conditionsCluster->setAttribute("label", "Conditions");
            gctx.conditionsCluster->setAttribute("style", "filled");
            gctx.conditionsCluster->setAttribute("fillcolor", "#eeeeee");
        }

        for (const auto &cond: condSet)
        {
            object_graph_add_node(gctx, gctx.conditionsCluster, cond);
            auto e = object_graph_add_edge(gctx, cond, op);
            e->setAttribute("color", "blue");
        }
    }
}

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
        object_graph_recurse_to_source(gctx, cond);
    }

    void visit(Directory *dir_) override
    {
        Q_UNUSED(dir_);
        //auto dir = std::dynamic_pointer_cast<Directory>(dir_->shared_from_this());
    }
};

LIBMVME_EXPORT GraphContext create_graph_context()
{
    auto [view, scene] = mesytec::graphviz_util::make_graph_view_and_scene();
    analysis::graph::GraphContext gctx{scene, view};
    return gctx;
}

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
    new_graph(gctx, goa);
    CreateGraphVisitor v{gctx};
    obj->accept(v);
    if (gctx.nodes.count(obj->getId()))
        gctx.scene->setRootNode(gctx.nodes[obj->getId()]);
    gctx.scene->applyLayout();
}

void new_graph(GraphContext &gctx, const GraphObjectAttributes &goa)
{
    gctx.scene->newGraph();
    gctx.clear();
    apply_graph_attributes(gctx.scene, goa);
}

struct DependencyGraphWidget::Private
{
    Private() {}

    DependencyGraphWidget *q = {};
    GraphContext gctx_;
    QToolBar *toolbar_ = {};
    QUndoStack history_;
    AnalysisObjectPtr obj_;
    AnalysisServiceProvider *asp_ = {};
    QAction *actionBack_ = {};
    QAction *actionForward_ = {};
    QAction *actionView = {};
    QAction *actionOpen = {};
    QAction *actionEdit = {};

    void setObject(const AnalysisObjectPtr &obj)
    {
        create_graph(gctx_, obj);

        if (auto ps = qobject_cast<PipeSourceInterface *>(obj.get()))
        {
            q->setWindowTitle(QSL("Dependency graph for %1 '%2'")
                .arg(ps->getDisplayName()).arg(obj->objectName()));
        }
        else
        {
            q->setWindowTitle(QSL("Dependency graph for '%1'").arg(obj->objectName()));
        }

        obj_ = obj;
    }

    QGVScene *scene() { return gctx_.scene; }
    QGraphicsView *view() { return gctx_.view; }

    QUuid objectId(const QGraphicsItem *item) const
    {
        if (auto qgvNode = dynamic_cast<const QGVNode *>(item))
            return gctx_.nodesToId.at(qgvNode);
        return {};
    }

    AnalysisObjectPtr object(const QGraphicsItem *item) const
    {
        if (auto obj = q->getObject())
        {
            if (obj->getAnalysis())
                return obj->getAnalysis()->getObject(objectId(item));
        }

        return {};
    }

    AnalysisObjectPtr selectedObject()
    {
        for (auto item : scene()->selectedItems())
        {
            if (auto obj = object(item))
                return obj;
        }

        return {};
    }

    void onSceneSelectionChanged();
    void onActionViewTriggered();
    void onActionOpenTriggered();
    void onActionEditTriggered();
};

class ShowObjectGraphCommand: public QUndoCommand
{
    public:
        ShowObjectGraphCommand(DependencyGraphWidget::Private *graphWidgetPrivate, const AnalysisObjectPtr &obj)
            : QUndoCommand(obj->objectName())
            , graphWidgetPrivate_(graphWidgetPrivate)
            , curObj_(obj)
        {}

        void redo() override
        {
            prevObj_ = graphWidgetPrivate_->q->getObject();
            graphWidgetPrivate_->setObject(curObj_);
            graphWidgetPrivate_->q->fitInView();
        }

        void undo() override
        {
            if (prevObj_)
            {
                graphWidgetPrivate_->setObject(prevObj_);
                prevObj_ = {};
                graphWidgetPrivate_->q->fitInView();
            }
        }

    private:
        DependencyGraphWidget::Private *graphWidgetPrivate_;
        AnalysisObjectPtr curObj_;
        AnalysisObjectPtr prevObj_;
};

DependencyGraphWidget::DependencyGraphWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : QWidget(parent)
    , d(std::make_shared<Private>())
{
    d->q = this;
    d->gctx_ = create_graph_context();
    d->toolbar_ = new QToolBar;
    d->asp_ = asp;

    setObjectName("AnalysisDependencyGraphWidget");

    auto layout = make_vbox(this);
    layout->addWidget(d->toolbar_);
    layout->addWidget(d->gctx_.view);
    layout->setStretch(0, 1);

    auto actionBack = d->history_.createUndoAction(this, QSL("Back to"));
    actionBack->setIcon(QIcon(QSL(":/arrow_left.png")));
    actionBack->setShortcut(QKeySequence("Alt-Right"));
    actionBack->setShortcutContext(Qt::WindowShortcut);

    auto actionForward = d->history_.createRedoAction(this, QSL("Forward to"));
    actionForward->setIcon(QIcon(QSL(":/arrow_right.png")));
    actionForward->setShortcut(QKeySequence("Alt+Left"));
    actionForward->setShortcutContext(Qt::WindowShortcut);

    d->actionBack_ = actionBack;
    d->actionForward_ = actionForward;
    d->actionView = new QAction(QIcon(":/node-select.png"), "View Graph");
    d->actionOpen = new QAction(QIcon(":/document-open.png"), "Open");
    d->actionEdit = new QAction(QIcon(":/pencil.png"), "Edit");

    d->toolbar_->addAction(actionBack);
    d->toolbar_->addAction(actionForward);
    d->toolbar_->addAction(d->actionView);
    d->toolbar_->addAction(d->actionOpen);
    d->toolbar_->addAction(d->actionEdit);

    // Handle forward/backward mouse button clicks
    installEventFilter(this);

    // Scene event handling (mouse clicks)
    d->scene()->installEventFilter(this);

    connect(d->scene(), &QGraphicsScene::selectionChanged,
            this, [this] { d->onSceneSelectionChanged(); });
    connect(d->actionView, &QAction::triggered,
            this, [this] { d->onActionViewTriggered(); });
    connect(d->actionOpen, &QAction::triggered,
            this, [this] { d->onActionOpenTriggered(); });
    connect(d->actionEdit, &QAction::triggered,
            this, [this] { d->onActionEditTriggered(); });
}

DependencyGraphWidget::~DependencyGraphWidget()
{
}

void DependencyGraphWidget::Private::onSceneSelectionChanged()
{
    auto obj = selectedObject();
    actionView->setEnabled(obj != nullptr);
    actionOpen->setEnabled(obj != nullptr && qobject_cast<SinkInterface *>(obj.get()));
    actionView->setEnabled(obj != nullptr);
}

void DependencyGraphWidget::Private::onActionViewTriggered()
{
    if (auto obj = selectedObject())
        q->setObject(obj);
}

void DependencyGraphWidget::Private::onActionOpenTriggered()
{
    if (auto obj = selectedObject())
    {
        if (auto sink = std::dynamic_pointer_cast<SinkInterface>(obj))
            show_sink_widget(asp_, sink);
    }
}

void DependencyGraphWidget::Private::onActionEditTriggered()
{
}

AnalysisObjectPtr DependencyGraphWidget::getObject() const
{
    return d->obj_;
}

void DependencyGraphWidget::setObject(const AnalysisObjectPtr &rootObj)
{
    if (getObject() && getObject() != rootObj)
    {
        auto cmd = new ShowObjectGraphCommand(d.get(), rootObj);
        d->history_.push(cmd);
    }
    else
        d->setObject(rootObj);
}

void DependencyGraphWidget::setGraphObjectAttributes(const GraphObjectAttributes &goa)
{
    apply_graph_attributes(d->gctx_.scene, goa);
}

void DependencyGraphWidget::fitInView()
{
    d->gctx_.view->fitInView(d->gctx_.view->scene()->sceneRect(), Qt::KeepAspectRatio);
}

bool DependencyGraphWidget::eventFilter(QObject *watched, QEvent *ev)
{
    if (watched == this && ev->type() == QEvent::MouseButtonPress)
    {
        auto mev = reinterpret_cast<QMouseEvent *>(ev);

        if (mev->button() == Qt::MouseButton::BackButton)
            d->actionBack_->trigger();
        else if (mev->button() == Qt::MouseButton::ForwardButton)
            d->actionForward_->trigger();
    }
    else if (watched == d->scene() && ev->type() == QEvent::GraphicsSceneMousePress && getObject())
    {
        auto mev = reinterpret_cast<QGraphicsSceneMouseEvent *>(ev);

        if (mev->modifiers() & Qt::KeyboardModifier::ControlModifier
            && mev->buttons() & Qt::MouseButton::LeftButton)
        {
            for (auto item: d->scene()->items(mev->scenePos()))
            {
                if (auto qgvNode = dynamic_cast<QGVNode *>(item))
                {
                    auto objectId = d->gctx_.nodesToId.at(qgvNode);
                    if (auto obj = getObject()->getAnalysis()->getObject(objectId))
                    {
                        setObject(obj);
                        break;
                    }
                }
            }
        }
    }

    return false;
}

DependencyGraphWidget *show_dependency_graph(
    AnalysisServiceProvider *asp, const AnalysisObjectPtr &obj, const GraphObjectAttributes &goa)
{
    DependencyGraphWidget *dgw = nullptr;
    auto widgets = QApplication::topLevelWidgets();

    for (auto w: widgets)
    {
        if ((dgw = qobject_cast<DependencyGraphWidget *>(w)))
            break;
    }

    if (!dgw)
    {
        dgw = new DependencyGraphWidget(asp);
        // Save/restore window position and size.
        auto geoSaver = new WidgetGeometrySaver(dgw);
        geoSaver->addAndRestore(dgw, "WindowGeometries/AnalysisDependencyGraphWidget");
        add_widget_close_action(dgw);
    }

    dgw->setGraphObjectAttributes(goa);
    dgw->setObject(obj);
    dgw->show();
    dgw->showNormal();
    dgw->raise();
    dgw->fitInView();

    return dgw;
}

}