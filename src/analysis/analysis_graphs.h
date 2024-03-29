#ifndef __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_
#define __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_

#include <map>
#include <memory>
#include <QUuid>
#include <QWidget>
#include "analysis_fwd.h"
#include "libmvme_export.h"

class AnalysisServiceProvider;
class QGVScene;
class QGVNode;
class QGVEdge;
class QGVSubGraph;
class QGraphicsView;

namespace analysis::graph
{

struct LIBMVME_EXPORT GraphContext
{
    QGVScene *scene; // non-owning
    QGraphicsView *view; // non-owning

    std::map<QUuid, QGVNode *> nodes;
    std::map<const QGVNode *, QUuid> nodesToId;
    std::map<std::pair<QUuid, QUuid>, QGVEdge *> edges;
    std::map<QUuid, QGVSubGraph *> dirgraphs;

    // Cluster for holding the conditions being used by objects in the graph.
    // Arrows point from the conditions in this cluster to the objects using
    // them.
    QGVSubGraph *conditionsCluster = nullptr;

    // Cluster for objects that use the condition being shown.
    // Only used when the root object is a condition. Arrows point from the
    // condition to the objects referencing it.
    QGVSubGraph *conditionDependees = nullptr;

    explicit GraphContext(QGVScene *scene_ = nullptr, QGraphicsView *view_ = nullptr)
        : scene(scene_)
        , view(view_)
        {}
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

LIBMVME_EXPORT GraphContext create_graph_context();
LIBMVME_EXPORT void apply_graph_attributes(QGVScene *scene, const GraphObjectAttributes &goa);
LIBMVME_EXPORT void create_graph(GraphContext &gctx, const AnalysisObjectPtr &rootObj, const GraphObjectAttributes &goa = {});
LIBMVME_EXPORT void new_graph(GraphContext &gctx, const GraphObjectAttributes &goa = {});

class LIBMVME_EXPORT DependencyGraphWidget: public QWidget
{
    Q_OBJECT
    signals:
        void editObject(const AnalysisObjectPtr &obj);

    public:
        explicit DependencyGraphWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~DependencyGraphWidget() override;

    public:
        AnalysisObjectPtr getRootObject() const;

    public slots:
        void setRootObject(const AnalysisObjectPtr &rootObj);
        void setGraphObjectAttributes(const GraphObjectAttributes &goa);
        void fitInView();

    protected:
        bool eventFilter(QObject *obj, QEvent *ev) override;

    private:
        friend class ShowObjectGraphCommand;
        struct Private;
        std::shared_ptr<Private> d;

};

// Returns a pointer to the last used DependencyGraphWidget or nullptr if no
// widget is found.
LIBMVME_EXPORT DependencyGraphWidget *find_dependency_graph_widget();

// Creates a new or reuses the last used DependencyGraphWidget to show the graph
// for the given obj.
LIBMVME_EXPORT DependencyGraphWidget *show_dependency_graph(
    AnalysisServiceProvider *asp, const AnalysisObjectPtr &obj, const GraphObjectAttributes &goa = {});

}

#endif // __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_