#include "dev_graphviz_lib_test5.h"
#include "ui_dev_graphviz_lib_test5_mainwin.h"

#include <map>
#include <QFileDialog>
#include <QGraphicsView>
#include <QGVCore/QGVEdge.h>
#include <QGVCore/QGVNode.h>
#include <QGVCore/QGVScene.h>
#include <QGVCore/QGVSubGraph.h>
#include <QJsonDocument>
#include <QMessageBox>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <QCollator>

#include "analysis/analysis.h"
#include "analysis/analysis_util.h"
#include "graphicsview_util.h"
#include "qt_util.h"


class TreeItem: public QTreeWidgetItem
{
    public:
        using QTreeWidgetItem::QTreeWidgetItem;

        bool operator<(const QTreeWidgetItem &other) const
        {
            static QCollator collator;
            collator.setCaseSensitivity(Qt::CaseSensitive);
            collator.setIgnorePunctuation(false);
            collator.setNumericMode(true);

            auto sa = this->data(0, Qt::DisplayRole).toString();
            auto sb = other.data(0, Qt::DisplayRole).toString();

            return collator.compare(sa, sb) < 0;
        }
};

struct MainWindow::Private
{
    explicit Private(MainWindow *q_) : q(q_) { }
    void on_action_openAnalysis();
    void on_action_saveGraph();
    void on_action_showGraphCode();
    void on_treeItem_clicked(QTreeWidgetItem *item, int col);

    void showAnalysisGraph();
    void showAnalysisObjectGraph(const analysis::AnalysisObjectPtr &obj);
    void showAnalysisTree();

    MainWindow *q;
    QTreeWidget *tree_;
    QGraphicsView *view_;
    QGVScene scene_;
    std::shared_ptr<analysis::Analysis> ana_;
    std::map<int, QTreeWidgetItem *> treeLevelItems_;
    std::map<QUuid, QTreeWidgetItem *> treeObjectItems_;
};

