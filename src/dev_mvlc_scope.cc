#include <QApplication>
#include <QDebug>
#include <limits>
#include "mvlc/trigger_io_scope.h"
#include "mvlc/trigger_io_scope_ui.h"
#include "mvme_qwt.h"

using namespace mesytec::mvme_mvlc;

struct ScopeData: public QwtSeriesData<QPointF>
{
    ScopeData(
        const trigger_io_scope::Timeline &timeline,
        const double offset,
        const unsigned preTriggerTime
        )
        : timeline(timeline)
        , offset(offset)
        , preTriggerTime(preTriggerTime)
    {}

    ~ScopeData()
    {
        qDebug() << __PRETTY_FUNCTION__  << this;
    }

    trigger_io_scope::Timeline timeline;
    double offset;
    unsigned preTriggerTime;

    virtual QRectF boundingRect() const override
    {
        auto tMin = std::numeric_limits<u16>::max();
        auto tMax = std::numeric_limits<u16>::min();

        for (const auto &sample: timeline)
        {
            tMin = std::min(tMin, sample.time);
            tMax = std::max(tMax, sample.time);
        }

        double tMinF = static_cast<double>(tMin) - preTriggerTime;
        double tRange = static_cast<double>(tMax) - tMin;

        return QRectF(tMinF, offset, tRange, 1.0);
        //return QRectF(tMin, offset, tMax-tMin, 1.0);
    }

    size_t size() const override
    {
        return timeline.size();
    }

    virtual QPointF sample(size_t i) const override
    {
        double time = timeline[i].time;
        double value = static_cast<double>(timeline[i].edge);
        return { time - preTriggerTime,  value + offset };
    }
};

int main(int argc, char *argv[])
{
    using namespace trigger_io_scope;

    Snapshot snapshot;
    snapshot.resize(10);
    for (auto &timeline: snapshot)
    {
        timeline.push_back({0, Edge::Rising});
        timeline.push_back({10, Edge::Falling});
        timeline.push_back({20, Edge::Rising});
        timeline.push_back({30, Edge::Falling});
        timeline.push_back({40, Edge::Rising});
        timeline.push_back({50, Edge::Falling});
        timeline.push_back({60, Edge::Rising});
    }


    auto tMin = std::numeric_limits<u16>::max();
    auto tMax = std::numeric_limits<u16>::min();

    for (const auto &timeline: snapshot)
    {
        for (const auto &sample: timeline)
        {
            tMin = std::min(tMin, sample.time);
            tMax = std::max(tMax, sample.time);
        }
    }

    const double Spacing = 0.5;
    unsigned PreTriggerTime = 30;

    // Plan is: make a curve data object
    // determine bounding rect
    // return samples
    // nope: determine bounding rect of all timelines in the snapshot

    std::vector<ScopeData *> scopeDatas;
    double offset = 0.0;

    for (const auto &timeline: snapshot)
    {
        scopeDatas.push_back(new ScopeData{ timeline, offset, PreTriggerTime });
        offset += 1.0 + Spacing;
    }

    QApplication app(argc, argv);
    QwtPlot plot;

    plot.setAxisScale(QwtPlot::yLeft, 0.0, offset);
    plot.enableAxis(QwtPlot::yLeft, false);

    int idx = 0;
    for (auto &scopeData: scopeDatas)
    {
        auto curve = new QwtPlotCurve(QString::number(idx));
        curve->setData(scopeData);
        curve->setStyle(QwtPlotCurve::Steps);
        curve->setRenderHint(QwtPlotItem::RenderAntialiased);
        curve->attach(&plot);
        ++idx;
    }

    plot.show();

    int ret =  app.exec();
    return ret;
}
