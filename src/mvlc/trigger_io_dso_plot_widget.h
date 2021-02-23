#ifndef __MVME_MVLC_TRIGGER_IO_DSO_PLOT_WIDGET_H__
#define __MVME_MVLC_TRIGGER_IO_DSO_PLOT_WIDGET_H__

#include <QWidget>
#include <chrono>
#include "libmvme_export.h"
#include "mvlc/trigger_io_dso.h"

class QwtPlot;

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

// Plot widget for DSO or simulated traces.
class LIBMVME_EXPORT DSOPlotWidget: public QWidget
{
    Q_OBJECT
    signals:
        void traceClicked(const Trace &trace, const QString &name);

    public:
        DSOPlotWidget(QWidget *parent = nullptr);
        ~DSOPlotWidget() override;

        void setTraces(
            const Snapshot &snapshot,
            unsigned preTriggerTime = 0,
            const QStringList &names = {});

        void setPreTriggerTime(double preTriggerTime); // FIXME: move into setTraces()?
        void setPostTriggerTime(double postTriggerTime); // FIXME: move into setTraces()?

        // Set the vector element to true if the trace with the corresponding
        // index should be treated as a trigger trace, e.g. drawn in a
        // different style/color. Must be called after setTraces to have an
        // effect.
        void setTriggerTraceInfo(const std::vector<bool> &triggerTraces);

        void setXInterval(double xMin, double xMax);
        void setXAutoScale();

        QwtPlot *getQwtPlot();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_DSO_PLOT_WIDGET_H__ */