MainWindow::MainWindow()
    : d(std::make_unique<Private>(this))
    , ui_(new Ui::MainWindow)
{
    d->tree_ = new QTreeWidget;

    d->view_ = new QGraphicsView;
    d->view_->setScene(&d->scene_);
    d->view_->setRenderHints(
        QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    d->view_->setDragMode(QGraphicsView::ScrollHandDrag);
    d->view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    d->view_->setContextMenuPolicy(Qt::CustomContextMenu);
    new MouseWheelZoomer(d->view_, d->view_);

    ui_->setupUi(this);

    connect(ui_->action_openAnalysis, &QAction::triggered,
            this, [this] { d->on_action_openAnalysis(); });

    connect(ui_->action_saveGraph, &QAction::triggered,
        this, [this] { d->on_action_saveGraph(); });

    connect(ui_->action_showGraphCode, &QAction::triggered,
        this, [this] { d->on_action_showGraphCode(); });

    connect(d->tree_, &QTreeWidget::itemClicked,
        this, [this] (QTreeWidgetItem *item, int col) { d->on_treeItem_clicked(item, col); });

    auto l = make_hbox<0, 0>(ui_->centralwidget);
    l->addWidget(d->tree_);
    l->addWidget(d->view_);
    l->setStretch(1, 1);
}

MainWindow::~MainWindow()
{
}

void MainWindow::openAnalysis(const QString &filename)
{
    auto jdoc = gui_read_json_file(filename);

    if (jdoc.isNull())
        return;

    auto && [ana, ec] = analysis::read_analysis(jdoc);

    if (!ana && ec)
    {
        QMessageBox::critical(this, "Error loading analysis",
            QString("Error loading analysis from %1: %2").arg(filename).arg(ec.message().c_str()));
        return;
    }

    d->ana_ = std::move(ana);
    d->showAnalysisGraph();
    d->showAnalysisTree();
}

void MainWindow::Private::on_action_openAnalysis()
{
    QString path; // TODO: fill the initial path
    auto filename = QFileDialog::getOpenFileName(q,"Open Analysis", path, "*.analysis;;");

    if (filename.isEmpty())
        return;

    q->openAnalysis(filename);
}

void MainWindow::Private::on_action_saveGraph()
{
    agwrite(scene_.graph(), stdout);
}

void MainWindow::Private::on_action_showGraphCode()
{
    auto fp = std::tmpfile();
    agwrite(scene_.graph(), fp);
    auto bytes = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    QByteArray dest(bytes, '\0');
    std::fread(dest.data(), dest.size(), dest.size(), fp);

    auto tb = new QTextBrowser;
    tb->setText(QString::fromLocal8Bit(dest));
    tb->setAttribute(Qt::WA_DeleteOnClose, true);
    tb->resize(1200, 800);
    tb->show();
}

#define id_str(x) (x).toString().toLocal8Bit().data()

void MainWindow::Private::showAnalysisGraph()
{
    scene_.newGraph();
    scene_.setGraphAttribute("rankdir", "LR");
    scene_.setGraphAttribute("compound", "true");
    scene_.setGraphAttribute("fontname", "Bitstream Vera Sans");
    scene_.setNodeAttribute("fontname", "Bitstream Vera Sans");
    scene_.setEdgeAttribute("fontname", "Bitstream Vera Sans");

    std::map<QUuid, QGVNode *> nodes;
    std::map<std::pair<QUuid, QUuid>, QGVEdge *> edges;
    std::map<QUuid, QGVSubGraph *> dirgraphs;
    std::map<int, QGVSubGraph *> levelgraphs;
    std::map<int, QGVNode *> levelnodes;

    // clusters for analysis userlevels
    for (auto level = 0; level <= ana_->getMaxUserLevel(); ++level)
    {
        auto clusterLabel = QSL("userlevel%1").arg(level);
        auto sg = scene_.addSubGraph(clusterLabel, true);
        sg->setAttribute("label", clusterLabel);
        sg->setAttribute("style", "filled");
        sg->setAttribute("fillcolor", "#fefefe");
        levelgraphs[level] = sg;

        //auto nodeId = QSL("node_userlevel%1").arg(level);
        //auto n = sg->addNode(nodeId, nodeId);
        //n->setAttribute("style", "invis");
        //levelnodes[level] = n;
    }

    // invisible edges between the invisible nodes in the userlevel clusters
    //for (auto level = 0; level < ana_->getMaxUserLevel(); ++level)
    //{
    //    auto e = scene_.addEdge(levelnodes[level], levelnodes[level+1]);
    //    e->setAttribute("style", "invis");
    //}

    for (auto dir: ana_->getDirectories())
    {
        auto level = dir->getUserLevel();
        auto lg = levelgraphs[level];
        assert(lg);
        auto dg = lg->addSubGraph(id_str(dir->getId()), true);
        dg->setAttribute("label", dir->objectName());
        dg->setAttribute("style", "filled");
        dg->setAttribute("fillcolor", "#ffffe0");
        dirgraphs[dir->getId()] = dg;
    }

    for (auto src: ana_->getSources())
    {
        auto modId = src->getModuleId();

        if (!nodes.count(modId))
        {
            auto modulename = ana_->getModuleProperty(modId, "moduleName").toString();
            auto mn = levelgraphs[src->getUserLevel()]->addNode(id_str(modId));
            if (!modulename.isEmpty())
                mn->setAttribute("label", modulename);
            mn->setAttribute("shape", "box");
            nodes[modId] = mn;
        }

        auto mn = nodes[modId];
        assert(mn);

        auto sn = levelgraphs[src->getUserLevel()]->addNode(id_str(src->getId()));
        sn->setAttribute("label", src->objectName());
        nodes[src->getId()] = sn;

        auto e = scene_.addEdge(mn, sn);
        edges[std::make_pair(modId, src->getId())] = e;
    }

    for (auto op: ana_->getOperators())
    {
        if (!nodes.count(op->getId()))
        {
            QGVNode *on{};

            auto pd = ana_->getParentDirectory(op);

            if (pd && dirgraphs[pd->getId()])
                on = dirgraphs[pd->getId()]->addNode(id_str(op->getId()));
            else if (levelgraphs.count(op->getUserLevel()))
                on = levelgraphs[op->getUserLevel()]->addNode(id_str(op->getId()));
            else
                on = scene_.addNode(id_str(op->getId()));

            on->setAttribute("label", "<<b>" + op->objectName() + "</b>>");
            nodes[op->getId()] = on;
        }
    }

    for (auto obj: ana_->getAllObjects())
    {
        if (auto ps = qobject_cast<const analysis::PipeSourceInterface *>(obj.get()))
        {
            if (auto psnode = nodes[ps->getId()])
            {
                for (int oi = 0; oi < ps->getNumberOfOutputs(); ++oi)
                {
                    auto outpipe = ps->getOutput(oi);

                    for (auto destslot : outpipe->getDestinations())
                    {
                        if (auto destobj = destslot->parentOperator)
                        {
                            auto key = std::make_pair(ps->getId(), destobj->getId());

                            if (!edges.count(key))
                            {
                                if (auto destnode = nodes[destobj->getId()])
                                {
                                    auto e = scene_.addEdge(psnode, destnode);
                                    edges[key] = e;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    scene_.applyLayout();
}

struct GraphContext
{
    QGVScene *scene;
    std::map<QUuid, QGVNode *> nodes;
    std::map<std::pair<QUuid, QUuid>, QGVEdge *> edges;
    //std::map<QUuid, QGVSubGraph *> dirgraphs;
    //std::map<int, QGVSubGraph *> levelgraphs;
    //std::map<int, QGVNode *> levelnodes;
};

QGVNode *object_graph_add_node(
    GraphContext &gctx,
    const analysis::AnalysisObjectPtr &obj)
{
    if (gctx.nodes.count(obj->getId()))
        return gctx.nodes[obj->getId()];

    QString label = obj->objectName();

    if (auto ps = std::dynamic_pointer_cast<analysis::PipeSourceInterface>(obj))
        label = QSL("<<b>%1</b><br/>%2>").arg(ps->getDisplayName()).arg(ps->objectName());

    auto objNode = gctx.scene->addNode(label, obj->getId().toString());
    gctx.nodes[obj->getId()] = objNode;

    if (auto cond = std::dynamic_pointer_cast<analysis::ConditionInterface>(obj))
        objNode->setAttribute("shape", "hexagon");
    else if (auto src = std::dynamic_pointer_cast<analysis::SourceInterface>(obj))
        objNode->setAttribute("shape", "box");

    return objNode;
}

QGVEdge *object_graph_add_edge(
    GraphContext &gctx,
    const analysis::AnalysisObjectPtr &srcObj,
    const analysis::AnalysisObjectPtr &dstObj)
{
    auto srcNode = gctx.nodes[srcObj->getId()];
    auto dstNode = gctx.nodes[dstObj->getId()];

    if (!srcNode || !dstNode)
        return {};

    auto key = std::make_pair(srcObj->getId(), dstObj->getId());

    if (gctx.edges.count(key))
        return gctx.edges[key];

    auto edge = gctx.scene->addEdge(srcNode, dstNode);
    gctx.edges[key] = edge;
    return edge;
}

void object_graph_add_source_module(GraphContext &gctx, const analysis::SourcePtr &src)
{
    assert(gctx.nodes[src->getId()]); // source node must exist already

    auto modId = src->getModuleId();

    if (!gctx.nodes.count(modId))
    {
        auto label = src->getAnalysis()->getModuleProperty(modId, "moduleName").toString();
        auto node = gctx.scene->addNode(label, modId.toString());
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
}

void object_graph_recurse_on_inputs(GraphContext &gctx, const analysis::OperatorPtr &op)
{
    auto opNode = gctx.nodes[op->getId()];
    assert(opNode);

    for (auto si = 0; si < op->getNumberOfSlots(); ++si)
    {
        auto slot = op->getSlot(si);

        if (slot->isConnected())
        {
            auto inputObj = slot->getSource()->shared_from_this();
            object_graph_add_node(gctx, inputObj);
            object_graph_add_edge(gctx, inputObj, op);
            if (auto inputOp = std::dynamic_pointer_cast<analysis::OperatorInterface>(inputObj))
                object_graph_recurse_on_inputs(gctx, inputOp);
            else if (auto inputSrc = std::dynamic_pointer_cast<analysis::SourceInterface>(inputObj))
                object_graph_add_source_module(gctx, inputSrc);
        }
    }
}

void MainWindow::Private::showAnalysisObjectGraph(const analysis::AnalysisObjectPtr &obj)
{
    scene_.newGraph();
    scene_.setGraphAttribute("rankdir", "LR");
    scene_.setGraphAttribute("compound", "true");
    scene_.setGraphAttribute("fontname", "Bitstream Vera Sans");
    scene_.setNodeAttribute("fontname", "Bitstream Vera Sans");
    scene_.setEdgeAttribute("fontname", "Bitstream Vera Sans");

    GraphContext gctx{};
    gctx.scene = &scene_;

    auto objNode = object_graph_add_node(gctx, obj);
    objNode->setAttribute("fillcolor", "#aa0000");
    objNode->setAttribute("style", "filled");
    scene_.setRootNode(objNode);

    if (auto op = std::dynamic_pointer_cast<analysis::OperatorInterface>(obj))
        object_graph_recurse_on_inputs(gctx, op);

    scene_.applyLayout();
}

void MainWindow::Private::showAnalysisTree()
{
    tree_->clear();
    treeLevelItems_.clear();
    treeObjectItems_.clear();

    std::map<int, QTreeWidgetItem *> levelItems;
    std::map<QUuid, QTreeWidgetItem *> objectItems;

    for (auto level = 0; level <= ana_->getMaxUserLevel(); ++level)
    {
        auto item = new TreeItem(tree_->invisibleRootItem(), { QSL("userlevel%1").arg(level) });
        levelItems[level] = item;
    }

    for (auto src: ana_->getSources())
    {
        auto modId = src->getModuleId();

        if (!objectItems.count(modId))
        {
            auto item = new TreeItem(levelItems[0], { ana_->getModuleProperty(modId, "moduleName").toString() });
            item->setData(0, Qt::UserRole, modId);
            objectItems[modId] = item;
        }

        auto item = new TreeItem(objectItems[modId], { src->objectName() });
        item->setData(0, Qt::UserRole, src->getId());
        objectItems[src->getId()] = item;
    }

    for (const auto &obj: ana_->getAllObjects())
    {
        if (std::dynamic_pointer_cast<analysis::Directory>(obj))
            continue;

        if (objectItems.count(obj->getId()))
            continue;

        auto parentItem = levelItems[obj->getUserLevel()];

        auto parentDirs = ana_->getParentDirectories(obj);
        std::reverse(parentDirs.begin(), parentDirs.end());

        for (const auto &dir: parentDirs)
        {
            if (!objectItems.count(dir->getId()))
            {
                auto item = new TreeItem(parentItem, { dir->objectName() });
                item->setIcon(0, QIcon(":/folder_orange.png"));
                item->setData(0, Qt::UserRole, dir->getId());
                objectItems[dir->getId()] = item;
            }

            auto dirItem = objectItems[dir->getId()];
            parentItem = dirItem;
        }

        auto item = new TreeItem(parentItem, { obj->objectName() });
        item->setData(0, Qt::UserRole, obj->getId());
        objectItems[obj->getId()] = item;
    }

    tree_->sortByColumn(0, Qt::AscendingOrder);
    treeLevelItems_ = levelItems;
    treeObjectItems_ = objectItems;
}

void MainWindow::Private::on_treeItem_clicked(QTreeWidgetItem *item, int col)
{
    auto objId = item->data(0, Qt::UserRole).toUuid();

    if (auto obj = ana_->getObject(objId))
        showAnalysisObjectGraph(obj);
}