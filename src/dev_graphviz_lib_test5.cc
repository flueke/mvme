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

#include "analysis/analysis.h"
#include "analysis/analysis_util.h"
#include "graphicsview_util.h"
#include "qt_util.h"

struct MainWindow::Private
{
    explicit Private(MainWindow *q_) : q(q_) { }
    void on_action_openAnalysis();
    void showAnalysisGraph();

    MainWindow *q;
    QGraphicsView *view_;
    QGVScene scene_;
    std::shared_ptr<analysis::Analysis> ana_;
};

MainWindow::MainWindow()
    : d(std::make_unique<Private>(this))
    , ui_(new Ui::MainWindow)
{
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
            this, [this] () { d->on_action_openAnalysis(); });

    auto l = make_hbox<0, 0>(ui_->centralwidget);
    l->addWidget(d->view_);
}

MainWindow::~MainWindow()
{
}

void MainWindow::Private::on_action_openAnalysis()
{
    QString path; // TODO: fill the initial path
    auto filename = QFileDialog::getOpenFileName(q,"Open Analysis", path, "*.analysis;;");

    if (filename.isEmpty())
        return;

    auto jdoc = gui_read_json_file(filename);

    if (jdoc.isNull())
        return;

    auto && [ana, ec] = analysis::read_analysis(jdoc);

    if (!ana && ec)
    {
        QMessageBox::critical(q, "Error loading analysis",
            QString("Error loading analysis from %1: %2").arg(filename).arg(ec.message().c_str()));
        return;
    }

    ana_ = std::move(ana);
    showAnalysisGraph();
}

#define id_str(x) (x).toString().toLocal8Bit().data()

void MainWindow::Private::showAnalysisGraph()
{
    scene_.newGraph();
    scene_.setGraphAttribute("rankdir", "LR");

    std::map<QUuid, QGVNode *> nodes;
    std::map<std::pair<QUuid, QUuid>, QGVEdge *> edges;
    std::map<QUuid, QGVSubGraph *> dirgraphs;
    std::map<int, QGVSubGraph *> levelgraphs;

    for (auto level = 0; level <= ana_->getMaxUserLevel(); ++level)
    {
        auto sg = scene_.addSubGraph(QString("userlevel%1").arg(level), true);
        sg->setAttribute("label", QString("userlevel%1").arg(level));
        sg->setAttribute("style", "filled");
        sg->setAttribute("fillcolor", "#eeeeee");
        levelgraphs[level] = sg;
    }

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
            QString modulename;
            for (const auto &props: ana_->getModulePropertyList())
            {
                auto pm = props.toMap();
                if (pm["moduleId"] == modId.toString())
                {
                    modulename = pm["moduleName"].toString();
                    break;
                }
            }

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

            nodes[op->getId()] = on;

            on->setAttribute("label", "<b>" + op->objectName() + "</b>");
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