#ifndef __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_
#define __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_

#include <map>
#include <memory>
#include <QUuid>
#include <QWidget>
#include "analysis_fwd.h"
#include "libmvme_export.h"

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
    std::map<std::pair<QUuid, QUuid>, QGVEdge *> edges;
    std::map<QUuid, QGVSubGraph *> dirgraphs;
    QGVSubGraph *conditionsCluster = nullptr;

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
    public:
        explicit DependencyGraphWidget(QWidget *parent = nullptr);
        ~DependencyGraphWidget() override;

    public:
        AnalysisObjectPtr getObject() const;

    public slots:
        void setObject(const AnalysisObjectPtr &rootObj);
        void setGraphObjectAttributes(const GraphObjectAttributes &goa);
        void fitInView();

    protected:
        bool eventFilter(QObject *obj, QEvent *ev) override;

    private:
        friend class ShowObjectGraphCommand;
        struct Private;
        std::shared_ptr<Private> d;

};

LIBMVME_EXPORT DependencyGraphWidget *show_dependency_graph(const AnalysisObjectPtr &obj, const GraphObjectAttributes &goa = {});

}

#endif // __SSD2_DATA_SRC_MVME2_SRC_ANALYSIS_ANALYSIS_GRAPHS_H_